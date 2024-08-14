#include <fcntl.h>
#include <stdlib.h>
#include <sys/reboot.h>
#include <sys/syscall.h>
#include <uio/init.h>

bool insmod(const char *module_path)
{
    int fd = open(module_path, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    int err = syscall(__NR_finit_module, fd, "");
    if (err != 0) {
        close(fd);
        return false;
    }

    err = close(fd);
    if (err != 0) {
        return false;
    }

    return true;
}

int spawn_process_and_wait(const char *program, char *const *program_args,
                           int (*child_fn)(const char *, char *const *))
{
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    } else if (pid == 0) {
        int exit_status = child_fn(program, program_args);
        exit(exit_status);
    } else {
        int status;
        if (waitpid(pid, &status, 0) < 0) {
            return -1;
        }
        return status;
    }
}

void shutdown(void)
{
    reboot(RB_POWER_OFF);
    while(1) {
        sleep(1);
    }
}
