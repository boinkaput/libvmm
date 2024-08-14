#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <uio/init.h>

/* Uncomment this to enable debug logging */
#define DEBUG_UIO_BLK_INIT

#if defined(DEBUG_UIO_BLK_INIT)
#define LOG_UIO_BLK_INIT(...) do{ printf("UIO_BLK_DRIVER_INIT"); printf(": "); printf(__VA_ARGS__); }while(0)
#else
#define LOG_UIO_BLK_INIT(...) do{}while(0)
#endif

#define LOG_UIO_BLK_INIT_ERR(...) do{ printf("UIO_BLK_DRIVER_INIT"); printf("|ERROR: "); printf(__VA_ARGS__); }while(0)

#if defined(BOARD_qemu_virt_aarch64)
static char *uio_blk_driver_args[] = {"uio_blk_driver", "/dev/vda", NULL};
#elif defined(BOARD_odroidc4)
static char *uio_blk_driver_args[] = {"uio_blk_driver", "/dev/mmcblk0", NULL};
#else
#error Need to define board for the uio driver
#endif

#define UIO_BLK_DRIVER_PROGRAM_PATH "/root/uio_blk_driver"
#define VIRTIO_BLK_MODULE_PATH "/modules/virtio_blk.ko"

static bool init_mounts()
{
    LOG_UIO_BLK_INIT("Mounting sysfs to /sys of type sysfs\n");
    int err = mount("sysfs", "/sys", "sysfs",
                    MS_NOSUID | MS_NOEXEC | MS_NODEV,
                    NULL);
    if (err < 0) {
        LOG_UIO_BLK_INIT_ERR("Failed to mount sysfs on /sys: %s\n", strerror(errno));
        return false;
    }

    LOG_UIO_BLK_INIT("Mounting devtmpfs to /dev of type devtmpfs\n");
    err = mount("devtmpfs", "/dev", "devtmpfs",
                MS_NOSUID | MS_STRICTATIME,
                NULL);
    if (err < 0) {
        LOG_UIO_BLK_INIT_ERR("Failed to mount devtmpfs on /dev: %s\n", strerror(errno));
        return false;
    }

    return true;
}

int main()
{
    if (getpid() != 1) {
        LOG_UIO_BLK_INIT_ERR("init is not running as pid 1");
        shutdown();
    }

    if (chdir("/")) {
        LOG_UIO_BLK_INIT_ERR("Failed to cd to \'/\': %s", strerror(errno));
        shutdown();
    }

    if (!init_mounts()) {
        shutdown();
    }

    LOG_UIO_BLK_INIT("Initialising kernel module: %s\n", VIRTIO_BLK_MODULE_PATH);
    if (!insmod(VIRTIO_BLK_MODULE_PATH)) {
        LOG_UIO_BLK_INIT_ERR("Failed to initialise kernel module %s: %s\n", VIRTIO_BLK_MODULE_PATH, strerror(errno));
        shutdown();
    }

    // Create a new process for the uio driver and wait for it to exit.
    LOG_UIO_BLK_INIT("Starting %s\n", uio_blk_driver_args[0]);
    int status = spawn_process_and_wait(UIO_BLK_DRIVER_PROGRAM_PATH, uio_blk_driver_args, execv);
    if (status == -1) {
        LOG_UIO_BLK_INIT_ERR("Failed to start process for %s: %s\n", uio_blk_driver_args[0], strerror(errno));
    } else if (WIFEXITED(status)) {
        LOG_UIO_BLK_INIT("%s exited with status %d\n", uio_blk_driver_args[0], WEXITSTATUS(status));
    } else {
        LOG_UIO_BLK_INIT_ERR("%s did not exit normally\n", uio_blk_driver_args[0]);
    }

    LOG_UIO_BLK_INIT("Shutting down...\n");
    shutdown();

    return 0;
}
