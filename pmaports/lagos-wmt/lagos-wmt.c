// SPDX-License-Identifier: MIT
// Minimal stand-in for MediaTek's Android-only wmt_loader and wmt_launcher,
// for the connectivity subsystem (connsys) built into the MT6768 SoC.
//
//   lagos-wmt init
//       What wmt_loader does: tell wmt_drv the SoC chip ID and have it
//       initialise the WMT core, which creates /dev/stpwmt.
//   lagos-wmt launch [-p firmware-dir] [-x patch-prefix]
//       What wmt_launcher does: stay resident and answer the kernel's firmware
//       requests. On every connsys power-on the WMT core asks userspace
//       ("srh_patch", "srh_rom_patch") which patch files to download and
//       where; the answer comes from each file's header.
//   lagos-wmt wifi-on nvram-file
//       What the Android WiFi HAL does: hand the driver this phone's WiFi
//       NVRAM (MAC address, RF calibration; nvdata's APCFG/APRDEB/WIFI), then
//       switch on station mode, which creates wlan0. The driver only registers
//       with WMT once it has the NVRAM, and the NVRAM must arrive while WiFi
//       is still off.
//
// Protocol from the vendor driver source (connectivity-common, wmt_dev.c);
// the header fields were confirmed against the stock wmt_launcher.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define WMT_DETECT_IOC_MAGIC 'w'
#define COMBO_IOCTL_SET_CHIP_ID _IOW(WMT_DETECT_IOC_MAGIC, 1, int)
#define COMBO_IOCTL_GET_SOC_CHIP_ID _IOR(WMT_DETECT_IOC_MAGIC, 3, int)
#define COMBO_IOCTL_DO_MODULE_INIT _IOR(WMT_DETECT_IOC_MAGIC, 4, int)
#define COMBO_IOCTL_MODULE_CLEANUP _IOR(WMT_DETECT_IOC_MAGIC, 5, int)
#define COMBO_IOCTL_CONNSYS_SOC_HW_INIT _IOR(WMT_DETECT_IOC_MAGIC, 10, int)

#define WMT_IOC_MAGIC 0xa0
#define WMT_IOCTL_SET_STP_MODE _IOW(WMT_IOC_MAGIC, 5, int)
#define WMT_IOCTL_SET_PATCH_NUM _IOW(WMT_IOC_MAGIC, 14, int)
#define WMT_IOCTL_SET_PATCH_INFO _IOW(WMT_IOC_MAGIC, 15, char *)
#define WMT_IOCTL_SET_ROM_PATCH_INFO _IOW(WMT_IOC_MAGIC, 31, char *)

// Host interface: (UART baud << 8) | (FM mode << 4) | STP mode. The SoC
// connsys talks over BTIF (STP mode 3), with FM over the common interface (2)
#define STP_MODE_BTIF_FM_COMMON 0x23

#define PATCH_NAME_MAX 256
// Patch header: build time[16], platform[4], hw version[2], sw version[2],
// then the patch info the launcher hands to the kernel
#define PATCH_INFO_OFFSET 24

struct wmt_patch_info {
	uint32_t download_sequence;
	uint8_t address[4];
	char name[PATCH_NAME_MAX];
};

struct wmt_rom_patch_info {
	uint32_t type;
	uint8_t address[4];
	char name[PATCH_NAME_MAX];
};

static const char *firmware_directory = "/usr/lib/firmware";
static const char *patch_prefix = "soc1_0";

// Device nodes come from udev after the driver registers them: wait for them
static int open_device(const char *path, int flags)
{
	int fd = -1;
	for (int attempt = 0; attempt < 50; attempt++) {
		fd = open(path, flags);
		if (fd >= 0 || errno != ENOENT)
			break;
		usleep(100000);
	}
	return fd;
}

static int detect_init(void)
{
	int detect_fd = open_device("/dev/wmtdetect", O_RDWR);
	if (detect_fd < 0) {
		perror("open /dev/wmtdetect");
		return 1;
	}

	// Probes the connsys platform device; the chip ID can't be read before.
	// EEXIST: already done by an earlier run, carry on
	if (ioctl(detect_fd, COMBO_IOCTL_CONNSYS_SOC_HW_INIT, 0) < 0 && errno != EEXIST) {
		perror("COMBO_IOCTL_CONNSYS_SOC_HW_INIT");
		close(detect_fd);
		return 1;
	}

	int chip_id = 0;
	for (int attempt = 0; attempt < 20 && chip_id <= 0; attempt++) {
		chip_id = ioctl(detect_fd, COMBO_IOCTL_GET_SOC_CHIP_ID, 0);
		if (chip_id <= 0)
			usleep(100000);
	}
	if (chip_id <= 0) {
		fprintf(stderr, "no SoC connsys chip id (%d)\n", chip_id);
		close(detect_fd);
		return 1;
	}
	printf("connsys chip id 0x%04x\n", chip_id);

	ioctl(detect_fd, COMBO_IOCTL_SET_CHIP_ID, chip_id);
	// There's no external SDIO combo chip: drop its detect driver first
	ioctl(detect_fd, COMBO_IOCTL_MODULE_CLEANUP, chip_id);
	int result = ioctl(detect_fd, COMBO_IOCTL_DO_MODULE_INIT, chip_id);
	close(detect_fd);
	if (result < 0) {
		perror("COMBO_IOCTL_DO_MODULE_INIT");
		return 1;
	}
	return 0;
}

static int read_patch_info(const char *file_name, uint8_t patch_info[8])
{
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", firmware_directory, file_name);

	FILE *patch_file = fopen(path, "rb");
	if (!patch_file) {
		fprintf(stderr, "open %s: %s\n", path, strerror(errno));
		return -1;
	}
	int ok = fseek(patch_file, PATCH_INFO_OFFSET, SEEK_SET) == 0 &&
		 fread(patch_info, 1, 8, patch_file) == 8;
	fclose(patch_file);
	return ok ? 0 : -1;
}

// Firmware files named "<prefix>_<kind>_*_hdr.bin", sorted for a stable order
static int find_patches(const char *kind, char names[][PATCH_NAME_MAX], int max_count)
{
	char pattern[64];
	snprintf(pattern, sizeof(pattern), "%s_%s_", patch_prefix, kind);

	DIR *directory = opendir(firmware_directory);
	if (!directory)
		return 0;

	int count = 0;
	struct dirent *entry;
	while ((entry = readdir(directory)) && count < max_count) {
		size_t length = strlen(entry->d_name);
		if (strncmp(entry->d_name, pattern, strlen(pattern)) != 0 ||
		    length < 8 || strcmp(entry->d_name + length - 8, "_hdr.bin") != 0)
			continue;
		snprintf(names[count++], PATCH_NAME_MAX, "%s", entry->d_name);
	}
	closedir(directory);
	qsort(names, count, PATCH_NAME_MAX, (int (*)(const void *, const void *))strcmp);
	return count;
}

// "srh_patch": the MCU patch(es). Info byte 0 packs the patch count (high
// nibble) and this file's download sequence (low nibble); the load address
// is the other three bytes.
static int answer_patch_search(int wmt_fd)
{
	static int patch_count_sent;
	char names[8][PATCH_NAME_MAX];
	int count = find_patches("patch", names, 8);
	if (count == 0) {
		fprintf(stderr, "no %s_patch_*_hdr.bin in %s\n", patch_prefix, firmware_directory);
		return -1;
	}

	for (int index = 0; index < count; index++) {
		uint8_t patch_info[8];
		if (read_patch_info(names[index], patch_info))
			return -1;

		// The kernel keeps the count for good and rejects a second one
		if (!patch_count_sent) {
			ioctl(wmt_fd, WMT_IOCTL_SET_PATCH_NUM, patch_info[0] >> 4);
			patch_count_sent = 1;
		}

		struct wmt_patch_info info = {
			.download_sequence = patch_info[0] & 0x0f,
			.address = { 0x00, patch_info[1], patch_info[2], patch_info[3] },
		};
		memcpy(info.name, names[index], sizeof(info.name));
		printf("patch %u: %s address %02x%02x%02x%02x\n", info.download_sequence, info.name,
		       info.address[0], info.address[1], info.address[2], info.address[3]);
		if (ioctl(wmt_fd, WMT_IOCTL_SET_PATCH_INFO, &info) < 0)
			perror("WMT_IOCTL_SET_PATCH_INFO");
	}
	return 0;
}

// "srh_rom_patch": the RAM code for each function (MCU, WiFi, BT). Byte 7 of
// the info is the WMT driver type (BT 0, WiFi 3, WMT/MCU 4).
static int answer_rom_patch_search(int wmt_fd)
{
	char names[8][PATCH_NAME_MAX];
	int count = find_patches("ram", names, 8);

	for (int index = 0; index < count; index++) {
		uint8_t patch_info[8];
		if (read_patch_info(names[index], patch_info))
			return -1;

		struct wmt_rom_patch_info info = {
			.type = patch_info[7],
			.address = { 0x00, patch_info[1], patch_info[2], patch_info[3] },
		};
		memcpy(info.name, names[index], sizeof(info.name));
		printf("rom patch type %u: %s address %02x%02x%02x%02x\n", info.type, info.name,
		       info.address[0], info.address[1], info.address[2], info.address[3]);
		if (ioctl(wmt_fd, WMT_IOCTL_SET_ROM_PATCH_INFO, &info) < 0)
			perror("WMT_IOCTL_SET_ROM_PATCH_INFO");
	}
	return 0;
}

// Tell systemd (Type=notify) that WiFi can be switched on now
static void notify_ready(void)
{
	const char *socket_path = getenv("NOTIFY_SOCKET");
	if (!socket_path || (socket_path[0] != '/' && socket_path[0] != '@'))
		return;

	struct sockaddr_un address = { .sun_family = AF_UNIX };
	size_t path_length = strlen(socket_path);
	if (path_length >= sizeof(address.sun_path))
		return;
	memcpy(address.sun_path, socket_path, path_length);
	// Abstract namespace socket
	if (address.sun_path[0] == '@')
		address.sun_path[0] = '\0';

	int notify_fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (notify_fd < 0)
		return;
	sendto(notify_fd, "READY=1", 7, 0, (struct sockaddr *)&address,
	       offsetof(struct sockaddr_un, sun_path) + path_length);
	close(notify_fd);
}

static int launch(void)
{
	int wmt_fd = open_device("/dev/stpwmt", O_RDWR | O_NOCTTY);
	if (wmt_fd < 0) {
		perror("open /dev/stpwmt");
		return 1;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);

	// Without it the WMT core can't bring STP up after a power-on ("no hif info")
	if (ioctl(wmt_fd, WMT_IOCTL_SET_STP_MODE, STP_MODE_BTIF_FM_COMMON) < 0)
		perror("WMT_IOCTL_SET_STP_MODE");

	printf("waiting for WMT requests (firmware %s, prefix %s)\n", firmware_directory, patch_prefix);
	notify_ready();

	for (;;) {
		struct pollfd wmt_poll = { .fd = wmt_fd, .events = POLLIN };
		if (poll(&wmt_poll, 1, -1) < 0) {
			if (errno == EINTR)
				continue;
			perror("poll /dev/stpwmt");
			return 1;
		}
		if (!(wmt_poll.revents & POLLIN))
			continue;

		char command[PATCH_NAME_MAX] = { 0 };
		ssize_t length = read(wmt_fd, command, sizeof(command) - 1);
		if (length <= 0)
			continue;
		printf("request: %s\n", command);

		int result = 0;
		if (strcmp(command, "srh_patch") == 0)
			result = answer_patch_search(wmt_fd);
		else if (strcmp(command, "srh_rom_patch") == 0)
			result = answer_rom_patch_search(wmt_fd);

		// Anything else (e.g. patch version updates for Android
		// properties) needs nothing from us: acknowledge it
		const char *response = result == 0 ? "ok" : "fail";
		if (write(wmt_fd, response, strlen(response)) < 0)
			perror("write /dev/stpwmt");
	}
}

static int wifi_on(const char *nvram_path)
{
	static const char nvram_prefix[] = "WR-BUF:NVRAM";
	// The driver's NVRAM buffer is 2 KiB; leave room for growth
	char buffer[sizeof(nvram_prefix) - 1 + 8192];
	memcpy(buffer, nvram_prefix, sizeof(nvram_prefix) - 1);

	FILE *nvram_file = fopen(nvram_path, "rb");
	if (!nvram_file) {
		fprintf(stderr, "open %s: %s\n", nvram_path, strerror(errno));
		return 1;
	}
	size_t nvram_length = fread(buffer + sizeof(nvram_prefix) - 1, 1,
				    sizeof(buffer) - (sizeof(nvram_prefix) - 1), nvram_file);
	fclose(nvram_file);
	if (nvram_length == 0) {
		fprintf(stderr, "%s is empty\n", nvram_path);
		return 1;
	}

	int wifi_fd = open_device("/dev/wmtWifi", O_WRONLY);
	if (wifi_fd < 0) {
		perror("open /dev/wmtWifi");
		return 1;
	}
	// One write: the driver treats everything after the prefix as the NVRAM
	size_t total_length = sizeof(nvram_prefix) - 1 + nvram_length;
	if (write(wifi_fd, buffer, total_length) != (ssize_t)total_length) {
		perror("write NVRAM to /dev/wmtWifi");
		close(wifi_fd);
		return 1;
	}
	// 'S': power WiFi on in station mode
	if (write(wifi_fd, "S", 1) != 1) {
		perror("write S to /dev/wmtWifi");
		close(wifi_fd);
		return 1;
	}
	close(wifi_fd);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc >= 3 && strcmp(argv[1], "wifi-on") == 0)
		return wifi_on(argv[2]);

	if (argc >= 2 && strcmp(argv[1], "init") == 0)
		return detect_init();

	if (argc >= 2 && strcmp(argv[1], "launch") == 0) {
		int option;
		optind = 2;
		while ((option = getopt(argc, argv, "p:x:")) != -1) {
			if (option == 'p')
				firmware_directory = optarg;
			else if (option == 'x')
				patch_prefix = optarg;
			else
				return 2;
		}
		return launch();
	}

	fprintf(stderr, "usage: %s init | launch [-p firmware-dir] [-x patch-prefix] | wifi-on nvram-file\n",
		argv[0]);
	return 2;
}
