// SPDX-License-Identifier: GPL-2.0-only
/*
 * Narrow Android property compatibility for the Xiaomi 14 Pro audio stack.
 *
 * nwrt deliberately does not run Android init/property_service.  The stock
 * audio binaries nevertheless use libcutils property_get() to choose their
 * board configuration and feature defaults.  Serve only values captured from
 * the matching OS3.0.308.0 shennong build.  Unknown keys remain unknown so the
 * unmodified caller's default is used.
 *
 * property_set() is process-local: it preserves the Audio HAL's own runtime
 * state without pretending that a system-wide Android property service exists.
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define PROPERTY_FILE "/vendor/etc/xm14pro-audio.properties"
#define PROP_NAME_MAX  92
#define PROP_VALUE_MAX 92
#define FILE_BUFFER_SIZE 32768
#define MUTABLE_PROPERTY_COUNT 64

struct mutable_property {
	char name[PROP_NAME_MAX];
	char value[PROP_VALUE_MAX];
};

static struct mutable_property mutable_properties[MUTABLE_PROPERTY_COUNT];
static volatile int mutable_lock;

static void lock_mutable(void)
{
	while (__sync_lock_test_and_set(&mutable_lock, 1))
		;
}

static void unlock_mutable(void)
{
	__sync_lock_release(&mutable_lock);
}

static size_t copy_string(char *destination, const char *source, size_t size)
{
	size_t length = source ? strlen(source) : 0;
	size_t copied = length;

	if (size == 0)
		return length;
	if (copied >= size)
		copied = size - 1;
	if (copied)
		memcpy(destination, source, copied);
	destination[copied] = '\0';
	return length;
}

static int get_mutable_property(const char *key, char *value)
{
	int found = 0;
	size_t index;

	lock_mutable();
	for (index = 0; index < MUTABLE_PROPERTY_COUNT; index++) {
		if (!mutable_properties[index].name[0])
			continue;
		if (strcmp(mutable_properties[index].name, key) == 0) {
			copy_string(value, mutable_properties[index].value,
				    PROP_VALUE_MAX);
			found = 1;
			break;
		}
	}
	unlock_mutable();
	return found;
}

static int get_captured_property(const char *key, char *value)
{
	char buffer[FILE_BUFFER_SIZE + 1];
	char *cursor;
	char *end;
	ssize_t length;
	size_t key_length = strlen(key);
	int fd;

	fd = open(PROPERTY_FILE, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	length = read(fd, buffer, FILE_BUFFER_SIZE);
	close(fd);
	if (length <= 0)
		return 0;
	buffer[length] = '\0';
	cursor = buffer;
	end = buffer + length;

	while (cursor < end) {
		char *line_end = memchr(cursor, '\n', (size_t)(end - cursor));
		char *separator;
		size_t line_length;

		if (!line_end)
			line_end = end;
		line_length = (size_t)(line_end - cursor);
		if (line_length && cursor[line_length - 1] == '\r')
			line_length--;
		if (line_length && cursor[0] != '#') {
			separator = memchr(cursor, '=', line_length);
			if (separator && (size_t)(separator - cursor) == key_length &&
			    memcmp(cursor, key, key_length) == 0) {
				size_t value_length = line_length - key_length - 1;

				if (value_length >= PROP_VALUE_MAX)
					value_length = PROP_VALUE_MAX - 1;
				memcpy(value, separator + 1, value_length);
				value[value_length] = '\0';
				return 1;
			}
		}
		cursor = line_end < end ? line_end + 1 : end;
	}
	return 0;
}

int property_get(const char *key, char *value, const char *default_value)
{
	if (!key || !value)
		return 0;
	if (!get_mutable_property(key, value) &&
	    !get_captured_property(key, value))
		copy_string(value, default_value ? default_value : "",
			    PROP_VALUE_MAX);
	return (int)strlen(value);
}

int __system_property_get(const char *key, char *value)
{
	if (!key || !value)
		return 0;
	if (!get_mutable_property(key, value) &&
	    !get_captured_property(key, value)) {
		value[0] = '\0';
		return 0;
	}
	return (int)strlen(value);
}

static bool parse_bool(const char *value, bool fallback)
{
	if (!value || !value[0])
		return fallback;
	if (!strcasecmp(value, "1") || !strcasecmp(value, "y") ||
	    !strcasecmp(value, "yes") || !strcasecmp(value, "on") ||
	    !strcasecmp(value, "true"))
		return true;
	if (!strcasecmp(value, "0") || !strcasecmp(value, "n") ||
	    !strcasecmp(value, "no") || !strcasecmp(value, "off") ||
	    !strcasecmp(value, "false"))
		return false;
	return fallback;
}

bool property_get_bool(const char *key, bool default_value)
{
	char value[PROP_VALUE_MAX];

	if (!property_get(key, value, ""))
		return default_value;
	return parse_bool(value, default_value);
}

int32_t property_get_int32(const char *key, int32_t default_value)
{
	char value[PROP_VALUE_MAX];
	char *tail;
	long parsed;

	if (!property_get(key, value, ""))
		return default_value;
	parsed = strtol(value, &tail, 0);
	return tail != value && *tail == '\0' ? (int32_t)parsed : default_value;
}

int64_t property_get_int64(const char *key, int64_t default_value)
{
	char value[PROP_VALUE_MAX];
	char *tail;
	long long parsed;

	if (!property_get(key, value, ""))
		return default_value;
	parsed = strtoll(value, &tail, 0);
	return tail != value && *tail == '\0' ? (int64_t)parsed : default_value;
}

int property_set(const char *key, const char *value)
{
	size_t index;
	size_t empty = MUTABLE_PROPERTY_COUNT;

	if (!key || !key[0] || !value || strlen(key) >= PROP_NAME_MAX ||
	    strlen(value) >= PROP_VALUE_MAX)
		return -1;
	lock_mutable();
	for (index = 0; index < MUTABLE_PROPERTY_COUNT; index++) {
		if (!mutable_properties[index].name[0]) {
			if (empty == MUTABLE_PROPERTY_COUNT)
				empty = index;
			continue;
		}
		if (strcmp(mutable_properties[index].name, key) == 0)
			break;
	}
	if (index == MUTABLE_PROPERTY_COUNT)
		index = empty;
	if (index == MUTABLE_PROPERTY_COUNT) {
		unlock_mutable();
		return -1;
	}
	copy_string(mutable_properties[index].name, key, PROP_NAME_MAX);
	copy_string(mutable_properties[index].value, value, PROP_VALUE_MAX);
	unlock_mutable();
	return 0;
}
