// SPDX-License-Identifier: GPL-2.0-only
/*
 * Read the effective rmnet V2 endpoint configuration from Xiaomi 14 Pro's
 * stock nicmd process.  This is a read-only reverse-engineering aid for the
 * exact OS3.0.308.0 libnicm_internal.so shipped with the phone.
 */

#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define NICM_INSTANCE_SYMBOL_OFFSET 0x0a4688UL
#define EGRESS_COUNT_OFFSET         348UL
#define EGRESS_ARRAY_OFFSET         352UL
#define INGRESS_COUNT_OFFSET        448UL
#define INGRESS_ARRAY_OFFSET        452UL

struct rmnet_egress_param {
    uint32_t egress_ep_type;
    uint32_t pipe_setup_status;
    uint32_t cs_offload_en;
    uint32_t aggr_en;
    uint32_t ulso_en;
    uint32_t ipid_min_max_idx;
    uint32_t int_modt;
    uint32_t int_modc;
};

struct rmnet_ingress_param {
    uint32_t ingress_ep_type;
    uint32_t pipe_setup_status;
    uint32_t cs_offload_en;
    uint32_t buff_size;
    uint32_t agg_byte_limit;
    uint32_t agg_time_limit;
    uint32_t agg_pkt_limit;
    uint32_t int_modt;
    uint32_t int_modc;
    uint32_t padding;
};

static pid_t
find_process (const char *wanted)
{
    DIR           *dir;
    struct dirent *entry;
    pid_t          found = -1;

    dir = opendir ("/proc");
    if (!dir)
        return -1;

    while ((entry = readdir (dir))) {
        char path[128];
        char comm[128];
        char *end = NULL;
        long candidate;
        FILE *fp;
        size_t len;

        if (!isdigit ((unsigned char)entry->d_name[0]))
            continue;
        candidate = strtol (entry->d_name, &end, 10);
        if (!end || *end || candidate <= 0)
            continue;
        snprintf (path, sizeof (path), "/proc/%ld/cmdline", candidate);
        fp = fopen (path, "r");
        if (!fp)
            continue;
        if (!fgets (comm, sizeof (comm), fp)) {
            fclose (fp);
            continue;
        }
        fclose (fp);
        len = strlen (comm);
        if (len && comm[len - 1] == '\n')
            comm[len - 1] = '\0';
        if (!strcmp (comm, wanted) ||
            (len > strlen (wanted) &&
             !strcmp (comm + len - strlen (wanted), wanted))) {
            found = (pid_t)candidate;
            break;
        }
    }
    closedir (dir);
    return found;
}

static uintptr_t
find_library_base (pid_t pid, const char *needle)
{
    char      path[128];
    char      line[1024];
    FILE     *fp;
    uintptr_t base = 0;

    snprintf (path, sizeof (path), "/proc/%ld/maps", (long)pid);
    fp = fopen (path, "r");
    if (!fp)
        return 0;

    while (fgets (line, sizeof (line), fp)) {
        unsigned long start, end, offset;
        char perms[5];

        if (!strstr (line, needle))
            continue;
        if (sscanf (line, "%lx-%lx %4s %lx", &start, &end, perms, &offset) != 4)
            continue;
        if (offset == 0) {
            base = (uintptr_t)start;
            break;
        }
    }
    fclose (fp);
    return base;
}

static int
read_exact (int fd, uintptr_t address, void *buffer, size_t size)
{
    ssize_t got;

    got = pread (fd, buffer, size, (off_t)address);
    if (got != (ssize_t)size) {
        if (got >= 0)
            errno = EIO;
        return -1;
    }
    return 0;
}

int
main (int argc, char **argv)
{
    const char *process = argc > 1 ? argv[1] : "nicmd";
    pid_t pid;
    uintptr_t library_base;
    uintptr_t instance = 0;
    uintptr_t tagged_instance;
    char mem_path[128];
    int mem_fd;
    uint32_t egress_count = 0;
    uint32_t ingress_count = 0;
    struct rmnet_egress_param egress[3];
    struct rmnet_ingress_param ingress[4];
    uint32_t i;

    pid = find_process (process);
    if (pid < 0) {
        fprintf (stderr, "process '%s' not found\n", process);
        return 2;
    }

    library_base = find_library_base (pid, "libnicm_internal.so");
    if (!library_base) {
        fprintf (stderr, "libnicm_internal.so mapping not found in pid %ld\n", (long)pid);
        return 3;
    }

    snprintf (mem_path, sizeof (mem_path), "/proc/%ld/mem", (long)pid);
    mem_fd = open (mem_path, O_RDONLY | O_CLOEXEC);
    if (mem_fd < 0) {
        fprintf (stderr, "cannot open %s: %s\n", mem_path, strerror (errno));
        return 4;
    }

    if (read_exact (mem_fd,
                    library_base + NICM_INSTANCE_SYMBOL_OFFSET,
                    &instance,
                    sizeof (instance)) < 0) {
        fprintf (stderr, "cannot read ConfigurationManager::mInstance: %s\n",
                 strerror (errno));
        close (mem_fd);
        return 5;
    }
    if (instance < 0x10000) {
        fprintf (stderr, "ConfigurationManager instance is not initialized\n");
        close (mem_fd);
        return 6;
    }

    /* Android's heap tagging uses the top byte on arm64. /proc/PID/mem
     * expects the canonical, untagged virtual address. */
    tagged_instance = instance;
    instance &= UINT64_C(0x00ffffffffffffff);
    printf ("pid=%ld library_base=0x%" PRIxPTR
            " tagged_instance=0x%" PRIxPTR " instance=0x%" PRIxPTR "\n",
            (long)pid, library_base, tagged_instance, instance);

    if (read_exact (mem_fd, instance + EGRESS_COUNT_OFFSET,
                    &egress_count, sizeof (egress_count)) < 0 ||
        read_exact (mem_fd, instance + INGRESS_COUNT_OFFSET,
                    &ingress_count, sizeof (ingress_count)) < 0) {
        fprintf (stderr, "cannot read endpoint counts: %s\n", strerror (errno));
        close (mem_fd);
        return 7;
    }
    if (egress_count > 3 || ingress_count > 4) {
        fprintf (stderr, "unexpected endpoint counts: egress=%u ingress=%u\n",
                 egress_count, ingress_count);
        close (mem_fd);
        return 8;
    }

    memset (egress, 0, sizeof (egress));
    memset (ingress, 0, sizeof (ingress));
    if (read_exact (mem_fd, instance + EGRESS_ARRAY_OFFSET,
                    egress, egress_count * sizeof (egress[0])) < 0 ||
        read_exact (mem_fd, instance + INGRESS_ARRAY_OFFSET,
                    ingress, ingress_count * sizeof (ingress[0])) < 0) {
        fprintf (stderr, "cannot read endpoint arrays: %s\n", strerror (errno));
        close (mem_fd);
        return 9;
    }
    close (mem_fd);

    printf ("egress_count=%u\n", egress_count);
    for (i = 0; i < egress_count; i++) {
        printf ("egress[%u]: type=%u status=%u checksum=%u aggregation=%u "
                "ulso=%u ipid_index=%u int_modt=%u int_modc=%u\n",
                i,
                egress[i].egress_ep_type,
                egress[i].pipe_setup_status,
                egress[i].cs_offload_en,
                egress[i].aggr_en,
                egress[i].ulso_en,
                egress[i].ipid_min_max_idx,
                egress[i].int_modt,
                egress[i].int_modc);
    }
    printf ("ingress_count=%u\n", ingress_count);
    for (i = 0; i < ingress_count; i++) {
        printf ("ingress[%u]: type=%u status=%u checksum=%u buffer=%u "
                "agg_bytes=%u agg_time=%u agg_packets=%u int_modt=%u "
                "int_modc=%u padding=%u\n",
                i,
                ingress[i].ingress_ep_type,
                ingress[i].pipe_setup_status,
                ingress[i].cs_offload_en,
                ingress[i].buff_size,
                ingress[i].agg_byte_limit,
                ingress[i].agg_time_limit,
                ingress[i].agg_pkt_limit,
                ingress[i].int_modt,
                ingress[i].int_modc,
                ingress[i].padding);
    }
    return 0;
}
