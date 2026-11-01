// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/reboot.h>
#include <signal.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define WAIT_SECONDS 15
#define ROOT_DEVICE "/dev/block/by-name/linux_root"
#define ROOT_DEVICE_SECTORS 419430400ULL
#define NEW_ROOT "/newroot"
#define RESCUE_ROOT "/nwrt-rescue"
#define MODULE_DIRECTORY "/lib/modules"
#define MODULES_LOAD MODULE_DIRECTORY "/modules.load"
#define MODULES_LOAD_RECOVERY MODULE_DIRECTORY "/modules.load.recovery"
#define MODULES_DEP MODULE_DIRECTORY "/modules.dep"
#define MODULES_SOFTDEP MODULE_DIRECTORY "/modules.softdep"
#define RAM_BOOT_LOG "/run/xm14-boot-shim.log"
#define PERSISTENT_LOG_DIRECTORY NEW_ROOT "/root/nwrt-logs"
#define PERSISTENT_BOOT_LOG PERSISTENT_LOG_DIRECTORY "/boot-shim-last.log"
#define EARLY_SPLASH_PATH "/early-splash"

static pid_t early_splash_pid = -1;
#define MAX_MODULES 512
#define MAX_MODULE_DEPENDENCIES 128

#define FIRMWARE_MOUNT "/vendor/firmware_mnt"
#define FIRMWARE_BIND "/vendor/firmware"
#define MODEM_FIRMWARE_MOUNT "/vendor/modem_firmware"
#define DSP_MOUNT "/vendor/dsp"
#define BT_FIRMWARE_MOUNT "/vendor/bt_firmware"

static char loaded_modules[MAX_MODULES][NAME_MAX];
static size_t loaded_module_count;
static bool defer_firmware_consumers;

static void trim(char *value);
static bool wait_for_path(const char *path);
static void log_message(const char *format, ...);

static bool verify_root_device_geometry(void) {
    struct stat device_stat;
    if (stat(ROOT_DEVICE, &device_stat) != 0 || !S_ISBLK(device_stat.st_mode)) {
        log_message("linux_root is not a block device");
        return false;
    }

    char size_path[PATH_MAX];
    snprintf(size_path, sizeof(size_path), "/sys/dev/block/%u:%u/size",
             major(device_stat.st_rdev), minor(device_stat.st_rdev));
    FILE *size_file = fopen(size_path, "re");
    if (!size_file) {
        log_message("cannot read linux_root geometry: %s", strerror(errno));
        return false;
    }
    unsigned long long sectors = 0;
    int matched = fscanf(size_file, "%llu", &sectors);
    fclose(size_file);
    if (matched != 1 || sectors != ROOT_DEVICE_SECTORS) {
        log_message("refusing linux_root with unexpected size: %llu sectors",
                    sectors);
        return false;
    }
    log_message("verified exact 200 GiB linux_root geometry");
    return true;
}

static int reboot_bootloader(void) {
    if (getuid() != 0) {
        fprintf(stderr, "xm14pro-boot-shim: reboot helper requires root\n");
        return 2;
    }
    sync();
    if (syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
                LINUX_REBOOT_CMD_RESTART2, "bootloader") != 0) {
        fprintf(stderr, "xm14pro-boot-shim: reboot to bootloader failed: %s\n",
                strerror(errno));
        return 1;
    }
    return 0;
}

static void log_message(const char *format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (length < 0) return;
    if ((size_t)length >= sizeof(buffer)) length = (int)sizeof(buffer) - 1;

    int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (fd >= 0) {
        dprintf(fd, "<6>xm14-boot-shim: %.*s\n", length, buffer);
        close(fd);
    }
    fd = open(RAM_BOOT_LOG, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd >= 0) {
        dprintf(fd, "xm14-boot-shim: %.*s\n", length, buffer);
        close(fd);
    }
    fd = open(PERSISTENT_BOOT_LOG,
              O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd >= 0) {
        dprintf(fd, "xm14-boot-shim: %.*s\n", length, buffer);
        /* This log is the only evidence recovery can read if a vendor module
         * panics the kernel before OpenWrt userspace starts.  Durability is
         * more important than the few boot-time syncs while diagnosing. */
        fsync(fd);
        close(fd);
    }
    dprintf(STDERR_FILENO, "xm14-boot-shim: %.*s\n", length, buffer);
}

static void make_directory(const char *path, mode_t mode) {
    if (mkdir(path, mode) != 0 && errno != EEXIST)
        log_message("mkdir %s failed: %s", path, strerror(errno));
}

static void persist_ram_boot_log(void) {
    make_directory(PERSISTENT_LOG_DIRECTORY, 0755);
    int source = open(RAM_BOOT_LOG, O_RDONLY | O_CLOEXEC);
    int destination = open(PERSISTENT_BOOT_LOG,
                           O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (source < 0 || destination < 0) {
        if (source >= 0) close(source);
        if (destination >= 0) close(destination);
        return;
    }
    char buffer[4096];
    ssize_t count;
    while ((count = read(source, buffer, sizeof(buffer))) > 0) {
        ssize_t offset = 0;
        while (offset < count) {
            ssize_t written = write(destination, buffer + offset,
                                    (size_t)(count - offset));
            if (written <= 0) break;
            offset += written;
        }
    }
    fsync(destination);
    close(destination);
    close(source);
}

static void start_early_splash(void) {
    if (access(EARLY_SPLASH_PATH, X_OK) != 0) {
        log_message("early DRM splash is unavailable");
        return;
    }
    pid_t child = fork();
    if (child < 0) {
        log_message("start early DRM splash failed: %s", strerror(errno));
        return;
    }
    if (child == 0) {
        execl(EARLY_SPLASH_PATH, "xm14pro-early-splash", NULL);
        _exit(127);
    }
    early_splash_pid = child;
    log_message("started early DRM splash pid=%ld", (long)child);
}

static void stop_early_splash(void) {
    if (early_splash_pid > 1)
        (void)kill(early_splash_pid, SIGTERM);
    early_splash_pid = -1;
}

static void make_character_device(const char *path, mode_t mode,
                                  unsigned int major_number,
                                  unsigned int minor_number) {
    if (mknod(path, S_IFCHR | mode, makedev(major_number, minor_number)) != 0 &&
        errno != EEXIST)
        log_message("mknod %s failed: %s", path, strerror(errno));
}

static bool make_drm_device_from_sysfs(const char *name) {
    char sysfs_path[PATH_MAX];
    char device_path[PATH_MAX];
    unsigned int major_number = 0;
    unsigned int minor_number = 0;
    snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/drm/%s/dev", name);
    snprintf(device_path, sizeof(device_path), "/dev/dri/%s", name);

    FILE *file = NULL;
    for (int attempt = 0; attempt < 100; ++attempt) {
        file = fopen(sysfs_path, "re");
        if (file) break;
        usleep(10000);
    }
    if (!file) {
        log_message("DRM sysfs node %s is unavailable", sysfs_path);
        return false;
    }
    int matched = fscanf(file, "%u:%u", &major_number, &minor_number);
    fclose(file);
    if (matched != 2 || major_number == 0) {
        log_message("DRM sysfs node %s has no usable device number", sysfs_path);
        return false;
    }
    make_directory("/dev/dri", 0755);
    make_character_device(device_path, 0660, major_number, minor_number);
    if (access(device_path, R_OK | W_OK) != 0) {
        log_message("DRM device %s is unavailable after mknod: %s",
                    device_path, strerror(errno));
        return false;
    }
    log_message("prepared early DRM device %s (%u:%u)", device_path,
                major_number, minor_number);
    return true;
}

static void refresh_drm_devices(void) {
    (void)make_drm_device_from_sysfs("card0");
    /* renderD128 is not needed by the splash, but creating it here keeps the
     * first-stage /dev view complete when devtmpfs misses the DRM uevent. */
    (void)make_drm_device_from_sysfs("renderD128");
}

static void mount_early_filesystems(void) {
    make_directory("/dev", 0755);
    make_directory("/proc", 0555);
    make_directory("/sys", 0555);
    make_directory("/run", 0755);
    make_directory("/mnt", 0755);
    make_directory(NEW_ROOT, 0755);

    if (mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID, "mode=0755") != 0 &&
        errno != EBUSY)
        log_message("devtmpfs unavailable; creating the bounded static /dev set");
    make_character_device("/dev/console", 0600, 5, 1);
    make_character_device("/dev/null", 0666, 1, 3);
    make_character_device("/dev/zero", 0666, 1, 5);
    make_character_device("/dev/tty", 0666, 5, 0);
    make_character_device("/dev/kmsg", 0600, 1, 11);
    make_character_device("/dev/random", 0666, 1, 8);
    make_character_device("/dev/urandom", 0666, 1, 9);
    (void)mount("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
    (void)mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
    (void)mount("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755");

    int console = open("/dev/console", O_RDWR | O_CLOEXEC);
    if (console >= 0) {
        dup2(console, STDIN_FILENO);
        dup2(console, STDOUT_FILENO);
        dup2(console, STDERR_FILENO);
        if (console > STDERR_FILENO) close(console);
    }
}

static bool safe_component(const char *value) {
    if (!value || !value[0]) return false;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.'))
            return false;
    }
    return true;
}

static void refresh_block_devices(void) {
    make_directory("/dev/block", 0755);
    make_directory("/dev/block/by-name", 0755);
    DIR *directory = opendir("/sys/class/block");
    if (!directory) return;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char uevent_path[PATH_MAX];
        snprintf(uevent_path, sizeof(uevent_path),
                 "/sys/class/block/%s/uevent", entry->d_name);
        FILE *uevent = fopen(uevent_path, "re");
        if (!uevent) continue;
        char devname[NAME_MAX] = "";
        char partname[NAME_MAX] = "";
        unsigned int major_number = 0;
        unsigned int minor_number = 0;
        char line[PATH_MAX];
        while (fgets(line, sizeof(line), uevent)) {
            if (sscanf(line, "MAJOR=%u", &major_number) == 1) continue;
            if (sscanf(line, "MINOR=%u", &minor_number) == 1) continue;
            if (strncmp(line, "DEVNAME=", 8) == 0)
                sscanf(line + 8, "%255[^\r\n]", devname);
            if (strncmp(line, "PARTNAME=", 9) == 0)
                sscanf(line + 9, "%255[^\r\n]", partname);
        }
        fclose(uevent);
        if (!safe_component(devname) || major_number == 0) continue;
        char device_path[PATH_MAX];
        snprintf(device_path, sizeof(device_path), "/dev/block/%s", devname);
        if (mknod(device_path, S_IFBLK | 0600,
                  makedev(major_number, minor_number)) != 0 && errno != EEXIST)
            continue;
        if (!safe_component(partname)) continue;
        char link_path[PATH_MAX];
        char link_target[PATH_MAX];
        snprintf(link_path, sizeof(link_path), "/dev/block/by-name/%s", partname);
        snprintf(link_target, sizeof(link_target), "../%s", devname);
        if (symlink(link_target, link_path) != 0 && errno != EEXIST)
            log_message("symlink %s failed: %s", partname, strerror(errno));
    }
    closedir(directory);
}

static bool module_seen(const char *path) {
    for (size_t i = 0; i < loaded_module_count; ++i) {
        if (strcmp(loaded_modules[i], path) == 0) return true;
    }
    return false;
}

static bool firmware_consumer(const char *path) {
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    return strcmp(name, "adsp_loader_dlkm.ko") == 0 ||
           strcmp(name, "cdsp-loader.ko") == 0 ||
           strcmp(name, "qca_cld3_kiwi_v2.ko") == 0 ||
           strcmp(name, "cnss2.ko") == 0 ||
           strcmp(name, "icnss2.ko") == 0 ||
           strcmp(name, "wlan_firmware_service.ko") == 0 ||
           strncmp(name, "cnss_", 5) == 0 ||
           strcmp(name, "ipam.ko") == 0 ||
           strcmp(name, "ipanetm.ko") == 0 ||
           strncmp(name, "rmnet_", 6) == 0;
}

static void normalize_module_name(char *name) {
    for (char *character = name; *character; ++character) {
        if (*character == '-') *character = '_';
    }
}

static int load_module_recursive(const char *path, unsigned int depth);

static bool module_path_for_name(const char *requested, char *path,
                                 size_t path_capacity) {
    char wanted[NAME_MAX];
    snprintf(wanted, sizeof(wanted), "%s", requested);
    normalize_module_name(wanted);

    FILE *dependencies = fopen(MODULES_DEP, "re");
    if (!dependencies) return false;
    char *line = NULL;
    size_t capacity = 0;
    bool found = false;
    while (getline(&line, &capacity, dependencies) >= 0) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        const char *filename = strrchr(line, '/');
        filename = filename ? filename + 1 : line;
        char candidate[NAME_MAX];
        snprintf(candidate, sizeof(candidate), "%s", filename);
        char *extension = strstr(candidate, ".ko");
        if (extension) *extension = '\0';
        normalize_module_name(candidate);
        if (strcmp(candidate, wanted) != 0) continue;
        snprintf(path, path_capacity, "%s", line);
        found = true;
        break;
    }
    free(line);
    fclose(dependencies);
    return found;
}

static void load_soft_dependencies(const char *path, bool load_pre,
                                   unsigned int depth) {
    const char *filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;
    char module_name[NAME_MAX];
    snprintf(module_name, sizeof(module_name), "%s", filename);
    char *extension = strstr(module_name, ".ko");
    if (extension) *extension = '\0';
    normalize_module_name(module_name);

    FILE *softdeps = fopen(MODULES_SOFTDEP, "re");
    if (!softdeps) return;
    char *line = NULL;
    size_t capacity = 0;
    while (getline(&line, &capacity, softdeps) >= 0) {
        char *save = NULL;
        char *keyword = strtok_r(line, " \t\r\n", &save);
        char *owner = strtok_r(NULL, " \t\r\n", &save);
        if (!keyword || !owner || strcmp(keyword, "softdep") != 0) continue;
        normalize_module_name(owner);
        if (strcmp(owner, module_name) != 0) continue;

        enum { SOFTDEP_NONE, SOFTDEP_PRE, SOFTDEP_POST } section = SOFTDEP_NONE;
        for (char *token = strtok_r(NULL, " \t\r\n", &save); token;
             token = strtok_r(NULL, " \t\r\n", &save)) {
            if (strcmp(token, "pre:") == 0) {
                section = SOFTDEP_PRE;
                continue;
            }
            if (strcmp(token, "post:") == 0) {
                section = SOFTDEP_POST;
                continue;
            }
            if ((load_pre && section != SOFTDEP_PRE) ||
                (!load_pre && section != SOFTDEP_POST) ||
                !safe_component(token))
                continue;
            char dependency_path[PATH_MAX];
            if (!module_path_for_name(token, dependency_path,
                                      sizeof(dependency_path))) {
                log_message("soft dependency %s for %s is unavailable", token,
                            path);
                continue;
            }
            if (strcmp(dependency_path, path) == 0) continue;
            if (load_module_recursive(dependency_path, depth + 1) != 0)
                log_message("soft dependency %s for %s failed", dependency_path,
                            path);
        }
    }
    free(line);
    fclose(softdeps);
}

static void module_boot_options(const char *path, char *options,
                                size_t options_capacity) {
    options[0] = '\0';
    const char *filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;
    char module_name[NAME_MAX];
    snprintf(module_name, sizeof(module_name), "%s", filename);
    char *extension = strstr(module_name, ".ko");
    if (extension) *extension = '\0';
    normalize_module_name(module_name);

    FILE *cmdline = fopen("/proc/cmdline", "re");
    if (!cmdline) return;
    char *line = NULL;
    size_t capacity = 0;
    if (getline(&line, &capacity, cmdline) < 0) {
        free(line);
        fclose(cmdline);
        return;
    }
    fclose(cmdline);

    char *save = NULL;
    for (char *token = strtok_r(line, " \t\r\n", &save); token;
         token = strtok_r(NULL, " \t\r\n", &save)) {
        char *dot = strchr(token, '.');
        if (!dot || dot == token || !dot[1]) continue;
        *dot = '\0';
        normalize_module_name(token);
        if (strcmp(token, module_name) != 0) continue;

        const char *argument = dot + 1;
        size_t used = strlen(options);
        size_t argument_length = strlen(argument);
        size_t separator = used ? 1 : 0;
        if (used + separator + argument_length + 1 > options_capacity) {
            log_message("boot parameters for %s exceed the bounded buffer", path);
            break;
        }
        if (separator) options[used++] = ' ';
        memcpy(options + used, argument, argument_length + 1);
    }
    free(line);
}

static int load_module_recursive(const char *path, unsigned int depth) {
    if (depth > 32 || !path || strncmp(path, MODULE_DIRECTORY "/", 13) != 0)
        return -1;
    if (defer_firmware_consumers && firmware_consumer(path)) return -1;
    if (module_seen(path)) return 0;
    if (loaded_module_count >= MAX_MODULES) return -1;

    FILE *dependencies = fopen(MODULES_DEP, "re");
    if (dependencies) {
        char *line = NULL;
        size_t capacity = 0;
        while (getline(&line, &capacity, dependencies) >= 0) {
            char *colon = strchr(line, ':');
            if (!colon) continue;
            *colon = '\0';
            if (strcmp(line, path) != 0) continue;
            char *save = NULL;
            char *dependency_paths[MAX_MODULE_DEPENDENCIES];
            size_t dependency_count = 0;
            for (char *dependency = strtok_r(colon + 1, " \t\r\n", &save);
                 dependency; dependency = strtok_r(NULL, " \t\r\n", &save)) {
                if (dependency_count >= MAX_MODULE_DEPENDENCIES) {
                    log_message("dependency list for %s exceeds %u entries",
                                path, MAX_MODULE_DEPENDENCIES);
                    break;
                }
                dependency_paths[dependency_count++] = dependency;
            }
            /* Android 14 libmodprobe walks modules.dep from the final entry
             * back to the first.  The generated Xiaomi list is a flattened
             * dependency closure, not a direct-dependency list, so forward
             * recursion changes the hardware probe order. */
            while (dependency_count > 0) {
                const char *dependency =
                    dependency_paths[--dependency_count];
                if (load_module_recursive(dependency, depth + 1) != 0)
                    log_message("dependency %s for %s failed", dependency,
                                path);
            }
            break;
        }
        free(line);
        fclose(dependencies);
    }

    /* This is the same position used by Android's InsmodWithDeps(): hard
     * dependencies first, then soft pre-dependencies, then the target and
     * finally soft post-dependencies. */
    load_soft_dependencies(path, true, depth);

    /* UFS is itself loaded from this list. Its partitions are unavailable
     * during the initial /dev scan, so publish them immediately before
     * block2mtd opens the stock panic-log partition. */
    if (strcmp(path, MODULE_DIRECTORY "/block2mtd.ko") == 0) {
        for (unsigned int attempt = 0; attempt < 20; ++attempt) {
            refresh_block_devices();
            if (access("/dev/block/by-name/oops", R_OK) == 0) break;
            usleep(100000);
        }
        if (access("/dev/block/by-name/oops", R_OK) != 0)
            log_message("oops partition unavailable before block2mtd");
    }

    int module = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (module < 0) {
        log_message("open module %s failed: %s", path, strerror(errno));
        return -1;
    }
    char options[PATH_MAX];
    module_boot_options(path, options, sizeof(options));
    if (options[0])
        log_message("loading %s with boot parameters: %s", path, options);
    int result = (int)syscall(SYS_finit_module, module, options, 0);
    int saved_errno = errno;
    close(module);
    if (result != 0 && saved_errno != EEXIST) {
        log_message("load module %s failed: %s", path, strerror(saved_errno));
        return -1;
    }
    snprintf(loaded_modules[loaded_module_count++], NAME_MAX, "%s", path);
    load_soft_dependencies(path, false, depth);
    return 0;
}

static void load_vendor_module_list(const char *module_list,
                                    unsigned int *requested,
                                    unsigned int *failed) {
    FILE *modules = fopen(module_list, "re");
    if (!modules) {
        log_message("%s is unavailable", module_list);
        return;
    }
    char line[NAME_MAX];
    while (fgets(line, sizeof(line), modules)) {
        trim(line);
        if (!line[0] || line[0] == '#' || !safe_component(line)) continue;
        char path[sizeof(MODULE_DIRECTORY) + NAME_MAX + 1];
        snprintf(path, sizeof(path), MODULE_DIRECTORY "/%s", line);
        ++*requested;
        if (load_module_recursive(path, 0) != 0) {
            ++*failed;
            continue;
        }
    }
    fclose(modules);
}

static void load_vendor_modules(void) {
    unsigned int requested = 0;
    unsigned int failed = 0;
    const char *module_list = access(MODULES_LOAD_RECOVERY, R_OK) == 0 ?
        MODULES_LOAD_RECOVERY : MODULES_LOAD;

    /* Android first-stage init selects exactly one list for the boot mode.
     * modules.load.recovery already contains the base modules; loading
     * modules.load first changes the original Xiaomi probe order. */
    load_vendor_module_list(module_list, &requested, &failed);
    log_message("vendor module load complete from %s: requested=%u failed=%u total=%zu",
                module_list, requested, failed, loaded_module_count);
}

static void enable_stock_firmware_fallback(void) {
    static const char path[] =
        "/proc/sys/kernel/firmware_config/force_sysfs_fallback";
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        log_message("stock firmware fallback node unavailable: %s",
                    strerror(errno));
        return;
    }
    const char enabled[] = "1\n";
    ssize_t written = write(fd, enabled, sizeof(enabled) - 1);
    int saved_errno = errno;
    close(fd);
    if (written == (ssize_t)(sizeof(enabled) - 1))
        log_message("enabled stock firmware sysfs fallback");
    else
        log_message("enable stock firmware fallback failed: %s",
                    strerror(saved_errno));
}

static bool wait_for_path(const char *path) {
    for (int second = 0; second < WAIT_SECONDS; ++second) {
        refresh_block_devices();
        if (access(path, R_OK) == 0) return true;
        sleep(1);
    }
    return false;
}

static bool boot_slot_is_a(void) {
    FILE *cmdline = fopen("/proc/cmdline", "re");
    if (!cmdline) return false;
    char *line = NULL;
    size_t capacity = 0;
    bool slot_a = false;
    if (getline(&line, &capacity, cmdline) >= 0 &&
        (strstr(line, "androidboot.slot_suffix=_a") ||
         strstr(line, "androidboot.slot=a")))
        slot_a = true;
    free(line);
    fclose(cmdline);
    return slot_a;
}

static bool mount_stock_partition(const char *base_name, const char *target,
                                  const char *filesystem, const char *options) {
    char first[PATH_MAX];
    char second[PATH_MAX];
    bool slot_a = boot_slot_is_a();
    snprintf(first, sizeof(first), "/dev/block/by-name/%s_%c", base_name,
             slot_a ? 'a' : 'b');
    snprintf(second, sizeof(second), "/dev/block/by-name/%s_%c", base_name,
             slot_a ? 'b' : 'a');

    const char *candidates[] = {first, second};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (!wait_for_path(candidates[i])) continue;
        if (mount(candidates[i], target, filesystem,
                  MS_RDONLY | MS_NODEV | MS_NOSUID, options) == 0) {
            log_message("mounted stock %s at %s", candidates[i], target);
            return true;
        }
        log_message("mount %s at %s failed: %s", candidates[i], target,
                    strerror(errno));
    }
    return false;
}

static void mount_platform_firmware(void) {
    make_directory("/vendor", 0755);
    make_directory(FIRMWARE_MOUNT, 0755);
    make_directory(FIRMWARE_BIND, 0755);
    make_directory(MODEM_FIRMWARE_MOUNT, 0755);
    make_directory(DSP_MOUNT, 0755);
    make_directory(BT_FIRMWARE_MOUNT, 0755);

    if (mount_stock_partition("modem", FIRMWARE_MOUNT, "vfat",
                              "shortname=lower")) {
        if (mount(FIRMWARE_MOUNT "/image", FIRMWARE_BIND, NULL,
                  MS_BIND | MS_REC, NULL) == 0)
            log_message("bound %s/image at %s like stock recovery",
                        FIRMWARE_MOUNT, FIRMWARE_BIND);
        else
            log_message("bind stock firmware image at %s failed: %s",
                        FIRMWARE_BIND, strerror(errno));
    }
    (void)mount_stock_partition("modemfirmware", MODEM_FIRMWARE_MOUNT,
                                "vfat", "shortname=lower");
    (void)mount_stock_partition("dsp", DSP_MOUNT, "ext4",
                                "errors=remount-ro");
    (void)mount_stock_partition("bluetooth", BT_FIRMWARE_MOUNT, "vfat",
                                "shortname=lower");
}

static void carry_platform_mount(const char *source, const char *root) {
    char target[PATH_MAX];
    snprintf(target, sizeof(target), "%s%s", root, source);
    char parent[PATH_MAX];
    snprintf(parent, sizeof(parent), "%s/vendor", root);
    make_directory(parent, 0755);
    make_directory(target, 0755);
    if (mount(source, target, NULL, MS_BIND | MS_REC, NULL) != 0)
        log_message("bind %s into nwrt root failed: %s", source,
                    strerror(errno));
}

static void trim(char *value) {
    char *newline = strpbrk(value, "\r\n");
    if (newline) *newline = '\0';
}

static int switch_to_root(const char *root, const char *mode) {
    char init_path[256];
    snprintf(init_path, sizeof(init_path), "%s/sbin/init", root);
    if (access(init_path, X_OK) != 0) {
        log_message("%s root has no executable /sbin/init", mode);
        return -1;
    }

    make_directory("/oldroot", 0755);
    if (mount(root, root, NULL, MS_BIND | MS_REC, NULL) != 0) {
        log_message("bind %s failed: %s", root, strerror(errno));
        return -1;
    }
    const char *early_mounts[] = {"dev", "proc", "sys", "run"};
    for (size_t i = 0; i < sizeof(early_mounts) / sizeof(early_mounts[0]); ++i) {
        char source[32];
        char target[256];
        snprintf(source, sizeof(source), "/%s", early_mounts[i]);
        snprintf(target, sizeof(target), "%s/%s", root, early_mounts[i]);
        make_directory(target, 0755);
        if (mount(source, target, NULL, MS_BIND | MS_REC, NULL) != 0) {
            log_message("bind %s into %s failed: %s", source, mode, strerror(errno));
            return -1;
        }
    }
    if (chdir(root) != 0 || chroot(".") != 0 || chdir("/") != 0) {
        log_message("chroot %s failed: %s", root, strerror(errno));
        return -1;
    }
    setenv("XM14_BOOT_MODE", mode, 1);
    /* The final nwrt display service owns DRM after userspace starts.  Do not
     * leave the initramfs splash process holding the display indefinitely. */
    stop_early_splash();
    execl("/sbin/init", "init", NULL);
    log_message("exec %s init failed: %s", mode, strerror(errno));
    return -1;
}

static int boot_persistent_system(void) {
    if (!wait_for_path(ROOT_DEVICE)) {
        log_message("linux_root is absent; persistent boot unavailable");
        return -1;
    }
    if (!verify_root_device_geometry())
        return -1;
    if (mount(ROOT_DEVICE, NEW_ROOT, "ext4", MS_NODEV | MS_NOSUID,
              "errors=remount-ro") != 0) {
        log_message("mount linux_root failed: %s", strerror(errno));
        return -1;
    }
    persist_ram_boot_log();
    log_message("persistent linux_root log is available");
    carry_platform_mount(FIRMWARE_MOUNT, NEW_ROOT);
    carry_platform_mount(FIRMWARE_BIND, NEW_ROOT);
    carry_platform_mount(MODEM_FIRMWARE_MOUNT, NEW_ROOT);
    carry_platform_mount(DSP_MOUNT, NEW_ROOT);
    carry_platform_mount(BT_FIRMWARE_MOUNT, NEW_ROOT);
    log_message("starting persistent nwrt from linux_root");
    return switch_to_root(NEW_ROOT, "linux-root");
}

static void reap_children(int signal_number) {
    (void)signal_number;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--reboot-bootloader") == 0)
        return reboot_bootloader();
    if (argc != 1) {
        fprintf(stderr, "usage: %s [--reboot-bootloader]\n", argv[0]);
        return 2;
    }
    if (getpid() != 1) {
        fprintf(stderr, "xm14pro-boot-shim must run as PID 1\n");
        return 2;
    }
    signal(SIGCHLD, reap_children);
    mount_early_filesystems();
    log_message("starting; B slot uses the independent linux_root filesystem");
    defer_firmware_consumers = true;
    /* Match Android recovery first-stage semantics: select only the recovery
     * list and honour its dependency order.  Synaptics touch and the ADSP
     * vendor_dlkm loader remain outside this early phase. */
    load_vendor_modules();
    refresh_block_devices();
    refresh_drm_devices();
    start_early_splash();
    enable_stock_firmware_fallback();
    mount_platform_firmware();
    defer_firmware_consumers = false;
    refresh_block_devices();

    if (boot_persistent_system() == 0) return 0;
    stop_early_splash();
    log_message("falling back to embedded rescue rootfs");
    if (switch_to_root(RESCUE_ROOT, "rescue") == 0) return 0;

    log_message("no usable rootfs; stopping without formatting storage");
    for (;;) pause();
}
