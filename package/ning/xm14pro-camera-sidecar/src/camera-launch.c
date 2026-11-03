#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 5) return 2;
    uid_t uid = (uid_t)strtoul(argv[2], NULL, 10);
    gid_t gid = (gid_t)strtoul(argv[3], NULL, 10);
    if (chroot(argv[1]) || chdir("/") || setgid(gid) || setuid(uid)) {
        perror("camera-launch setup");
        return 3;
    }
    execv(argv[4], argv + 4);
    perror("camera-launch exec");
    return 4;
}
