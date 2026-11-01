// SPDX-License-Identifier: Apache-2.0
// Minimal, recovery-safe Android sparse image expander.
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define SPARSE_MAGIC 0xed26ff3aU
#define CHUNK_RAW 0xcac1U
#define CHUNK_FILL 0xcac2U
#define CHUNK_DONT_CARE 0xcac3U
#define CHUNK_CRC32 0xcac4U

struct __attribute__((packed)) sparse_header {
    uint32_t magic;
    uint16_t major;
    uint16_t minor;
    uint16_t file_hdr_sz;
    uint16_t chunk_hdr_sz;
    uint32_t blk_sz;
    uint32_t total_blks;
    uint32_t total_chunks;
    uint32_t checksum;
};

struct __attribute__((packed)) chunk_header {
    uint16_t type;
    uint16_t reserved;
    uint32_t chunk_sz;
    uint32_t total_sz;
};

static void die(const char *what) {
    fprintf(stderr, "simg2img-lite: %s: %s\n", what, strerror(errno));
    exit(2);
}

static void fail(const char *what) {
    fprintf(stderr, "simg2img-lite: %s\n", what);
    exit(2);
}

static void read_full(int fd, void *buf, size_t size) {
    uint8_t *p = buf;
    while (size) {
        ssize_t n = read(fd, p, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) die("read");
        p += (size_t)n;
        size -= (size_t)n;
    }
}

static void write_full(int fd, const void *buf, size_t size) {
    const uint8_t *p = buf;
    while (size) {
        ssize_t n = write(fd, p, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) die("write");
        p += (size_t)n;
        size -= (size_t)n;
    }
}

static void skip_input(int fd, uint64_t size) {
    uint8_t buf[4096];
    while (size) {
        size_t now = size > sizeof(buf) ? sizeof(buf) : (size_t)size;
        read_full(fd, buf, now);
        size -= now;
    }
}

static void copy_raw(int in, int out, uint64_t size) {
    uint8_t buf[1024 * 1024];
    while (size) {
        size_t now = size > sizeof(buf) ? sizeof(buf) : (size_t)size;
        read_full(in, buf, now);
        write_full(out, buf, now);
        size -= now;
    }
}

static void write_fill(int out, uint32_t pattern, uint64_t size) {
    uint32_t buf[64 * 1024];
    for (size_t i = 0; i < sizeof(buf) / sizeof(buf[0]); ++i) buf[i] = pattern;
    while (size) {
        size_t now = size > sizeof(buf) ? sizeof(buf) : (size_t)size;
        write_full(out, buf, now);
        size -= now;
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: simg2img-lite SPARSE_IMAGE OUTPUT\n");
        return 2;
    }
    int in = open(argv[1], O_RDONLY | O_CLOEXEC);
    if (in < 0) die("open input");
    int out = open(argv[2], O_WRONLY | O_CLOEXEC);
    if (out < 0) die("open output");

    struct sparse_header h;
    read_full(in, &h, sizeof(h));
    if (h.magic != SPARSE_MAGIC || h.major != 1 || h.file_hdr_sz < sizeof(h) ||
        h.chunk_hdr_sz < sizeof(struct chunk_header) || h.blk_sz == 0) {
        fail("unsupported sparse header");
    }
    skip_input(in, h.file_hdr_sz - sizeof(h));

    uint64_t blocks = 0;
    for (uint32_t i = 0; i < h.total_chunks; ++i) {
        struct chunk_header c;
        read_full(in, &c, sizeof(c));
        skip_input(in, h.chunk_hdr_sz - sizeof(c));
        uint64_t bytes = (uint64_t)c.chunk_sz * h.blk_sz;
        if (c.total_sz < h.chunk_hdr_sz) fail("chunk size underflow");
        uint64_t payload = c.total_sz - h.chunk_hdr_sz;
        if (blocks + c.chunk_sz > h.total_blks) fail("chunk exceeds image geometry");

        switch (c.type) {
        case CHUNK_RAW:
            if (payload != bytes) fail("invalid RAW chunk size");
            copy_raw(in, out, bytes);
            break;
        case CHUNK_FILL: {
            uint32_t pattern;
            if (payload != sizeof(pattern)) fail("invalid FILL chunk size");
            read_full(in, &pattern, sizeof(pattern));
            write_fill(out, pattern, bytes);
            break;
        }
        case CHUNK_DONT_CARE:
            if (payload != 0) fail("invalid DONT_CARE chunk size");
            if (lseek(out, (off_t)bytes, SEEK_CUR) < 0) die("seek output");
            break;
        case CHUNK_CRC32:
            if (payload != 4 || c.chunk_sz != 0) fail("invalid CRC32 chunk");
            skip_input(in, 4);
            break;
        default:
            fail("unknown sparse chunk type");
        }
        blocks += c.chunk_sz;
    }
    if (blocks != h.total_blks) fail("incomplete sparse image");
    struct stat out_stat;
    if (fstat(out, &out_stat) != 0) die("stat output");
    if (S_ISREG(out_stat.st_mode) &&
        ftruncate(out, (off_t)((uint64_t)h.total_blks * h.blk_sz)) != 0) {
        die("resize output");
    }
    if (fsync(out) != 0) die("fsync output");
    fprintf(stderr, "simg2img-lite: wrote %" PRIu64 " bytes\n",
            (uint64_t)h.total_blks * h.blk_sz);
    close(out);
    close(in);
    return 0;
}
