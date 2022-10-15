#include <solution.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define ARG_MAX 2097152
#define PATH_SIZE 256

pid_t get_pid(const char* str){
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

void create_path(pid_t pid, char* path, char* link){
    snprintf(path, PATH_SIZE, "/proc/%d/%s", pid, link);
    return;
}

ssize_t read_file(const char *path, char **result){
    int fd = open(path, O_RDONLY);
    if (fd == -1){
        return -1;
    }

    if ((*result) == NULL){
        close(fd);
    }

    ssize_t size_read = read(fd, *result, ARG_MAX * sizeof(char));
    if (size_read == -1){
        free(*result);
        close(fd);
        return -1;
    }
    close(fd);
    return(size_read);
}

void construct_list_of_strings(char **argv, char* buf, ssize_t buf_size){
    int argc = 0;
    ssize_t i;
    for (i = 0; i < buf_size; i++){
        if(buf[i] == '\0'){
            argc++;
        }
    }
    char * new_arg_start = buf;
    char * cur = buf;
    i = 0;
    while (cur < buf + buf_size) {
        if (*cur == '\0') {
            argv[i] = new_arg_start;
            new_arg_start = cur + 1;
            i++;
        }
        cur++;
    }
}

void ps(void)
{
    DIR* dir = NULL;
    dir = opendir("/proc");

    if (dir == NULL){
        report_error("/proc", errno);
        return;
    }

    struct dirent *dir_entry = NULL;
    char **argv = (char **)malloc(ARG_MAX * sizeof(char *));
    char **envp = (char **)malloc(ARG_MAX * sizeof(char *));
    errno = 0;

    while((dir_entry = readdir(dir)) != NULL){
        pid_t pid = get_pid(dir_entry -> d_name);
        if (pid == -1){
            continue;
        }

        char exe_path[PATH_SIZE] = {-1};
        char environ_path[PATH_SIZE] = {-1};
        char cmdline_path[PATH_SIZE] = { -1};

        char result_exe_path[PATH_SIZE] = {-1};

        create_path(pid, exe_path, "exe");
        create_path(pid, environ_path, "environ");
        create_path(pid, cmdline_path, "cmdline");

        ssize_t exe_size = readlink(exe_path, result_exe_path, PATH_SIZE);
        if (exe_size < 0){
            report_error(exe_path, errno);
            continue;
        }

        char *args_from_file = (char *)calloc(ARG_MAX, sizeof(char));;
        ssize_t args_size = read_file(cmdline_path, &args_from_file);
        if (args_size < 0){
            report_error(cmdline_path, errno);
            continue;
        }
        memset(argv, 0, ARG_MAX * sizeof(char*));
        construct_list_of_strings(argv, args_from_file, args_size);

        char* envs_from_file = (char *)calloc(ARG_MAX, sizeof(char));;
        ssize_t envs_size = read_file(environ_path, &envs_from_file);
        if (envs_size < 0){
            report_error(environ_path, errno);
            continue;
        }
        memset(envp, 0, ARG_MAX * sizeof(char*));
        construct_list_of_strings(envp, envs_from_file, envs_size);

        report_process(pid, result_exe_path, argv, envp);

        free(args_from_file);
        free(envs_from_file);
    }

    free(argv);
    free(envp);
    closedir(dir);
    return;
}
