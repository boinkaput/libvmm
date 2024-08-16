#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <uio/init.h>

/* Uncomment this to enable debug logging */
#define DEBUG_UIO_SND_INIT

#if defined(DEBUG_UIO_SND_INIT)
#define LOG_UIO_SND_INIT(...) do{ printf("UIO_SND_DRIVER_INIT"); printf(": "); printf(__VA_ARGS__); }while(0)
#else
#define LOG_UIO_SND_INIT(...) do{}while(0)
#endif

#define LOG_UIO_SND_INIT_ERR(...) do{ printf("UIO_SND_DRIVER_INIT"); printf("|ERROR: "); printf(__VA_ARGS__); }while(0)

#define ALSA_CONFIG_DIR "/alsa"
#define UIO_SND_DRIVER_LOGFILE "/user_sound"
#define UIO_SND_DRIVER_PROGRAM_PATH "/root/uio_snd_driver"
#define NUM_VIRTIO_MODULES 18

static const char *module_paths[] = {
    "/modules/nls_base.ko", "/modules/soundcore.ko", "/modules/snd-intel-dspcfg.ko",
    "/modules/ledtrig-audio.ko", "/modules/led-class.ko", "/modules/usbcore.ko",
    "/modules/snd.ko", "/modules/snd-timer.ko", "/modules/snd-pcm.ko",
    "/modules/snd-hda-core.ko", "/modules/snd-hda-codec.ko", "/modules/snd-hda-codec-generic.ko",
    "/modules/snd-hda-intel.ko", "/modules/snd-hda-codec-realtek.ko", "/modules/snd-hda-codec-idt.ko",
    "/modules/snd-hda-codec-conexant.ko", "/modules/snd-ctl-led.ko", "/modules/virtio_snd.ko"
};

static char *uio_snd_driver_args[] = {"user_sound.elf", "default", "hw:0,0", NULL};

static bool init_mounts()
{
    LOG_UIO_SND_INIT("Mounting sysfs to /sys of type sysfs\n");
    int err = mount("sysfs", "/sys", "sysfs",
                    MS_NOSUID | MS_NOEXEC | MS_NODEV,
                    NULL);
    if (err < 0) {
        LOG_UIO_SND_INIT_ERR("Failed to mount sysfs on /sys: %s\n", strerror(errno));
        return false;
    }

    LOG_UIO_SND_INIT("Mounting devtmpfs to /dev of type devtmpfs\n");
    err = mount("devtmpfs", "/dev", "devtmpfs",
                MS_NOSUID | MS_STRICTATIME,
                NULL);
    if (err < 0) {
        LOG_UIO_SND_INIT_ERR("Failed to mount devtmpfs on /dev: %s\n", strerror(errno));
        return false;
    }

    return true;
}

static int exec_uio_snd_driver(const char *program, char *const *program_args)
{
    int fd = open(UIO_SND_DRIVER_LOGFILE, O_WRONLY | O_CREAT | O_APPEND);
    if (fd < 0) {
        LOG_UIO_SND_INIT_ERR("Failed to open log file\n");
        return EXIT_FAILURE;
    }

    // Redirect stdout and stderr to log file.
    int err = dup2(fd, STDOUT_FILENO);
    if (err < 0) {
        LOG_UIO_SND_INIT_ERR("Failed to redirect stdout to log file\n");
        return EXIT_FAILURE;
    }

    err = dup2(fd, STDERR_FILENO);
    if (err < 0) {
        LOG_UIO_SND_INIT_ERR("Failed to redirect stderr to log file\n");
        return EXIT_FAILURE;
    }

    err = close(fd);
    if (err < 0) {
        LOG_UIO_SND_INIT_ERR("Failed to close log file\n");
        return EXIT_FAILURE;
    }

    sleep(1);
    execv(program, program_args);
    return EXIT_FAILURE;
}

int main()
{
    if (getpid() != 1) {
        LOG_UIO_SND_INIT_ERR("init is not running as pid 1");
        shutdown();
    }

    if (chdir("/")) {
        LOG_UIO_SND_INIT_ERR("Failed to cd to \'/\': %s", strerror(errno));
        shutdown();
    }

    if (!init_mounts()) {
        shutdown();
    }

    if (setenv("ALSA_CONFIG_DIR", ALSA_CONFIG_DIR, 1) != 0) {
        LOG_UIO_SND_INIT_ERR("Failed to set environment variable ALSA_CONFIG_DIR: %s", strerror(errno));
        shutdown();
    }

    for (int i = 0; i < NUM_VIRTIO_MODULES; i++) {
        LOG_UIO_SND_INIT("Initialising kernel module: %s\n", module_paths[i]);
        if (!insmod(module_paths[i])) {
            LOG_UIO_SND_INIT_ERR("Failed to initialise kernel module %s: %s\n", module_paths[i], strerror(errno));
            shutdown();
        }
    }

    // Create a new process for the uio driver and wait for it to finish.
    int status = spawn_process_and_wait(UIO_SND_DRIVER_PROGRAM_PATH, uio_snd_driver_args, exec_uio_snd_driver);
    if (status == -1) {
        LOG_UIO_SND_INIT_ERR("Failed to start process for %s: %s\n", uio_snd_driver_args[0], strerror(errno));
    } else if (WIFEXITED(status)) {
        LOG_UIO_SND_INIT("%s exited with status %d\n", uio_snd_driver_args[0], WEXITSTATUS(status));
    } else {
        LOG_UIO_SND_INIT_ERR("%s did not exit normally\n", uio_snd_driver_args[0]);
    }

    LOG_UIO_SND_INIT("Shutting down...\n");
    shutdown();

    return 0;
}
