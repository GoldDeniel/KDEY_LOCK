/* Reads a keyboard event node and prints key code press events.
 * Exits 0 if anything was received, 3 if the reader timed out silently. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: evmon <event> <ms>\n");
        return 2;
    }

    int fd = open(argv[1], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        perror("open");
        return 2;
    }

    int64_t end = now_ms() + atoi(argv[2]);
    int count = 0;

    while (now_ms() < end) {
        struct input_event ev;
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_KEY && ev.value == 1) {
                printf("%d\n", ev.code);
                fflush(stdout);
                count++;
            }
        } else {
            usleep(5000);
        }
    }

    close(fd);
    return count ? 0 : 3;
}
