// SPDX-License-Identifier: Apache-2.0
// NWRT's framework-side owner of the stock Android SystemSuspend interfaces.
// Android/Xiaomi services and kernel wakeup sources remain authoritative.
#include <android/binder_auto_utils.h>
#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_parcel.h>
#include <android/binder_process.h>
#include <android/binder_stability.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
#include <poll.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

struct ProcessStatePointer {
    void *pointer;
    ~ProcessStatePointer() { pointer = nullptr; }
};
extern ProcessStatePointer init_process_state(const char *)
    __asm__("_ZN7android12ProcessState14initWithDriverEPKc");

static volatile sig_atomic_t running = 1;
static std::atomic<bool> service_failed{false};
static void stop(int) { running = 0; }
static void service_died(void *) { service_failed.store(true); }
static void *create(void *p) { return p; }
static void destroy(void *) {}
static binder_status_t unused(AIBinder *, transaction_code_t, const AParcel *, AParcel *)
{ return STATUS_UNKNOWN_TRANSACTION; }

static uint64_t boot_millis()
{
    timespec now{};
    clock_gettime(CLOCK_BOOTTIME, &now);
    return uint64_t(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

static bool read_text(const char *path, char *value, size_t length)
{
    FILE *file = std::fopen(path, "r");
    if (!file) return false;
    const bool ok = std::fgets(value, length, file) != nullptr;
    std::fclose(file);
    return ok;
}

static ndk::SpAIBinder service(const char *name, AIBinder_Class *cls)
{
    ndk::SpAIBinder binder(AServiceManager_checkService(name));
    if (!binder.get() || !AIBinder_associateClass(binder.get(), cls))
        return ndk::SpAIBinder();
    return binder;
}

static bool call(AIBinder *binder, AParcel **in, AParcel **out)
{
    binder_status_t result = AIBinder_transact(binder, 1, in, out, FLAG_PRIVATE_LOCAL);
    ndk::ScopedAStatus status;
    if (result == STATUS_OK)
        result = AParcel_readStatusHeader(*out, status.getR());
    if (result != STATUS_OK || !status.isOk()) {
        std::fprintf(stderr, "SystemSuspend call failed: %d, %s\n", result,
            status.get() ? status.getDescription().c_str() : "no reply");
        return false;
    }
    return true;
}

static bool acquire_lock(AIBinder *suspend, ndk::SpAIBinder &lock)
{
    ndk::ScopedAParcel in, out;
    if (AIBinder_prepareTransaction(suspend, in.getR()) != STATUS_OK ||
        AParcel_writeInt32(in.get(), 0) != STATUS_OK ||
        AParcel_writeString(in.get(), "NWRT.PowerPolicy", 16) != STATUS_OK ||
        !call(suspend, in.getR(), out.getR())) return false;
    return AParcel_readStrongBinder(out.get(), lock.getR()) == STATUS_OK && lock.get();
}

static bool release_lock(ndk::SpAIBinder &lock, AIBinder_Class *cls)
{
    if (!lock.get()) return true;
    ndk::ScopedAParcel in, out;
    if (!AIBinder_associateClass(lock.get(), cls) ||
        AIBinder_prepareTransaction(lock.get(), in.getR()) != STATUS_OK ||
        AIBinder_transact(lock.get(), 1, in.getR(), out.getR(),
            FLAG_PRIVATE_LOCAL | FLAG_ONEWAY) != STATUS_OK) return false;
    lock.set(nullptr);
    return true;
}

static bool display_awake()
{
    char value[48]{};
    // Unknown panel state keeps the device awake rather than freezing its UI.
    return !read_text("/sys/class/backlight/panel0-backlight/brightness", value, sizeof(value)) ||
        std::strtol(value, nullptr, 10) > 0;
}

static bool usb_active()
{
    glob_t paths{};
    if (glob("/sys/class/udc/*/state", 0, nullptr, &paths) != 0) {
        globfree(&paths);
        return true;
    }
    bool active = false;
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        char value[64]{};
        if (!read_text(paths.gl_pathv[i], value, sizeof(value)) ||
            std::strncmp(value, "not attached", 12) != 0) active = true;
    }
    globfree(&paths);
    return active;
}

static bool audio_active()
{
    glob_t paths{};
    glob("/proc/asound/card*/pcm*/sub*/status", 0, nullptr, &paths);
    bool active = false;
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        char value[96]{};
        if (read_text(paths.gl_pathv[i], value, sizeof(value)) &&
            (std::strstr(value, "RUNNING") || std::strstr(value, "DRAINING"))) active = true;
    }
    globfree(&paths);
    return active;
}

static bool wifi_ready()
{
    char current_boot[80]{};
    if (!read_text("/proc/sys/kernel/random/boot_id", current_boot, sizeof(current_boot)))
        return false;
    current_boot[std::strcspn(current_boot, "\r\n")] = '\0';
    glob_t paths{};
    if (glob("/sys/class/ieee80211/phy*", 0, nullptr, &paths) != 0) {
        globfree(&paths);
        return false;
    }
    bool ready = paths.gl_pathc > 0;
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        const char *phy = std::strrchr(paths.gl_pathv[i], '/') + 1;
        char path[192], boot[80]{};
        unsigned long long inode = 0;
        struct stat state{};
        std::snprintf(path, sizeof(path), "/data/vendor/nwrt-power/%s.ready", phy);
        FILE *file = std::fopen(path, "r");
        const bool valid = file && std::fscanf(file, "%79s %llu", boot, &inode) == 2 &&
            stat(paths.gl_pathv[i], &state) == 0 &&
            inode == static_cast<unsigned long long>(state.st_ino) &&
            std::strcmp(boot, current_boot) == 0;
        if (file) std::fclose(file);
        if (!valid) ready = false;
    }
    globfree(&paths);
    return ready;
}

static uint64_t traffic_bytes()
{
    DIR *directory = opendir("/sys/class/net");
    if (!directory) return 0;
    uint64_t bytes = 0;
    for (dirent *entry; (entry = readdir(directory));) {
        if (std::strcmp(entry->d_name, "nwrt-ap") &&
            std::strncmp(entry->d_name, "rmnet_data", 10) &&
            std::strcmp(entry->d_name, "wlan1")) continue;
        for (const char *counter : {"rx_bytes", "tx_bytes"}) {
            char path[384], value[64]{};
            std::snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/%s", entry->d_name, counter);
            if (read_text(path, value, sizeof(value))) bytes += std::strtoull(value, nullptr, 10);
        }
    }
    closedir(directory);
    return bytes;
}

static std::vector<pollfd> power_keys()
{
    std::vector<pollfd> result;
    glob_t paths{};
    glob("/dev/input/event*", 0, nullptr, &paths);
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        int fd = open(paths.gl_pathv[i], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        unsigned long bits[(KEY_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))]{};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) >= 0 &&
            (bits[KEY_POWER / (8 * sizeof(unsigned long))] &
                (1UL << (KEY_POWER % (8 * sizeof(unsigned long))))))
            result.push_back({fd, POLLIN, 0});
        else close(fd);
    }
    globfree(&paths);
    return result;
}

int main(int argc, char **argv)
{
    const bool check = argc == 2 && std::strcmp(argv[1], "--check") == 0;
    const bool run = argc == 2 && std::strcmp(argv[1], "--run") == 0;
    if (!check && !run) {
        std::fprintf(stderr, "usage: nwrt-power-policy --check | --run\n");
        return 2;
    }
    auto state = init_process_state("/dev/binder");
    if (!state.pointer) return 2;
    ABinderProcess_setThreadPoolMaxThreadCount(1);
    ABinderProcess_startThreadPool();
    auto *control_cls = AIBinder_Class_define(
        "android.system.suspend.internal.ISuspendControlServiceInternal", create, destroy, unused);
    auto *suspend_cls = AIBinder_Class_define(
        "android.system.suspend.ISystemSuspend", create, destroy, unused);
    auto *lock_cls = AIBinder_Class_define("android.system.suspend.IWakeLock", create, destroy, unused);
    auto *token_cls = AIBinder_Class_define("nwrt.power.SuspendToken", create, destroy, unused);
    if (!control_cls || !suspend_cls || !lock_cls || !token_cls) return 2;
    auto control = service("suspend_control_internal", control_cls);
    auto suspend = service("android.system.suspend.ISystemSuspend/default", suspend_cls);
    if (!control.get() || !suspend.get()) {
        std::fprintf(stderr, "stock SystemSuspend service unavailable\n");
        return 2;
    }
    ndk::SpAIBinder lock;
    if (!acquire_lock(suspend.get(), lock)) return 2;
    if (check) {
        if (!release_lock(lock, lock_cls)) return 2;
        std::printf("SystemSuspend ready; display_awake=%d usb_active=%d audio_active=%d wifi_ready=%d\n",
            display_awake(), usb_active(), audio_active(), wifi_ready());
        return 0;
    }
    ndk::ScopedAIBinder_DeathRecipient death(AIBinder_DeathRecipient_new(service_died));
    const binder_status_t death_result = death.get() ?
        AIBinder_linkToDeath(control.get(), death.get(), nullptr) : STATUS_NO_MEMORY;
    if (death_result != STATUS_OK) {
        std::fprintf(stderr, "SystemSuspend death notification failed: %d\n", death_result);
        return 2;
    }
    auto keys = power_keys();
    if (keys.empty()) {
        std::fprintf(stderr, "power-key input unavailable; autosuspend not enabled\n");
        return 2;
    }
    ndk::SpAIBinder token(AIBinder_new(token_cls, nullptr));
    ndk::ScopedAParcel in, out;
    if (!token.get() || AIBinder_prepareTransaction(control.get(), in.getR()) != STATUS_OK ||
        AParcel_writeStrongBinder(in.get(), token.get()) != STATUS_OK ||
        !call(control.get(), in.getR(), out.getR())) return 2;
    bool enabled = false;
    const binder_status_t enable_result = AParcel_readBool(out.get(), &enabled);
    if (enable_result != STATUS_OK) {
        std::fprintf(stderr, "SystemSuspend registration failed: %d enabled=%d\n", enable_result, enabled);
        return 2;
    }
    // The stock implementation registers the token first, then returns false
    // when its suspend thread was already started by an earlier owner. A valid
    // false reply therefore still leaves this token registered and usable.
    struct sigaction action{};
    action.sa_handler = stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
    uint64_t awake_until = boot_millis() + 30000;
    uint64_t last_sample = boot_millis(), previous_bytes = traffic_bytes();
    std::puts("NWRT automatic suspend policy ready");
    std::fflush(stdout);
    while (running && !service_failed.load()) {
        if (poll(keys.data(), keys.size(), 1000) < 0 && errno != EINTR) return 2;
        uint64_t now = boot_millis();
        for (auto &key : keys) {
            if (key.revents & (POLLERR | POLLHUP | POLLNVAL)) return 2;
            input_event event{};
            while (read(key.fd, &event, sizeof(event)) == sizeof(event)) {
                if (event.type == EV_KEY && event.code == KEY_POWER && event.value == 1)
                    awake_until = std::max(awake_until, now + 5000);
            }
        }
        const uint64_t bytes = traffic_bytes();
        const uint64_t elapsed = std::max<uint64_t>(1, now - last_sample);
        if (bytes >= previous_bytes && (bytes - previous_bytes) / elapsed >= 32)
            awake_until = std::max(awake_until, now + 5000);
        previous_bytes = bytes; last_sample = now;
        const bool busy = now < awake_until || display_awake() || usb_active() ||
            audio_active() || !wifi_ready();
        if (busy && !lock.get() && !acquire_lock(suspend.get(), lock)) return 2;
        if (!busy && !release_lock(lock, lock_cls)) return 2;
    }
    for (const auto &key : keys) close(key.fd);
    // Binder token death removes this owner's autosuspend registration. Never
    // use forceSuspend or bypass the radio, Wi-Fi, USB and kernel wake locks.
    return running ? 2 : 0;
}
