#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void die(const char *what) {
    perror(what);
    exit(EXIT_FAILURE);
}

static void write_text_file(const char *path, const char *text) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        die(path);
    size_t length = strlen(text);
    if (write(fd, text, length) != (ssize_t)length)
        die(path);
    if (close(fd) < 0)
        die(path);
}

static void configure_user_namespace(void) {
    char map[64];
    write_text_file("/proc/self/setgroups", "deny\n");
    snprintf(map, sizeof(map), "0 %ld 1\n", (long)getuid());
    write_text_file("/proc/self/uid_map", map);
    snprintf(map, sizeof(map), "0 %ld 1\n", (long)getgid());
    write_text_file("/proc/self/gid_map", map);
}

static void usage(const char *program) {
    fprintf(stderr, "Usage: %s --root ROOTFS [--hostname NAME] -- COMMAND [ARGS...]\n", program);
    exit(EXIT_FAILURE);
}

static void setup_rootfs(const char *rootfs) {
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0)
        die("make mounts private");

    if (mount(rootfs, rootfs, NULL, MS_BIND | MS_REC, NULL) < 0)
        die("bind rootfs");
    if (chdir(rootfs) < 0)
        die("chdir rootfs");
    if (chroot(".") < 0)
        die("chroot");
    if (chdir("/") < 0)
        die("chdir /");

    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0)
        die("mount /proc");
}

int main(int argc, char **argv) {
    const char *rootfs = NULL;
    const char *hostname = "ns-container";
    int command_index = -1;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--") == 0) {
            command_index = i + 1;
            break;
        }
        if (strcmp(argv[i], "--root") == 0 && i + 1 < argc)
            rootfs = argv[++i];
        else if (strcmp(argv[i], "--hostname") == 0 && i + 1 < argc)
            hostname = argv[++i];
        else
            usage(argv[0]);
    }
    if (!rootfs || command_index < 0 || command_index >= argc)
        usage(argv[0]);

    if (unshare(CLONE_NEWUSER) < 0)
        die("unshare user namespace");
    configure_user_namespace();

    if (unshare(CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWNET | CLONE_NEWIPC | CLONE_NEWPID) < 0)
        die("unshare namespaces");
    if (sethostname(hostname, strlen(hostname)) < 0)
        die("sethostname");

    pid_t child = fork();
    if (child < 0)
        die("fork PID-namespace init");
    if (child == 0) {
        setup_rootfs(rootfs);
        execvp(argv[command_index], &argv[command_index]);
        die("exec command");
    }

    int status;
    if (waitpid(child, &status, 0) < 0)
        die("waitpid");
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "command killed by signal %d\n", WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }
    return EXIT_FAILURE;
}
