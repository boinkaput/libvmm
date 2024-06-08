#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/reboot.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/wait.h>

#define UIO_DRIVER_PROGRAM_PATH "/root/uio_blk_driver"

#if defined(BOARD_qemu_arm_virt)
#define UIO_DRIVER_PROGRAM_ARGS {"uio_blk_driver", "/dev/vda", NULL}
#elif defined(BOARD_odroidc4)
#define UIO_DRIVER_PROGRAM_ARGS {"uio_blk_driver", "/dev/mmcblk0", NULL}
#else
#error Need to define args for the uio driver
#endif

#define LOG_UIO_DRIVER_INFO(...) do{ printf("UIO_DRIVER_INIT"); printf("|INFO "); printf(__VA_ARGS__); }while(0)
#define LOG_UIO_DRIVER_ERR(...) do{ printf("UIO_DRIVER_INIT"); printf("|ERROR: "); printf(__VA_ARGS__); }while(0)

int run_child(const char *program, char *const *program_args,
              int (*child_fn)(const char *, char *const *))
{
    LOG_UIO_DRIVER_INFO("Starting %s\n", program);
    pid_t pid = fork();
    if (pid < 0) {
        LOG_UIO_DRIVER_ERR("Failed to fork\n");
        return -1;
    } else if (pid == 0) {
        // Child process - should not exit.
        int exit_status = child_fn(program, program_args);
        LOG_UIO_DRIVER_ERR("Failed to execute %s\n", program);
        exit(exit_status);
    } else {
        // Parent process - wait for the uio driver to exit (error if it does).
        int status;
        if (waitpid(pid, &status, 0) < 0) {
            LOG_UIO_DRIVER_ERR("Failed to wait for %s\n", program);
            return -1;
        }

        if (WIFEXITED(status)) {
            LOG_UIO_DRIVER_INFO("%s exited with status %d\n", program, WEXITSTATUS(status));
        } else {
            LOG_UIO_DRIVER_ERR("%s did not exit normally\n", program);
        }
        return status;
    }
}

int main()
{
    // Mount proc, sysfs and devtmpfs.
    LOG_UIO_DRIVER_INFO("Mounting proc to /proc of type proc\n");
    int err = mount("proc", "/proc", "proc",
                    MS_NOSUID | MS_NOEXEC | MS_NODEV,
                    NULL);
    if (err < 0) {
        LOG_UIO_DRIVER_ERR("Failed to mount proc on /proc\n");
        goto shutdown;
    }

    LOG_UIO_DRIVER_INFO("Mounting sysfs to /sys of type sysfs\n");
    err = mount("sysfs", "/sys", "sysfs",
                MS_NOSUID | MS_NOEXEC | MS_NODEV,
                NULL);
    if (err < 0) {
        LOG_UIO_DRIVER_ERR("Failed to mount sysfs on /sys\n");
        goto shutdown;
    }

    LOG_UIO_DRIVER_INFO("Mounting devtmpfs to /dev of type devtmpfs\n");
    err = mount("devtmpfs", "/dev", "devtmpfs",
                MS_NOSUID | MS_STRICTATIME,
                NULL);
    if (err < 0) {
        LOG_UIO_DRIVER_ERR("Failed to mount devtmpfs on /dev\n");
        goto shutdown;
    }

    // Create a new process for the uio driver and wait for it to finish.
    char *uio_driver_args[] = UIO_DRIVER_PROGRAM_ARGS;
    run_child(UIO_DRIVER_PROGRAM_PATH, uio_driver_args, execv);

shutdown:
    sync();
    reboot(LINUX_REBOOT_CMD_POWER_OFF);

    return EXIT_FAILURE;
}
