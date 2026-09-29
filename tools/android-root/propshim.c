/*
 * LD_PRELOAD shim for stock Android vendor daemons running without Android's
 * init: serves system properties from /vendor/etc/lagos.prop (key=value per
 * line) and keeps property_set() values in memory. Logs every set to stderr.
 * Built against bionic's libc.so, no NDK headers.
 */
typedef unsigned long size_t;
typedef unsigned int uint32_t;
typedef struct FILE FILE;

FILE *fopen(const char *path, const char *mode);
char *fgets(char *buffer, int size, FILE *stream);
int fclose(FILE *stream);
int dprintf(int fd, const char *format, ...);
size_t strlen(const char *string);
int strcmp(const char *left, const char *right);
char *strchr(const char *string, int character);

#define PROP_NAME_MAX 256
#define PROP_VALUE_MAX 92
#define MAX_PROPERTIES 4096
#define PROPERTY_FILE "/vendor/etc/lagos.prop"

/* Same layout as bionic's prop_info: callers may hand our pointers to
 * libc's __system_property_serial(), which spins while bit 0 is set. */
struct property {
	volatile uint32_t serial; /* value length << 24 | change counter << 1 */
	char value[PROP_VALUE_MAX];
	char name[PROP_NAME_MAX];
};

static struct property properties[MAX_PROPERTIES];
static int property_count;

static void copy_string(char *destination, const char *source, size_t size)
{
	size_t index = 0;
	for (; index + 1 < size && source[index]; index++)
		destination[index] = source[index];
	destination[index] = '\0';
}

static struct property *lookup(const char *name)
{
	for (int index = 0; index < property_count; index++)
		if (strcmp(properties[index].name, name) == 0)
			return &properties[index];
	return 0;
}

static struct property *store(const char *name, const char *value)
{
	struct property *property = lookup(name);
	if (!property) {
		if (property_count == MAX_PROPERTIES)
			return 0;
		property = &properties[property_count++];
		copy_string(property->name, name, PROP_NAME_MAX);
	}
	copy_string(property->value, value, PROP_VALUE_MAX);
	property->serial = (uint32_t)strlen(property->value) << 24 |
		((property->serial + 2) & 0xfffffe);
	return property;
}

__attribute__((constructor)) static void load_properties(void)
{
	char line[PROP_NAME_MAX + PROP_VALUE_MAX + 2];
	FILE *file = fopen(PROPERTY_FILE, "r");
	if (!file)
		return;
	while (fgets(line, sizeof(line), file)) {
		char *newline = strchr(line, '\n');
		if (newline)
			*newline = '\0';
		char *separator = strchr(line, '=');
		if (line[0] == '#' || !separator)
			continue;
		*separator = '\0';
		store(line, separator + 1);
	}
	fclose(file);
}

int __system_property_get(const char *name, char *value)
{
	struct property *property = lookup(name);
	copy_string(value, property ? property->value : "", PROP_VALUE_MAX);
	return (int)strlen(value);
}

const void *__system_property_find(const char *name)
{
	return lookup(name);
}

void __system_property_read_callback(const void *info,
	void (*callback)(void *cookie, const char *name, const char *value, uint32_t serial),
	void *cookie)
{
	const struct property *property = info;
	callback(cookie, property->name, property->value, 0);
}

int __system_property_read(const void *info, char *name, char *value)
{
	const struct property *property = info;
	if (name)
		copy_string(name, property->name, PROP_NAME_MAX);
	copy_string(value, property->value, PROP_VALUE_MAX);
	return (int)strlen(value);
}

uint32_t __system_property_serial(const void *info)
{
	return ((const struct property *)info)->serial;
}

uint32_t __system_property_area_serial(void)
{
	return 0;
}

int __system_property_set(const char *name, const char *value)
{
	dprintf(2, "[propshim] set %s=%s\n", name, value);
	return store(name, value) ? 0 : -1;
}

int property_get(const char *name, char *value, const char *default_value)
{
	struct property *property = lookup(name);
	if (property && property->value[0])
		copy_string(value, property->value, PROP_VALUE_MAX);
	else
		copy_string(value, default_value ? default_value : "", PROP_VALUE_MAX);
	return (int)strlen(value);
}

int property_set(const char *name, const char *value)
{
	return __system_property_set(name, value);
}

/* No SELinux policy is loaded on the host, so libselinux cannot map peer
 * contexts to SIDs and servicemanager would reject every addService(). */
int selinux_check_access(const char *source, const char *target, const char *class,
	const char *permission, void *audit_data)
{
	(void)source, (void)target, (void)class, (void)permission, (void)audit_data;
	return 0;
}
