// SPDX-License-Identifier: GPL-2.0-only
#include <fcntl.h>
#include <linux/android/binderfs.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	struct binderfs_device device = { 0 };
	int fd;

	if (argc != 3 || strlen(argv[2]) >= sizeof(device.name))
		return 2;
	fd = open(argv[1], O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		perror("open binder-control");
		return 3;
	}
	strcpy(device.name, argv[2]);
	if (ioctl(fd, BINDER_CTL_ADD, &device) < 0) {
		perror("BINDER_CTL_ADD");
		close(fd);
		return 4;
	}
	close(fd);
	printf("%s %u:%u\n", device.name, device.major, device.minor);
	return 0;
}
