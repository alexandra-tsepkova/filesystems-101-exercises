#include <solution.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define PATH_SIZE 1024

int get_num(const char* str){
    if (str == NULL){
        return -1;
    }
    char* endptr = NULL;
    pid_t res;
    res = strtol(str, &endptr, 10);
    if(endptr == str || *endptr != '\0' || res == 0){
        return -1;
    }
    return res;
}

void create_path_for_files(pid_t pid, char* path, int fd){
    snprintf(path, PATH_SIZE, "/proc/%d/fd/%d", pid, fd);
    return;
}

void report_files_from_fd_directory(DIR* dir_with_fds, pid_t pid){
    struct dirent *dir_entry_fd = NULL;
    while ((dir_entry_fd = readdir(dir_with_fds)) != NULL) {
        int fd = get_num(dir_entry_fd->d_name);
        if (fd == -1){
            continue;
        }

        char file_path_in_fd_dir[PATH_SIZE] = {-1};
        char result_path[PATH_SIZE] = {-1};
        create_path_for_files(pid, file_path_in_fd_dir, fd);


        ssize_t file_size = readlink(file_path_in_fd_dir, result_path, PATH_SIZE);
        if (file_size < 0) {
            report_error(file_path_in_fd_dir, errno);
            continue;
        }
        report_file(result_path);
    }
}

void lsof(void)
{
    DIR* dir = NULL;
    dir = opendir("/proc");

    if (dir == NULL){
        report_error("/proc", errno);
        return;
    }

    struct dirent *dir_entry = NULL;
    errno = 0;

    while((dir_entry = readdir(dir)) != NULL) {
        pid_t pid = get_num(dir_entry->d_name);
        if (pid == -1) {
            continue;
        }

        char fd_dir_path[PATH_SIZE] = {-1};
        snprintf(fd_dir_path, PATH_SIZE, "/proc/%d/fd/", pid);


        DIR *dir_with_fds = opendir(fd_dir_path);
        if (dir_with_fds == NULL) {
            report_error(fd_dir_path, errno);
            continue;
        }

        report_files_from_fd_directory(dir_with_fds, pid);
        closedir(dir_with_fds);
    }
    closedir(dir);
    return;
}
