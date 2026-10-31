// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/rtc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define EARLIEST_VALID_MS INT64_C(1577836800000) /* 2020-01-01 */
#define LATEST_VALID_MS   INT64_C(4102444800000) /* 2100-01-01 */

static int read_rtc_ms(int64_t *value)
{
	struct rtc_time rtc = { 0 };
	struct tm tm = { 0 };
	time_t seconds;
	int fd;

	fd = open("/dev/rtc0", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	if (ioctl(fd, RTC_RD_TIME, &rtc) < 0) {
		close(fd);
		return -1;
	}
	close(fd);

	tm.tm_sec = rtc.tm_sec;
	tm.tm_min = rtc.tm_min;
	tm.tm_hour = rtc.tm_hour;
	tm.tm_mday = rtc.tm_mday;
	tm.tm_mon = rtc.tm_mon;
	tm.tm_year = rtc.tm_year;
	tm.tm_isdst = 0;
	seconds = timegm(&tm);
	if (seconds < 0)
		return -1;
	*value = (int64_t)seconds * 1000;
	return 0;
}

static int read_le64(const char *path, int64_t *value)
{
	unsigned char data[8];
	uint64_t result = 0;
	ssize_t count;
	int fd;
	int i;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	count = read(fd, data, sizeof(data));
	close(fd);
	if (count != (ssize_t)sizeof(data))
		return -1;
	for (i = 7; i >= 0; --i)
		result = (result << 8) | data[i];
	*value = (int64_t)result;
	return 0;
}

static int read_decimal(const char *path, int64_t *value)
{
	char buffer[64];
	char *end;
	ssize_t count;
	long long parsed;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	count = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	if (count <= 0)
		return -1;
	buffer[count] = '\0';
	errno = 0;
	parsed = strtoll(buffer, &end, 10);
	if (errno || end == buffer)
		return -1;
	*value = (int64_t)parsed;
	return 0;
}

static int valid_offset(int64_t offset, int64_t rtc_ms)
{
	int64_t wall_ms;

	if (__builtin_add_overflow(offset, rtc_ms, &wall_ms))
		return 0;
	return wall_ms >= EARLIEST_VALID_MS && wall_ms < LATEST_VALID_MS;
}

static int restore_time(const char *ats_path, const char *state_path)
{
	struct timespec now;
	struct timespec restored;
	int64_t rtc_ms;
	int64_t ats_offset;
	int64_t saved_offset;
	int64_t selected = INT64_MIN;
	int64_t wall_ms;
	int64_t now_ms;

	if (read_rtc_ms(&rtc_ms) < 0) {
		perror("read /dev/rtc0");
		return 1;
	}
	if (read_le64(ats_path, &ats_offset) == 0 &&
	    valid_offset(ats_offset, rtc_ms))
		selected = ats_offset;
	if (read_decimal(state_path, &saved_offset) == 0 &&
	    valid_offset(saved_offset, rtc_ms) && saved_offset > selected)
		selected = saved_offset;
	if (selected == INT64_MIN) {
		fprintf(stderr, "no valid Qualcomm TOD offset\n");
		return 1;
	}

	wall_ms = selected + rtc_ms;
	if (clock_gettime(CLOCK_REALTIME, &now) < 0) {
		perror("clock_gettime");
		return 1;
	}
	now_ms = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
	if (wall_ms <= now_ms) {
		printf("wall clock already newer than stored TOD base\n");
		return 0;
	}

	restored.tv_sec = wall_ms / 1000;
	restored.tv_nsec = (wall_ms % 1000) * 1000000;
	if (clock_settime(CLOCK_REALTIME, &restored) < 0) {
		perror("clock_settime");
		return 1;
	}
	printf("restored wall clock from Qualcomm TOD base: %" PRId64 "\n",
	       wall_ms);
	return 0;
}

static int save_offset(const char *state_path)
{
	struct timespec now;
	char temp_path[512];
	char buffer[64];
	int64_t rtc_ms;
	int64_t wall_ms;
	int64_t offset;
	int length;
	int fd;

	if (read_rtc_ms(&rtc_ms) < 0 || clock_gettime(CLOCK_REALTIME, &now) < 0)
		return 1;
	wall_ms = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
	if (wall_ms < EARLIEST_VALID_MS || wall_ms >= LATEST_VALID_MS) {
		fprintf(stderr, "refusing to save an invalid wall clock\n");
		return 1;
	}
	offset = wall_ms - rtc_ms;
	if (snprintf(temp_path, sizeof(temp_path), "%s.tmp.%ld", state_path,
		     (long)getpid()) >= (int)sizeof(temp_path))
		return 1;
	length = snprintf(buffer, sizeof(buffer), "%" PRId64 "\n", offset);
	fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0)
		return 1;
	if (write(fd, buffer, (size_t)length) != length || fsync(fd) < 0) {
		close(fd);
		unlink(temp_path);
		return 1;
	}
	if (close(fd) < 0 || rename(temp_path, state_path) < 0) {
		unlink(temp_path);
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 4 && strcmp(argv[1], "restore") == 0)
		return restore_time(argv[2], argv[3]);
	if (argc == 3 && strcmp(argv[1], "save") == 0)
		return save_offset(argv[2]);
	fprintf(stderr, "usage: %s restore ATS_1 STATE | save STATE\n", argv[0]);
	return 2;
}
