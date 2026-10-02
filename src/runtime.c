#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>

#define STACK_SIZE (1024 * 1024)

static char  *g_rootfs;
static char **g_cmd;
static int    g_sync[2];

static void die(const char *msg) {
    perror(msg);
    exit(1);
}

static void write_file(const char *path, const char *str) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) die(path);
    if (write(fd, str, strlen(str)) < 0) die("write");
    close(fd);
}

static int child_main(void *arg) {
    (void)arg;

    char c;
    if (read(g_sync[0], &c, 1) != 1) die("read sync");
    close(g_sync[0]);

    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0)
        die("mount private");

    if (chroot(g_rootfs) < 0) die("chroot");
    if (chdir("/") < 0)      die("chdir");

    mkdir("/proc", 0755);
    if (mount("proc", "/proc", "proc", 0, NULL) < 0)
        die("mount proc");

    mkdir("/dev", 0755);
    if (mount("tmpfs", "/dev", "tmpfs", MS_NOSUID, "mode=755") < 0)
        die("mount dev");

    const char *devs[] = {"null", "zero", "full", "random", "urandom", "tty", NULL};
    for (int i = 0; devs[i]; i++) {
        char p[64];
        snprintf(p, sizeof(p), "/dev/%s", devs[i]);
        int fd = open(p, O_CREAT | O_WRONLY, 0666);
        if (fd >= 0) close(fd);
        mount(p, p, NULL, MS_BIND, NULL);
    }

    sethostname("container", 9);

    execv(g_cmd[0], g_cmd);
    die("execv");
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 5 || strcmp(argv[1], "run") != 0 || strcmp(argv[2], "--rootfs") != 0) {
        fprintf(stderr, "usage: %s run --rootfs <path> <cmd> [args...]\n", argv[0]);
        return 1;
    }
    g_rootfs = argv[3];
    g_cmd    = &argv[4];

    if (pipe(g_sync) < 0) die("pipe");

    char *stack = malloc(STACK_SIZE);
    if (!stack) die("malloc");

    int flags = CLONE_NEWPID | CLONE_NEWNS  | CLONE_NEWUSER |
                CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWNET | SIGCHLD;

    pid_t child = clone(child_main, stack + STACK_SIZE, flags, NULL);
    if (child < 0) die("clone");

    char path[256], buf[64];

    snprintf(path, sizeof(path), "/proc/%d/setgroups", child);
    int fd = open(path, O_WRONLY);
    if (fd >= 0) { write(fd, "deny", 4); close(fd); }

    snprintf(path, sizeof(path), "/proc/%d/uid_map", child);
    snprintf(buf,  sizeof(buf),  "0 %d 1\n", getuid());
    write_file(path, buf);

    snprintf(path, sizeof(path), "/proc/%d/gid_map", child);
    snprintf(buf,  sizeof(buf),  "0 %d 1\n", getgid());
    write_file(path, buf);

    if (write(g_sync[1], "x", 1) != 1) die("write sync");
    close(g_sync[1]);

    int status;
    waitpid(child, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}
