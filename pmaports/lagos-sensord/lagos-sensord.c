// SPDX-License-Identifier: MIT
// Bridge the MT6768's sensor hub to standard Linux input devices.
//
// The sensors run on the SCP coprocessor and MediaTek's sensorHub delivers
// them through its own sensor class (/dev/m_*_misc, enabled through
// /sys/class/sensor/m_*_misc), which only Android's sensor HAL speaks. This
// daemon re-publishes them as uinput devices that iio-sensor-proxy serves:
//
// - the accelerometer, with INPUT_PROP_ACCELEROMETER (udev tags it
//   ID_INPUT_ACCELEROMETER), for screen rotation. It only runs while the
//   display is on (DRM connector dpms).
// - the proximity sensor, as the SW_FRONT_PROXIMITY switch, which sxmo uses
//   to blank the screen during calls. It only runs while sxmo's proximity
//   lock does (its sxmo_proximity_lock_running wakelock), screen on or off.

#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define DISPLAY_DPMS "/sys/class/drm/card0-DSI-1/dpms"
#define WAKE_LOCKS "/sys/power/wake_lock"
#define PROXIMITY_LOCK "sxmo_proximity_lock_running"

// Sample period while a sensor is on
#define PERIOD_NS 100000000LL

// iio-sensor-proxy reads input accelerometers as 1 g = 256; the SCP reports
// thousandths of m/s²
#define COUNTS_PER_G 256
#define MILLI_STANDARD_GRAVITY 9807
#define AXIS_LIMIT (8 * COUNTS_PER_G)

// Proximity events carry the hub's "oneshot" value + 1: 0 is near, 1 far
#define PROXIMITY_NEAR 1

// struct sensor_event from MediaTek's sensor 1.0 framework (packed)
struct sensor_event {
	int64_t time_stamp;
	int8_t handle;
	int8_t flush_action;
	int8_t status;
	int8_t reserved;
	int32_t word[6];
} __attribute__((packed));

#define DATA_ACTION 0

struct bridged_sensor {
	const char *device;      // /dev/m_*_misc
	const char *control;     // /sys/class/sensor/m_*_misc/
	const char *prefix;      // attribute prefix: "acc", "ps"
	int handle;              // hub sensor handle
	int input;               // uinput device
	int data;                // sensor device
	int running;
};

static volatile sig_atomic_t stopping;

static void handle_stop(int signal_number)
{
	(void)signal_number;
	stopping = 1;
}

static int write_attribute(const struct bridged_sensor *sensor, const char *name,
			   const char *value)
{
	char path[128];
	int fd;
	ssize_t written;

	snprintf(path, sizeof(path), "%s%s%s", sensor->control, sensor->prefix, name);
	fd = open(path, O_WRONLY);
	if (fd < 0) {
		perror(path);
		return -1;
	}
	written = write(fd, value, strlen(value));
	close(fd);
	return written < 0 ? -1 : 0;
}

static void set_sensor(struct bridged_sensor *sensor, int enable)
{
	if (enable == sensor->running)
		return;
	if (enable) {
		char batch[64];
		// handle, no flags, sample period, no batching latency
		snprintf(batch, sizeof(batch), "%d,0,%lld,0", sensor->handle, PERIOD_NS);
		if (write_attribute(sensor, "batch", batch) < 0)
			return;
	}
	if (write_attribute(sensor, "active", enable ? "1" : "0") == 0)
		sensor->running = enable;
}

static int read_text(const char *path, char *text, size_t size)
{
	int fd = open(path, O_RDONLY);
	ssize_t length;

	text[0] = 0;
	if (fd < 0)
		return -1;
	length = read(fd, text, size - 1);
	close(fd);
	if (length < 0)
		return -1;
	text[length] = 0;
	return 0;
}

static int display_is_on(void)
{
	char state[8];

	if (read_text(DISPLAY_DPMS, state, sizeof(state)) < 0)
		return 1;
	return strncmp(state, "On", 2) == 0;
}

static int proximity_wanted(void)
{
	char locks[4096];

	if (read_text(WAKE_LOCKS, locks, sizeof(locks)) < 0)
		return 0;
	return strstr(locks, PROXIMITY_LOCK) != NULL;
}

static int create_input(const char *name, void (*describe)(int fd))
{
	struct uinput_setup setup = { 0 };
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

	if (fd < 0) {
		perror("/dev/uinput");
		return -1;
	}
	describe(fd);
	setup.id.bustype = BUS_VIRTUAL;
	snprintf(setup.name, sizeof(setup.name), "%s", name);
	if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
		perror("uinput");
		close(fd);
		return -1;
	}
	return fd;
}

static void describe_accelerometer(int fd)
{
	struct uinput_abs_setup axis = { 0 };

	ioctl(fd, UI_SET_EVBIT, EV_ABS);
	ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_ACCELEROMETER);
	axis.absinfo.minimum = -AXIS_LIMIT;
	axis.absinfo.maximum = AXIS_LIMIT;
	axis.absinfo.resolution = COUNTS_PER_G;
	for (int code = ABS_X; code <= ABS_Z; code++) {
		ioctl(fd, UI_SET_ABSBIT, code);
		axis.code = code;
		ioctl(fd, UI_ABS_SETUP, &axis);
	}
}

static void describe_proximity(int fd)
{
	ioctl(fd, UI_SET_EVBIT, EV_SW);
	ioctl(fd, UI_SET_SWBIT, SW_FRONT_PROXIMITY);
}

static void emit(int fd, uint16_t type, uint16_t code, int32_t value)
{
	struct input_event event = { .type = type, .code = code, .value = value };

	if (write(fd, &event, sizeof(event)) < 0 && errno != EAGAIN)
		perror("uinput write");
}

static int32_t to_counts(int32_t milli_mps2)
{
	int64_t counts = (int64_t)milli_mps2 * COUNTS_PER_G / MILLI_STANDARD_GRAVITY;

	if (counts > AXIS_LIMIT)
		return AXIS_LIMIT;
	if (counts < -AXIS_LIMIT)
		return -AXIS_LIMIT;
	return (int32_t)counts;
}

static void forward_accelerometer(int input, const struct sensor_event *event)
{
	// The SCP uses Android's axes (upright: y = +g, left edge up: x = -g);
	// iio-sensor-proxy calls the device upright when y is negative and
	// left-up when x is positive, so turn the frame 180 degrees around z.
	// Face up stays z = +g in both.
	emit(input, EV_ABS, ABS_X, to_counts(-event->word[0]));
	emit(input, EV_ABS, ABS_Y, to_counts(-event->word[1]));
	emit(input, EV_ABS, ABS_Z, to_counts(event->word[2]));
	emit(input, EV_SYN, SYN_REPORT, 0);
}

static void forward_proximity(int input, const struct sensor_event *event)
{
	emit(input, EV_SW, SW_FRONT_PROXIMITY, event->word[0] == PROXIMITY_NEAR);
	emit(input, EV_SYN, SYN_REPORT, 0);
}

static void forward(struct bridged_sensor *sensor,
		    void (*forward_event)(int, const struct sensor_event *))
{
	struct sensor_event events[16];
	ssize_t length = read(sensor->data, events, sizeof(events));

	for (ssize_t offset = 0; offset + (ssize_t)sizeof(events[0]) <= length;
	     offset += sizeof(events[0])) {
		const struct sensor_event *event =
			(const struct sensor_event *)((const char *)events + offset);

		if (event->handle == sensor->handle && event->flush_action == DATA_ACTION)
			forward_event(sensor->input, event);
	}
}

static int open_sensor(struct bridged_sensor *sensor, const char *name,
		       void (*describe)(int))
{
	sensor->input = create_input(name, describe);
	if (sensor->input < 0)
		return -1;
	sensor->data = open(sensor->device, O_RDONLY | O_NONBLOCK);
	if (sensor->data < 0) {
		perror(sensor->device);
		return -1;
	}
	return 0;
}

int main(void)
{
	struct sigaction stop = { .sa_handler = handle_stop };
	struct bridged_sensor accelerometer = {
		.device = "/dev/m_acc_misc",
		.control = "/sys/class/sensor/m_acc_misc/",
		.prefix = "acc",
		.handle = 0,
	};
	struct bridged_sensor proximity = {
		.device = "/dev/m_ps_misc",
		.control = "/sys/class/sensor/m_ps_misc/",
		.prefix = "ps",
		.handle = 7,
	};

	sigaction(SIGTERM, &stop, NULL);
	sigaction(SIGINT, &stop, NULL);

	if (open_sensor(&accelerometer, "MT6768 accelerometer (sensor hub)",
			describe_accelerometer) < 0 ||
	    open_sensor(&proximity, "MT6768 proximity (sensor hub)",
			describe_proximity) < 0)
		return 1;

	while (!stopping) {
		struct pollfd waiting[2] = {
			{ .fd = accelerometer.data, .events = POLLIN },
			{ .fd = proximity.data, .events = POLLIN },
		};

		set_sensor(&accelerometer, display_is_on());
		set_sensor(&proximity, proximity_wanted());
		// wake at least once a second to follow the display and lock state
		if (poll(waiting, 2, 1000) <= 0)
			continue;
		if (waiting[0].revents & POLLIN)
			forward(&accelerometer, forward_accelerometer);
		if (waiting[1].revents & POLLIN)
			forward(&proximity, forward_proximity);
	}

	set_sensor(&accelerometer, 0);
	set_sensor(&proximity, 0);
	ioctl(accelerometer.input, UI_DEV_DESTROY);
	ioctl(proximity.input, UI_DEV_DESTROY);
	return 0;
}
