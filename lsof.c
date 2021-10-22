#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>

#define BUF 1024
#define CMD_DISPLAY 10
#define USER_DISPLAY 11

struct pid_info_t {
    pid_t pid;
    char user[USER_DISPLAY];
    char cmdline[CMD_DISPLAY];
    char path[PATH_MAX];
    ssize_t parent_length;
};

void print_header()
{
    printf("%-25s %5s %8s %4s %9s %18s %9s %10s %s\n",
            "COMMAND",
            "PID",
            "USER",
            "FD",
            "TYPE",
            "DEVICE",
            "SIZE/OFF",
            "NODE",
            "NAME");
}

void print_type(char *type, struct pid_info_t* info)
{
    static ssize_t link_dest_size;
    static char link_dest[PATH_MAX];
    strncat(info->path, type, sizeof(info->path));
    if ((link_dest_size = readlink(info->path, link_dest, sizeof(link_dest)-1)) < 0) {
        if (errno == ENOENT)
            goto out;
        snprintf(link_dest, sizeof(link_dest), "%s (readlink: %s)", info->path, strerror(errno));
    } else {
        link_dest[link_dest_size] = '\0';
    }
    if (!strcmp(link_dest, "/"))
        goto out;
    printf("%-25s %5d %8s %4s %9s %18s %9s %10s %s\n",
            info->cmdline, info->pid, info->user, type,
            "", "", "", "", link_dest);
out:
    info->path[info->parent_length] = '\0';
}

void print_fds(struct pid_info_t* info)
{
    static char* fd_path = "fd/";
    strncat(info->path, fd_path, sizeof(info->path));
    int previous_length = info->parent_length;
    info->parent_length += strlen(fd_path);
    DIR *dir = opendir(info->path);
    if (dir == NULL) {
        char msg[BUF];
        snprintf(msg, sizeof(msg), "%s (opendir: %s)", info->path, strerror(errno));
        printf("%-25s %5d %8s %4s %9s %18s %9s %10s %s\n",
                info->cmdline, info->pid, info->user, "FDS",
                "", "", "", "", msg);
        goto out;
    }
    struct dirent* de;
    while ((de = readdir(dir))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        print_type(de->d_name, info);
    }
    closedir(dir);
out:
    info->parent_length = previous_length;
    info->path[info->parent_length] = '\0';
}

void lsof_dumpinfo(pid_t pid)
{
    int fd;
    struct pid_info_t info;
    struct stat pidstat;
    struct passwd *pw;
    info.pid = pid;
    snprintf(info.path, sizeof(info.path), "/proc/%d/", pid);
    info.parent_length = strlen(info.path);
    if (!stat(info.path, &pidstat)) {
        pw = getpwuid(pidstat.st_uid);
        if (pw) {
            strncpy(info.user, pw->pw_name, sizeof(info.user));
        } else {
            snprintf(info.user, USER_DISPLAY, "%d", (int)pidstat.st_uid);
        }
    } else {
        strcpy(info.user, "");
    }
    strncat(info.path, "cmdline", sizeof(info.path));
    fd = open(info.path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "couldn't read %s\n", info.path);
        return;
    }
    char cmdline[PATH_MAX];
    int numRead = read(fd, cmdline, sizeof(cmdline) - 1);
    close(fd);
    if (numRead < 0) {
        fprintf(stderr, "error reading cmdline: %s: %s\n", info.path, strerror(errno));
        return;
    }
    cmdline[numRead] = '\0';
    strncpy(info.cmdline, basename(cmdline), sizeof(info.cmdline));
    print_type("cwd", &info);
    print_type("exe", &info);
    print_type("root", &info);
    print_fds(&info);
}

int main(int argc, char *argv[])
{
    long int pid = 0;
    char* endptr;
    print_header();
    if (pid) {
        lsof_dumpinfo(pid);
    } else {
        DIR *dir = opendir("/proc");
        if (dir == NULL) {
            fprintf(stderr, "couldn't open /proc\n");
            return -1;
        }
        struct dirent* de;
        while ((de = readdir(dir))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
                continue;
            pid = strtol(de->d_name, &endptr, 10);
            if (*endptr != '\0')
                continue;
            lsof_dumpinfo(pid);
        }
        closedir(dir);
    }
    return 0;
}