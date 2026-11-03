#include <errno.h>
#include <time.h>

int main(void) {
    struct timespec remaining = { .tv_sec = 0, .tv_nsec = 200000000L };
    while (nanosleep(&remaining, &remaining) == -1 && errno == EINTR) {}
    return 0;
}
