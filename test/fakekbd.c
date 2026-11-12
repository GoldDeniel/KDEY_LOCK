/* Creates a virtual keyboard and emits key events on commands from stdin.
 * Test-only; requires /dev/uinput (inside the container). */
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int find_event(char *out, size_t len)
{
    DIR *dir = opendir("/sys/class/input");
    if (!dir)
        return -1;

    struct dirent *de;
    int found = -1;
    while ((de = readdir(dir)) != NULL) {
        if (strncmp(de->d_name, "event", 5) != 0)
            continue;

        char npath[512], name[128] = {0};
        snprintf(npath, sizeof(npath), "/sys/class/input/%s/device/name",
                 de->d_name);
        FILE *f = fopen(npath, "r");
        if (!f)
            continue;
        if (fgets(name, sizeof(name), f) &&
            strstr(name, "keyclean-test-kbd")) {
            snprintf(out, len, "/dev/input/%s", de->d_name);
            found = 0;
        }
        fclose(f);
        if (found == 0)
            break;
    }
    closedir(dir);
    return found;
}

static void emit(int fd, int code)
{
    struct input_event ev = {0};
    ev.type = EV_KEY;
    ev.code = code;
    ev.value = 1;
    (void)write(fd, &ev, sizeof(ev));
    ev.value = 0;
    (void)write(fd, &ev, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    (void)write(fd, &ev, sizeof(ev));
}

int main(void)
{
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open /dev/uinput");
        return 1;
    }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    for (int c = 1; c <= KEY_MAX; c++)
        ioctl(fd, UI_SET_KEYBIT, c);

    struct uinput_setup setup = {0};
    snprintf(setup.name, sizeof(setup.name), "keyclean-test-kbd");
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1234;
    setup.id.product = 0x5678;
    setup.id.version = 1;

    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 ||
        ioctl(fd, UI_DEV_CREATE) < 0) {
        perror("uinput setup");
        return 1;
    }

    char path[512] = {0};
    for (int i = 0; i < 100 && !path[0]; i++) {
        usleep(20000);
        find_event(path, sizeof(path));
    }
    if (!path[0]) {
        fprintf(stderr, "event node not found\n");
        return 1;
    }
    printf("%s\n", path);
    fflush(stdout);

    char line[64];
    while (fgets(line, sizeof(line), stdin)) {
        if (strncmp(line, "esc", 3) == 0)
            emit(fd, KEY_ESC);
        else if (line[0] == 'a')
            emit(fd, KEY_A);
        else if (line[0] == 'q')
            break;
    }

    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
