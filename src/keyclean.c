/*
 * keyclean - temporarily ignore physical keyboards so the keyboard can be
 * cleaned/wiped while the machine stays on. Mouse/touchpad keep working.
 *
 * The daemon grabs each keyboard with EVIOCGRAB. While grabbed, the compositor
 * (KWin, Mutter, any Wayland or X11 session) receives no key events. The panel
 * button talks to the daemon over a per-user unix socket, so it works the same
 * on KDE and GNOME, Wayland or X11.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_KBD       64
#define RESCAN_MS     2000
#define PANIC_HITS    5
#define PANIC_WINDOW  2000
#define DEFAULT_TIMEOUT 600

typedef struct {
    char path[PATH_MAX];
    int fd;
} kbd_t;

static kbd_t kbds[MAX_KBD];
static int nkbds;
static bool enabled;
static int64_t activated_ms;
static int64_t last_scan_ms;
static int timeout_s = DEFAULT_TIMEOUT;
static volatile sig_atomic_t running = 1;

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

static int bit_set(const unsigned long *bits, int bit)
{
    return (bits[bit / (int)(sizeof(long) * 8)] >>
            (bit % (int)(sizeof(long) * 8))) & 1UL;
}

/* A device that has the normal alpha keys is a keyboard. Devices that also
 * report relative axes are skipped: they are keyboard/mouse combos, and
 * grabbing one would also kill the pointer used to turn cleaning mode off. */
static bool is_keyboard(int fd)
{
    unsigned long ev[EV_MAX / (int)(sizeof(long) * 8) + 2] = {0};
    unsigned long rel[REL_MAX / (int)(sizeof(long) * 8) + 2] = {0};
    unsigned long key[KEY_MAX / (int)(sizeof(long) * 8) + 2] = {0};

    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) < 0)
        return false;
    if (!bit_set(ev, EV_KEY))
        return false;

    if (bit_set(ev, EV_REL) &&
        ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel)), rel) >= 0 &&
        (bit_set(rel, REL_X) || bit_set(rel, REL_Y)))
        return false;

    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key) < 0)
        return false;

    return bit_set(key, KEY_A) && bit_set(key, KEY_Z) &&
           bit_set(key, KEY_SPACE) && bit_set(key, KEY_ENTER);
}

static int open_and_grab(const char *path)
{
    int flags = O_RDONLY;

    for (;;) {
        int fd = open(path, flags | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            return -1;
        if (!is_keyboard(fd)) {
            close(fd);
            return -2;
        }
        if (ioctl(fd, EVIOCGRAB, 1) == 0)
            return fd;
        int err = errno;
        close(fd);
        /* Some systems only allow grabbing on a read-write descriptor. */
        if (err == EACCES && flags == O_RDONLY) {
            flags = O_RDWR;
            continue;
        }
        return -3;
    }
}

static bool already_grabbed(const char *path)
{
    for (int i = 0; i < nkbds; i++)
        if (strcmp(kbds[i].path, path) == 0)
            return true;
    return false;
}

static void remove_kbd(int i)
{
    if (i < 0 || i >= nkbds)
        return;
    if (kbds[i].fd >= 0)
        close(kbds[i].fd);
    kbds[i] = kbds[--nkbds];
}

static void release_all(void)
{
    for (int i = 0; i < nkbds; i++)
        if (kbds[i].fd >= 0) {
            ioctl(kbds[i].fd, EVIOCGRAB, 0);
            close(kbds[i].fd);
        }
    nkbds = 0;
}

/* Returns number of keyboards currently grabbed. */
static int grab_all(void)
{
    DIR *dir = opendir("/dev/input");
    if (!dir)
        return nkbds;

    struct dirent *de;
    while ((de = readdir(dir)) != NULL && nkbds < MAX_KBD) {
        if (strncmp(de->d_name, "event", 5) != 0)
            continue;

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
        if (already_grabbed(path))
            continue;

        int fd = open_and_grab(path);
        if (fd < 0)
            continue;

        size_t plen = strlen(path);
        if (plen >= sizeof(kbds[nkbds].path))
            plen = sizeof(kbds[nkbds].path) - 1;
        memcpy(kbds[nkbds].path, path, plen);
        kbds[nkbds].path[plen] = '\0';
        kbds[nkbds].fd = fd;
        nkbds++;
    }
    closedir(dir);
    return nkbds;
}

static int64_t esc_first;
static int esc_hits;
static bool panic_pending;

static bool check_panic(void)
{
    int64_t now = now_ms();
    if (esc_hits == 0 || now - esc_first > PANIC_WINDOW) {
        esc_first = now;
        esc_hits = 1;
    } else {
        esc_hits++;
    }
    if (esc_hits >= PANIC_HITS) {
        esc_hits = 0;
        return true;
    }
    return false;
}

static void drain_kbd(int i)
{
    struct input_event ev;

    for (;;) {
        ssize_t n = read(kbds[i].fd, &ev, sizeof(ev));
        if (n == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_KEY && ev.code == KEY_ESC && ev.value == 1 &&
                check_panic()) {
                /* Do not release here: the caller is iterating the fd list. */
                panic_pending = true;
                return;
            }
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        } else {
            remove_kbd(i);
            return;
        }
    }
}

static void runtime_path(char *buf, size_t len, const char *name)
{
    const char *dir = getenv("XDG_RUNTIME_DIR");
    if (!dir || !*dir)
        dir = "/tmp";
    snprintf(buf, len, "%s/%s-%d", dir, name, (int)getuid());
}

static socklen_t addr_from_path(struct sockaddr_un *addr, const char *path)
{
    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;

    size_t len = strlen(path);
    if (len >= sizeof(addr->sun_path))
        len = sizeof(addr->sun_path) - 1;
    memcpy(addr->sun_path, path, len);
    addr->sun_path[len] = '\0';

    return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + len + 1);
}

static int connect_socket(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    socklen_t alen = addr_from_path(&addr, path);

    if (connect(fd, (struct sockaddr *)&addr, alen) < 0) {
        close(fd);
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    return fd;
}

static int make_server(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    socklen_t alen = addr_from_path(&addr, path);

    unlink(path);
    if (bind(fd, (struct sockaddr *)&addr, alen) < 0) {
        close(fd);
        return -1;
    }
    chmod(path, 0600);
    listen(fd, 4);
    return fd;
}

static void set_enabled(bool on)
{
    if (!on) {
        release_all();
        enabled = false;
        return;
    }
    if (!enabled) {
        activated_ms = now_ms();
        last_scan_ms = 0;
        esc_hits = 0;
    }
    enabled = true;
    grab_all();
}

/* Returns 0 on success and -1 when no keyboard could be grabbed. */
static int handle_command(const char *cmd, char *resp, size_t len)
{
    if (strcmp(cmd, "on") == 0) {
        set_enabled(true);
        if (!enabled || nkbds == 0) {
            enabled = false;
            snprintf(resp, len, "error: no keyboard found\n");
            return -1;
        }
        snprintf(resp, len, "on\n");
        return 0;
    }
    if (strcmp(cmd, "toggle") == 0) {
        if (enabled) {
            set_enabled(false);
            snprintf(resp, len, "off\n");
            return 0;
        }
        set_enabled(true);
        if (!enabled || nkbds == 0) {
            enabled = false;
            snprintf(resp, len, "error: no keyboard found\n");
            return -1;
        }
        snprintf(resp, len, "on\n");
        return 0;
    }
    if (strcmp(cmd, "off") == 0) {
        set_enabled(false);
        snprintf(resp, len, "off\n");
        return 0;
    }
    if (strcmp(cmd, "status") == 0) {
        snprintf(resp, len, "%s\n", enabled ? "on" : "off");
        return 0;
    }
    if (strcmp(cmd, "stop") == 0) {
        set_enabled(false);
        running = 0;
        snprintf(resp, len, "off\n");
        return 0;
    }
    snprintf(resp, len, "error: unknown command\n");
    return -1;
}

static void serve_client(int fd)
{
    char cmd[64];
    size_t len = 0;
    struct timeval tv = { .tv_sec = 2 };

    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (len < sizeof(cmd) - 1) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0)
            break;
        if (c == '\n')
            break;
        cmd[len++] = c;
    }
    cmd[len] = '\0';

    char resp[64];
    handle_command(cmd, resp, sizeof(resp));
    (void)write(fd, resp, strlen(resp));
    close(fd);
}

static int daemon_main(void)
{
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGQUIT, on_signal);

    const char *t = getenv("KEYCLEAN_TIMEOUT");
    if (t && atoi(t) >= 0)
        timeout_s = atoi(t);

    char sock[PATH_MAX], lock[PATH_MAX];
    runtime_path(sock, sizeof(sock), "keyclean.sock");
    runtime_path(lock, sizeof(lock), "keyclean.lock");

    int lock_fd = open(lock, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock_fd < 0)
        return 1;
    if (flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        close(lock_fd);
        return 0; /* another daemon already owns the socket */
    }

    int listen_fd = make_server(sock);
    if (listen_fd < 0) {
        close(lock_fd);
        return 1;
    }

    struct stat self;
    if (stat(sock, &self) < 0) {
        close(listen_fd);
        close(lock_fd);
        return 1;
    }

    while (running) {
        int64_t now = now_ms();

        if (enabled && now - last_scan_ms >= RESCAN_MS) {
            last_scan_ms = now;
            grab_all();
        }
        if (enabled && timeout_s > 0 &&
            now - activated_ms >= (int64_t)timeout_s * 1000) {
            release_all();
            enabled = false;
        }

        /* If the runtime directory was cleaned (logout), stop and release. */
        struct stat st;
        if (stat(sock, &st) < 0 || st.st_ino != self.st_ino) {
            running = 0;
            break;
        }

        struct pollfd pfd[1 + MAX_KBD];
        pfd[0].fd = listen_fd;
        pfd[0].events = POLLIN;
        int polled = nkbds;
        for (int i = 0; i < polled; i++) {
            pfd[1 + i].fd = kbds[i].fd;
            pfd[1 + i].events = POLLIN;
        }

        int rc = poll(pfd, 1 + polled, 250);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (rc == 0)
            continue;

        /* Process device events before commands: a command may add or release
         * keyboards, which would invalidate the pollfd snapshot. */
        for (int i = polled - 1; i >= 0; i--) {
            short re = pfd[1 + i].revents;
            if (re & (POLLIN | POLLERR | POLLHUP | POLLNVAL))
                drain_kbd(i);
        }

        if (panic_pending) {
            panic_pending = false;
            release_all();
            enabled = false;
        }

        if (pfd[0].revents & POLLIN) {
            int cfd = accept4(listen_fd, NULL, NULL, SOCK_CLOEXEC);
            if (cfd >= 0)
                serve_client(cfd);
        }
    }

    release_all();
    close(listen_fd);
    close(lock_fd);
    unlink(sock);
    return 0;
}

static int start_daemon(char *exe, size_t exe_len)
{
    ssize_t n = readlink("/proc/self/exe", exe, exe_len - 1);
    if (n <= 0)
        return -1;
    exe[n] = '\0';

    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        setsid();
        if (fork() != 0)
            _exit(0);

        umask(077);
        if (chdir("/") < 0)
            _exit(1);

        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO)
                close(devnull);
        }

        execl(exe, exe, "--daemon", (char *)NULL);
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0);
    return 0;
}

static int request(const char *cmd, char *resp, size_t len)
{
    char sock[PATH_MAX];
    runtime_path(sock, sizeof(sock), "keyclean.sock");

    int fd = connect_socket(sock);
    if (fd < 0) {
        char exe[PATH_MAX];
        if (start_daemon(exe, sizeof(exe)) < 0)
            return -1;

        for (int i = 0; i < 50; i++) {
            usleep(20000);
            fd = connect_socket(sock);
            if (fd >= 0)
                break;
        }
        if (fd < 0)
            return -1;
    }

    if (write(fd, cmd, strlen(cmd)) < 0 ||
        write(fd, "\n", 1) < 0) {
        close(fd);
        return -1;
    }

    size_t used = 0;
    while (used < len - 1) {
        ssize_t n = read(fd, resp + used, len - 1 - used);
        if (n <= 0)
            break;
        used += n;
    }
    resp[used] = '\0';
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--daemon") == 0)
        return daemon_main();

    const char *cmd = argc > 1 ? argv[1] : "toggle";
    if (strcmp(cmd, "on") != 0 && strcmp(cmd, "off") != 0 &&
        strcmp(cmd, "toggle") != 0 && strcmp(cmd, "status") != 0 &&
        strcmp(cmd, "is-on") != 0 && strcmp(cmd, "stop") != 0) {
        fprintf(stderr,
                "usage: keyclean [on|off|toggle|status|is-on|stop]\n");
        return 2;
    }

    if (strcmp(cmd, "is-on") == 0) {
        char resp[64];
        if (request("status", resp, sizeof(resp)) < 0)
            return 1;
        return strcmp(resp, "on\n") == 0 ? 0 : 1;
    }

    if (strcmp(cmd, "stop") == 0) {
        char resp[64];
        if (request(cmd, resp, sizeof(resp)) < 0)
            return 0;
        fputs(resp, stdout);
        return 0;
    }

    char resp[64];
    if (request(cmd, resp, sizeof(resp)) < 0) {
        fprintf(stderr, "keyclean: cannot reach helper: %s\n", strerror(errno));
        return 1;
    }

    if (strncmp(resp, "error:", 6) == 0) {
        fprintf(stderr, "keyclean:%s", resp + 6);
        return 1;
    }

    fputs(resp, stdout);
    return 0;
}
