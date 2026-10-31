// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define REAL_IPTABLES "/usr/sbin/xtables-legacy-multi"

static bool called_by_dockerd(void) {
    char path[64];
    char command[64] = "";
    snprintf(path, sizeof(path), "/proc/%ld/comm", (long)getppid());
    FILE *file = fopen(path, "re");
    if (!file) return false;
    bool matched = fgets(command, sizeof(command), file) != NULL &&
                   strncmp(command, "dockerd", 7) == 0;
    fclose(file);
    return matched;
}

int main(int argc, char **argv) {
    argv[0] = (char *)"iptables";
    if (!called_by_dockerd()) {
        execv(REAL_IPTABLES, argv);
        fprintf(stderr, "iptables: exec %s failed: %s\n", REAL_IPTABLES,
                strerror(errno));
        return 127;
    }

    char **filtered = calloc((size_t)argc + 1, sizeof(*filtered));
    if (!filtered) return 125;
    int output = 0;
    filtered[output++] = (char *)"iptables";
    for (int input = 1; input < argc; ++input) {
        if (input + 1 < argc && strcmp(argv[input], "-m") == 0 &&
            strcmp(argv[input + 1], "addrtype") == 0) {
            ++input;
            continue;
        }
        if (input + 1 < argc &&
            (strcmp(argv[input], "--dst-type") == 0 ||
             strcmp(argv[input], "--src-type") == 0) &&
            strcmp(argv[input + 1], "LOCAL") == 0) {
            ++input;
            continue;
        }
        filtered[output++] = argv[input];
    }
    filtered[output] = NULL;
    execv(REAL_IPTABLES, filtered);
    fprintf(stderr, "iptables: exec %s failed: %s\n", REAL_IPTABLES,
            strerror(errno));
    free(filtered);
    return 127;
}
