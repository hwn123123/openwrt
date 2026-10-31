// SPDX-License-Identifier: GPL-2.0-only
// Load a binary SELinux policy with exactly one write(2).
// selinuxfs rejects a policy split across multiple writes; BusyBox dd with a
// small block size therefore cannot be used for /sys/fs/selinux/load.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail(const char *what)
{
	fprintf(stderr, "selinux-load: %s: %s\n", what, strerror(errno));
	return 1;
}

int main(int argc, char **argv)
{
	const char *load_path = "/sys/fs/selinux/load";
	struct stat st;
	unsigned char *policy;
	ssize_t got;
	size_t offset = 0;
	int input;
	int output;

	if (argc != 2 && argc != 3) {
		fprintf(stderr, "usage: %s POLICY [SELINUX_LOAD]\n", argv[0]);
		return 2;
	}
	if (argc == 3)
		load_path = argv[2];

	input = open(argv[1], O_RDONLY | O_CLOEXEC);
	if (input < 0)
		return fail("open policy");
	if (fstat(input, &st) < 0) {
		close(input);
		return fail("stat policy");
	}
	if (st.st_size <= 0 || st.st_size > 16 * 1024 * 1024) {
		fprintf(stderr, "selinux-load: invalid policy size: %lld\n",
			(long long)st.st_size);
		close(input);
		return 1;
	}

	policy = malloc((size_t)st.st_size);
	if (!policy) {
		close(input);
		return fail("allocate policy buffer");
	}
	while (offset < (size_t)st.st_size) {
		got = read(input, policy + offset, (size_t)st.st_size - offset);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0) {
			free(policy);
			close(input);
			if (got == 0)
				fprintf(stderr, "selinux-load: unexpected end of policy\n");
			else
				fprintf(stderr, "selinux-load: read policy: %s\n", strerror(errno));
			return 1;
		}
		offset += (size_t)got;
	}
	close(input);

	output = open(load_path, O_WRONLY | O_CLOEXEC);
	if (output < 0) {
		free(policy);
		return fail("open selinuxfs load node");
	}
	/* This must remain one syscall.  Never retry a short write. */
	got = write(output, policy, (size_t)st.st_size);
	if (got != st.st_size) {
		int saved = errno;
		free(policy);
		close(output);
		errno = saved ? saved : EIO;
		return fail("load policy");
	}

	free(policy);
	close(output);
	return 0;
}
