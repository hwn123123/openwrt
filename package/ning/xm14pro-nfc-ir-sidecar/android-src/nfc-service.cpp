// SPDX-License-Identifier: Apache-2.0

#include <android/binder_process.h>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#include "nfa_api.h"

// NXP's SN220 branch widens this argument relative to the public AOSP header.
extern tNFA_STATUS NFA_EnablePolling(uint16_t technology_mask);
extern tNFA_STATUS NFA_CeConfigureLocalTag(uint8_t protocol_mask,
                                           uint8_t *ndef_data,
                                           uint16_t current_size,
                                           uint16_t maximum_size,
                                           bool read_only,
                                           uint8_t uid_length,
                                           uint8_t *uid);
extern tNFA_STATUS NFA_RwReadNDef();
extern tNFA_STATUS NFA_RwWriteNDef(uint8_t *data, uint32_t length);

struct LeakedStrongPointer {
    void *pointer;
    ~LeakedStrongPointer() { pointer = nullptr; }
};

extern "C" LeakedStrongPointer init_process_state(const char *driver)
    __asm__("_ZN7android12ProcessState14initWithDriverEPKc");

class NfcAdaptation {
public:
    static NfcAdaptation &GetInstance();
    void Initialize();
    void Finalize();
    tHAL_NFC_ENTRY *GetHalEntryFuncs();
};

enum class ServiceMode { share, copy };
enum class WaitFor {
    none,
    enabled,
    polling,
    listening,
    local_tag,
    discovery,
    discovery_stopped,
    polling_stopped,
    listening_stopped,
    disabled,
};

static constexpr const char *kConfigPath =
    "/data/vendor/nfc/xm14/hotspot.conf";
static constexpr const char *kStatusPath =
    "/data/vendor/nfc/xm14/status.json";
static constexpr const char *kCopyPath =
    "/data/vendor/nfc/xm14/copy.ndef";
static constexpr const char *kMctDumpPath =
    "/data/vendor/nfc/xm14/mct-source.dump";

static pthread_mutex_t event_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t event_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t raw_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t raw_cond = PTHREAD_COND_INITIALIZER;
static WaitFor expected_event = WaitFor::none;
static tNFA_STATUS event_status = NFA_STATUS_FAILED;
static volatile sig_atomic_t stop_requested;
static ServiceMode service_mode = ServiceMode::share;
static std::vector<uint8_t> share_message;
static std::vector<uint8_t> copy_message;
static bool read_pending;
static bool write_pending;
static bool raw_waiting;
static tNFA_STATUS raw_status = NFA_STATUS_FAILED;
static std::vector<uint8_t> raw_response;
static bool tag_active;
static bool classic_active;
static bool classic_worker_running;
static std::array<uint8_t, 10> current_uid{};
static size_t current_uid_length;
static uint8_t current_sak;
static std::array<uint8_t, 10> source_uid{};
static size_t source_uid_length;

struct ClassicDump {
    bool valid = false;
    int sector_count = 0;
    int block_count = 0;
    std::array<uint8_t, 10> uid{};
    size_t uid_length = 0;
    std::vector<std::array<uint8_t, 16>> blocks;
    std::vector<bool> readable;
    std::vector<std::array<uint8_t, 6>> key_a;
    std::vector<std::array<uint8_t, 6>> key_b;
    std::vector<bool> has_key_a;
    std::vector<bool> has_key_b;
};

static ClassicDump classic_dump;

static size_t source_byte_count() {
    if (!copy_message.empty()) return copy_message.size();
    size_t count = 0;
    for (bool readable : classic_dump.readable)
        if (readable) count += 16;
    return count;
}

static void write_status(const char *stage, bool ok, const char *detail) {
    char temporary[256];
    std::snprintf(temporary, sizeof(temporary), "%s.%ld", kStatusPath,
                  static_cast<long>(getpid()));
    FILE *file = std::fopen(temporary, "we");
    if (!file) return;
    // stage/detail are fixed internal strings; hotspot credentials are never
    // written to status or logs.
    std::fprintf(file,
                 "{\"ok\":%s,\"mode\":\"%s\",\"stage\":\"%s\","
                 "\"detail\":\"%s\",\"source_bytes\":%zu}\n",
                 ok ? "true" : "false",
                 service_mode == ServiceMode::share ? "share" : "copy",
                 stage, detail ? detail : "", source_byte_count());
    if (std::fclose(file) == 0)
        rename(temporary, kStatusPath);
    else
        unlink(temporary);
}

static bool save_copy_message() {
    char temporary[256];
    std::snprintf(temporary, sizeof(temporary), "%s.%ld", kCopyPath,
                  static_cast<long>(getpid()));
    FILE *file = std::fopen(temporary, "we");
    if (!file) return false;
    bool ok = copy_message.empty() ||
              std::fwrite(copy_message.data(), 1, copy_message.size(), file) ==
                  copy_message.size();
    if (std::fclose(file) != 0) ok = false;
    if (ok) ok = rename(temporary, kCopyPath) == 0;
    if (!ok) unlink(temporary);
    return ok;
}

static void append_be16(std::vector<uint8_t> &buffer, uint16_t value) {
    buffer.push_back(static_cast<uint8_t>(value >> 8));
    buffer.push_back(static_cast<uint8_t>(value));
}

static void append_attribute(std::vector<uint8_t> &buffer, uint16_t id,
                             const uint8_t *value, size_t length) {
    append_be16(buffer, id);
    append_be16(buffer, static_cast<uint16_t>(length));
    buffer.insert(buffer.end(), value, value + length);
}

static void append_attribute_string(std::vector<uint8_t> &buffer, uint16_t id,
                                    const std::string &value) {
    append_attribute(buffer, id,
                     reinterpret_cast<const uint8_t *>(value.data()),
                     value.size());
}

static bool read_line(FILE *file, std::string &value, size_t maximum) {
    char buffer[256];
    if (!std::fgets(buffer, sizeof(buffer), file)) return false;
    size_t length = std::strcspn(buffer, "\r\n");
    if (length > maximum) return false;
    value.assign(buffer, length);
    return true;
}

static bool make_hotspot_ndef(std::vector<uint8_t> &message) {
    std::string ssid, key, encryption;
    FILE *file = std::fopen(kConfigPath, "re");
    if (!file) return false;
    bool valid = read_line(file, ssid, 32) && read_line(file, key, 63) &&
                 read_line(file, encryption, 64);
    std::fclose(file);
    if (!valid || ssid.empty() || key.size() < 8) return false;

    std::vector<uint8_t> credential;
    const uint8_t network_index = 1;
    const uint8_t mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    const uint8_t auth[2] = {0x00, 0x20};  // WPA2-Personal
    const uint8_t cipher[2] = {0x00, 0x08};  // AES
    append_attribute(credential, 0x1026, &network_index, 1);
    append_attribute_string(credential, 0x1045, ssid);
    append_attribute(credential, 0x1003, auth, sizeof(auth));
    append_attribute(credential, 0x100f, cipher, sizeof(cipher));
    append_attribute_string(credential, 0x1027, key);
    append_attribute(credential, 0x1020, mac, sizeof(mac));

    std::vector<uint8_t> wsc;
    append_be16(wsc, 0x100e);  // Credential
    append_be16(wsc, static_cast<uint16_t>(credential.size()));
    wsc.insert(wsc.end(), credential.begin(), credential.end());

    static constexpr char kMime[] = "application/vnd.wfa.wsc";
    if (wsc.size() > 255) return false;
    message.clear();
    message.push_back(0xd2);  // MB | ME | SR | TNF_MIME_MEDIA
    message.push_back(static_cast<uint8_t>(sizeof(kMime) - 1));
    message.push_back(static_cast<uint8_t>(wsc.size()));
    message.insert(message.end(), kMime, kMime + sizeof(kMime) - 1);
    message.insert(message.end(), wsc.begin(), wsc.end());
    return true;
}

static void *binder_thread_main(void *) {
    ABinderProcess_joinThreadPool();
    return nullptr;
}

static void notify(WaitFor event, tNFA_STATUS status) {
    pthread_mutex_lock(&event_lock);
    if (expected_event == event) {
        event_status = status;
        expected_event = WaitFor::none;
        pthread_cond_signal(&event_cond);
    }
    pthread_mutex_unlock(&event_lock);
}

static void device_management_callback(uint8_t event,
                                       tNFA_DM_CBACK_DATA *data) {
    if (event == NFA_DM_ENABLE_EVT)
        notify(WaitFor::enabled, data ? data->status : NFA_STATUS_FAILED);
    else if (event == NFA_DM_DISABLE_EVT)
        notify(WaitFor::disabled, data ? data->status : NFA_STATUS_FAILED);
}

static bool same_uid(const std::array<uint8_t, 10> &left, size_t left_length,
                     const std::array<uint8_t, 10> &right,
                     size_t right_length) {
    return left_length && left_length == right_length &&
           !std::memcmp(left.data(), right.data(), left_length);
}

static bool raw_transceive(const uint8_t *command, size_t command_length,
                           std::vector<uint8_t> &response,
                           unsigned timeout_seconds = 3) {
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_seconds;
    pthread_mutex_lock(&raw_lock);
    if (!tag_active || raw_waiting || command_length > UINT16_MAX) {
        pthread_mutex_unlock(&raw_lock);
        return false;
    }
    raw_response.clear();
    raw_status = NFA_STATUS_FAILED;
    raw_waiting = true;
    tNFA_STATUS sent = NFA_SendRawFrame(
        const_cast<uint8_t *>(command), static_cast<uint16_t>(command_length),
        1000);
    if (sent != NFA_STATUS_OK) raw_waiting = false;
    while (raw_waiting) {
        int result = pthread_cond_timedwait(&raw_cond, &raw_lock, &deadline);
        if (result == ETIMEDOUT) {
            raw_waiting = false;
            break;
        }
    }
    bool ok = raw_status == NFA_STATUS_OK && !raw_response.empty();
    if (ok) response = raw_response;
    pthread_mutex_unlock(&raw_lock);
    return ok;
}

static bool classic_authenticate(int block, const std::array<uint8_t, 6> &key,
                                 bool key_b,
                                 const std::array<uint8_t, 10> &uid,
                                 size_t uid_length) {
    if (uid_length < 4 || block < 0 || block > 255) return false;
    uint8_t command[12] = {static_cast<uint8_t>(key_b ? 0x61 : 0x60),
                           static_cast<uint8_t>(block)};
    std::memcpy(command + 2, key.data(), key.size());
    std::memcpy(command + 8, uid.data() + uid_length - 4, 4);
    std::vector<uint8_t> response;
    return raw_transceive(command, sizeof(command), response);
}

static bool classic_read_block(int block, std::array<uint8_t, 16> &data) {
    uint8_t command[2] = {0x30, static_cast<uint8_t>(block)};
    std::vector<uint8_t> response;
    if (!raw_transceive(command, sizeof(command), response) ||
        response.size() < data.size())
        return false;
    std::memcpy(data.data(), response.data(), data.size());
    return true;
}

static bool classic_write_block(int block,
                                const std::array<uint8_t, 16> &data) {
    uint8_t command[18] = {0xa0, static_cast<uint8_t>(block)};
    std::memcpy(command + 2, data.data(), data.size());
    std::vector<uint8_t> response;
    return raw_transceive(command, sizeof(command), response, 5);
}

static int classic_sector_first_block(int sector) {
    return sector < 32 ? sector * 4 : 128 + (sector - 32) * 16;
}

static int classic_sector_block_count(int sector) {
    return sector < 32 ? 4 : 16;
}

static int classic_sector_count_for_sak(uint8_t sak) {
    if (sak == 0x18) return 40;
    if (sak == 0x09) return 5;
    return 16;
}

static const std::array<std::array<uint8_t, 6>, 3> kMctStandardKeys = {{
    {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}},
    {{0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5}},
    {{0xd3, 0xf7, 0xd3, 0xf7, 0xd3, 0xf7}},
}};

static bool find_classic_key(int first_block, bool key_b,
                             const std::array<uint8_t, 10> &uid,
                             size_t uid_length,
                             std::array<uint8_t, 6> &found) {
    for (const auto &key : kMctStandardKeys) {
        if (classic_authenticate(first_block, key, key_b, uid, uid_length)) {
            found = key;
            return true;
        }
    }
    return false;
}

static bool save_mct_dump() {
    char temporary[256];
    std::snprintf(temporary, sizeof(temporary), "%s.%ld", kMctDumpPath,
                  static_cast<long>(getpid()));
    FILE *file = std::fopen(temporary, "we");
    if (!file) return false;
    static constexpr char hex[] = "0123456789ABCDEF";
    for (int sector = 0; sector < classic_dump.sector_count; ++sector) {
        std::fprintf(file, "+Sector: %d\n", sector);
        int first = classic_sector_first_block(sector);
        int count = classic_sector_block_count(sector);
        bool any = false;
        for (int block = first; block < first + count; ++block)
            any = any || classic_dump.readable[block];
        if (!any) {
            std::fprintf(file, "*No keys found or dead sector\n");
            continue;
        }
        for (int block = first; block < first + count; ++block) {
            if (!classic_dump.readable[block]) {
                std::fprintf(file, "--------------------------------\n");
                continue;
            }
            auto data = classic_dump.blocks[block];
            if (block == first + count - 1) {
                if (classic_dump.has_key_a[sector])
                    std::memcpy(data.data(), classic_dump.key_a[sector].data(), 6);
                if (classic_dump.has_key_b[sector])
                    std::memcpy(data.data() + 10,
                                classic_dump.key_b[sector].data(), 6);
            }
            for (uint8_t byte : data)
                std::fprintf(file, "%c%c", hex[byte >> 4], hex[byte & 0x0f]);
            std::fputc('\n', file);
        }
    }
    bool ok = std::fclose(file) == 0 && rename(temporary, kMctDumpPath) == 0;
    if (!ok) unlink(temporary);
    return ok;
}

static bool valid_access_bytes(const std::array<uint8_t, 16> &trailer) {
    uint8_t not_c1 = trailer[6] & 0x0f;
    uint8_t not_c2 = (trailer[6] >> 4) & 0x0f;
    uint8_t not_c3 = trailer[7] & 0x0f;
    uint8_t c1 = (trailer[7] >> 4) & 0x0f;
    uint8_t c2 = trailer[8] & 0x0f;
    uint8_t c3 = (trailer[8] >> 4) & 0x0f;
    return c1 == static_cast<uint8_t>((~not_c1) & 0x0f) &&
           c2 == static_cast<uint8_t>((~not_c2) & 0x0f) &&
           c3 == static_cast<uint8_t>((~not_c3) & 0x0f);
}

static bool authenticate_target_sector(
    int first_block, const std::array<uint8_t, 10> &uid, size_t uid_length,
    std::array<uint8_t, 6> &key, bool &key_b, int source_sector) {
    for (bool use_b : {false, true}) {
        for (const auto &candidate : kMctStandardKeys) {
            if (classic_authenticate(first_block, candidate, use_b, uid,
                                     uid_length)) {
                key = candidate;
                key_b = use_b;
                return true;
            }
        }
        if (classic_dump.has_key_a[source_sector] &&
            classic_authenticate(first_block, classic_dump.key_a[source_sector],
                                 use_b, uid, uid_length)) {
            key = classic_dump.key_a[source_sector];
            key_b = use_b;
            return true;
        }
        if (classic_dump.has_key_b[source_sector] &&
            classic_authenticate(first_block, classic_dump.key_b[source_sector],
                                 use_b, uid, uid_length)) {
            key = classic_dump.key_b[source_sector];
            key_b = use_b;
            return true;
        }
    }
    return false;
}

static bool write_classic_block_authenticated(
    int block, const std::array<uint8_t, 16> &data,
    const std::array<uint8_t, 6> &key, bool key_b,
    const std::array<uint8_t, 10> &uid, size_t uid_length) {
    return classic_authenticate(block, key, key_b, uid, uid_length) &&
           classic_write_block(block, data);
}

static bool read_classic_source(const std::array<uint8_t, 10> &uid,
                                size_t uid_length, uint8_t sak) {
    ClassicDump dump;
    dump.sector_count = classic_sector_count_for_sak(sak);
    dump.block_count = dump.sector_count == 40 ? 256 :
                       dump.sector_count == 5 ? 20 : 64;
    dump.uid = uid;
    dump.uid_length = uid_length;
    dump.blocks.resize(dump.block_count);
    dump.readable.resize(dump.block_count, false);
    dump.key_a.resize(dump.sector_count);
    dump.key_b.resize(dump.sector_count);
    dump.has_key_a.resize(dump.sector_count, false);
    dump.has_key_b.resize(dump.sector_count, false);
    int readable_blocks = 0;
    for (int sector = 0; sector < dump.sector_count; ++sector) {
        int first = classic_sector_first_block(sector);
        int count = classic_sector_block_count(sector);
        dump.has_key_a[sector] = find_classic_key(
            first, false, uid, uid_length, dump.key_a[sector]);
        dump.has_key_b[sector] = find_classic_key(
            first, true, uid, uid_length, dump.key_b[sector]);
        for (int block = first; block < first + count; ++block) {
            bool read = false;
            if (dump.has_key_a[sector] &&
                classic_authenticate(first, dump.key_a[sector], false, uid,
                                     uid_length))
                read = classic_read_block(block, dump.blocks[block]);
            if (!read && dump.has_key_b[sector] &&
                classic_authenticate(first, dump.key_b[sector], true, uid,
                                     uid_length))
                read = classic_read_block(block, dump.blocks[block]);
            dump.readable[block] = read;
            if (read) ++readable_blocks;
        }
    }
    dump.valid = readable_blocks > 0;
    if (!dump.valid) return false;
    classic_dump = std::move(dump);
    source_uid = uid;
    source_uid_length = uid_length;
    return save_mct_dump();
}

static bool write_classic_target(const std::array<uint8_t, 10> &uid,
                                 size_t uid_length, uint8_t sak,
                                 int &written_blocks) {
    if (classic_sector_count_for_sak(sak) != classic_dump.sector_count)
        return false;
    written_blocks = 0;
    for (int sector = 0; sector < classic_dump.sector_count; ++sector) {
        int first = classic_sector_first_block(sector);
        int count = classic_sector_block_count(sector);
        std::array<uint8_t, 6> target_key{};
        bool target_key_b = false;
        if (!authenticate_target_sector(first, uid, uid_length, target_key,
                                        target_key_b, sector))
            continue;
        int trailer = first + count - 1;
        for (int block = first; block < trailer; ++block) {
            // Preserve the target card UID/manufacturer block. MCT-compatible
            // dumps are used, but unsafe magic-card UID rewriting is excluded.
            if (block == 0 || !classic_dump.readable[block]) continue;
            if (write_classic_block_authenticated(
                    block, classic_dump.blocks[block], target_key, target_key_b,
                    uid, uid_length))
                ++written_blocks;
        }
        if (classic_dump.readable[trailer] &&
            classic_dump.has_key_a[sector] &&
            classic_dump.has_key_b[sector]) {
            auto trailer_data = classic_dump.blocks[trailer];
            std::memcpy(trailer_data.data(), classic_dump.key_a[sector].data(), 6);
            std::memcpy(trailer_data.data() + 10,
                        classic_dump.key_b[sector].data(), 6);
            if (valid_access_bytes(trailer_data) &&
                write_classic_block_authenticated(
                    trailer, trailer_data, target_key, target_key_b, uid,
                    uid_length))
                ++written_blocks;
        }
    }
    return written_blocks > 0;
}

static void *classic_worker(void *) {
    std::array<uint8_t, 10> uid;
    size_t uid_length;
    uint8_t sak;
    pthread_mutex_lock(&state_lock);
    uid = current_uid;
    uid_length = current_uid_length;
    sak = current_sak;
    bool source = !classic_dump.valid;
    pthread_mutex_unlock(&state_lock);

    if (source) {
        write_status("mct_reading_source", true,
                     "MCT：正在用已知密钥读取源卡扇区");
        bool ok = read_classic_source(uid, uid_length, sak);
        write_status(ok ? "mct_await_target" : "error", ok,
                     ok ? "MCT 源卡已读取，请移开源卡再贴目标卡" :
                          "未找到可用密钥或无法读取 MIFARE Classic 源卡");
    } else if (same_uid(uid, uid_length, classic_dump.uid,
                        classic_dump.uid_length)) {
        write_status("mct_await_target", true,
                     "请先移开源卡，再贴另一张目标卡");
    } else {
        int written = 0;
        write_status("mct_writing_target", true,
                     "MCT：正在写入目标卡数据块");
        bool ok = write_classic_target(uid, uid_length, sak, written);
        write_status(ok ? "copy_complete" : "error", ok,
                     ok ? "MCT 数据块复制完成，目标卡 UID 已保留" :
                          "目标卡容量、密钥或访问条件不匹配");
    }
    pthread_mutex_lock(&state_lock);
    classic_worker_running = false;
    pthread_mutex_unlock(&state_lock);
    NFA_Deactivate(false);
    return nullptr;
}

static void start_classic_worker() {
    pthread_mutex_lock(&state_lock);
    if (classic_worker_running) {
        pthread_mutex_unlock(&state_lock);
        return;
    }
    classic_worker_running = true;
    pthread_mutex_unlock(&state_lock);
    pthread_t thread;
    if (pthread_create(&thread, nullptr, classic_worker, nullptr) == 0)
        pthread_detach(thread);
    else {
        pthread_mutex_lock(&state_lock);
        classic_worker_running = false;
        pthread_mutex_unlock(&state_lock);
        write_status("error", false, "无法启动 MCT 读写线程");
    }
}

static void ndef_callback(tNFA_NDEF_EVT event, tNFA_NDEF_EVT_DATA *data) {
    if (service_mode != ServiceMode::copy || event != NFA_NDEF_DATA_EVT ||
        !data || !data->ndef_data.p_data || !data->ndef_data.len)
        return;
    pthread_mutex_lock(&state_lock);
    if (read_pending) {
        copy_message.assign(data->ndef_data.p_data,
                            data->ndef_data.p_data + data->ndef_data.len);
        source_uid = current_uid;
        source_uid_length = current_uid_length;
        read_pending = false;
        if (save_copy_message())
            write_status("await_target", true,
                         "源标签已读取，请贴近可写的目标标签");
        else
            write_status("error", false, "保存源标签数据失败");
        NFA_Deactivate(false);
    }
    pthread_mutex_unlock(&state_lock);
}

static void connection_callback(uint8_t event, tNFA_CONN_EVT_DATA *data) {
    tNFA_STATUS status = data ? data->status : NFA_STATUS_FAILED;
    switch (event) {
    case NFA_POLL_ENABLED_EVT:
        notify(WaitFor::polling, status);
        break;
    case NFA_LISTEN_ENABLED_EVT:
        notify(WaitFor::listening, status);
        break;
    case NFA_CE_LOCAL_TAG_CONFIGURED_EVT:
        notify(WaitFor::local_tag, status);
        break;
    case NFA_RF_DISCOVERY_STARTED_EVT:
        notify(WaitFor::discovery, status);
        break;
    case NFA_RF_DISCOVERY_STOPPED_EVT:
        notify(WaitFor::discovery_stopped, status);
        break;
    case NFA_POLL_DISABLED_EVT:
        notify(WaitFor::polling_stopped, status);
        break;
    case NFA_LISTEN_DISABLED_EVT:
        notify(WaitFor::listening_stopped, status);
        break;
    case NFA_ACTIVATED_EVT:
        if (service_mode == ServiceMode::copy && data) {
            const auto &activation = data->activated.activate_ntf;
            pthread_mutex_lock(&state_lock);
            current_uid_length = 0;
            current_sak = 0;
            if (activation.rf_tech_param.mode == NFC_DISCOVERY_TYPE_POLL_A) {
                current_uid_length = activation.rf_tech_param.param.pa.nfcid1_len;
                if (current_uid_length > current_uid.size())
                    current_uid_length = current_uid.size();
                std::memcpy(current_uid.data(),
                            activation.rf_tech_param.param.pa.nfcid1,
                            current_uid_length);
                current_sak = activation.rf_tech_param.param.pa.sel_rsp;
            }
            classic_active = activation.protocol == NFC_PROTOCOL_MIFARE;
            pthread_mutex_unlock(&state_lock);
            pthread_mutex_lock(&raw_lock);
            tag_active = true;
            pthread_mutex_unlock(&raw_lock);
            if (classic_active)
                write_status("mct_detected", true,
                             "检测到 MIFARE Classic，正在准备 MCT 流程");
        }
        break;
    case NFA_DATA_EVT:
        pthread_mutex_lock(&raw_lock);
        if (raw_waiting && data) {
            raw_status = data->data.status;
            raw_response.clear();
            if (data->data.p_data && data->data.len)
                raw_response.assign(data->data.p_data,
                                    data->data.p_data + data->data.len);
            raw_waiting = false;
            pthread_cond_signal(&raw_cond);
        }
        pthread_mutex_unlock(&raw_lock);
        break;
    case NFA_RW_INTF_ERROR_EVT:
        pthread_mutex_lock(&raw_lock);
        if (raw_waiting) {
            raw_status = NFA_STATUS_FAILED;
            raw_waiting = false;
            pthread_cond_signal(&raw_cond);
        }
        pthread_mutex_unlock(&raw_lock);
        break;
    case NFA_DEACTIVATED_EVT:
        pthread_mutex_lock(&raw_lock);
        tag_active = false;
        if (raw_waiting) {
            raw_status = NFA_STATUS_FAILED;
            raw_waiting = false;
            pthread_cond_signal(&raw_cond);
        }
        pthread_mutex_unlock(&raw_lock);
        pthread_mutex_lock(&state_lock);
        classic_active = false;
        current_uid_length = 0;
        pthread_mutex_unlock(&state_lock);
        break;
    case NFA_NDEF_DETECT_EVT:
        if (service_mode != ServiceMode::copy || !data) break;
        pthread_mutex_lock(&state_lock);
        if (classic_active) {
            pthread_mutex_unlock(&state_lock);
            start_classic_worker();
            break;
        } else if (classic_dump.valid) {
            write_status("error", false,
                         "源卡是 MIFARE Classic，请使用同类型目标卡");
            NFA_Deactivate(false);
        } else if (data->ndef_detect.status != NFA_STATUS_OK ||
            !(data->ndef_detect.flags & NFA_RW_NDEF_FL_SUPPORTED)) {
            write_status("error", false, "标签不是标准 NDEF 标签");
            NFA_Deactivate(false);
        } else if (copy_message.empty()) {
            if (!data->ndef_detect.cur_size) {
                write_status("error", false, "源标签没有 NDEF 内容");
                NFA_Deactivate(false);
            } else {
                read_pending = NFA_RwReadNDef() == NFA_STATUS_OK;
                write_status(read_pending ? "reading_source" : "error",
                             read_pending,
                             read_pending ? "正在读取源标签" :
                                            "无法读取源标签");
            }
        } else if (same_uid(current_uid, current_uid_length, source_uid,
                            source_uid_length)) {
            write_status("await_target", true,
                         "请先移开源标签，再贴另一个目标标签");
            NFA_Deactivate(false);
        } else if ((data->ndef_detect.flags & RW_NDEF_FL_READ_ONLY) ||
                   data->ndef_detect.max_size < copy_message.size()) {
            write_status("error", false, "目标标签只读或容量不足");
            NFA_Deactivate(false);
        } else {
            write_pending =
                NFA_RwWriteNDef(copy_message.data(), copy_message.size()) ==
                NFA_STATUS_OK;
            write_status(write_pending ? "writing_target" : "error",
                         write_pending,
                         write_pending ? "正在写入目标标签" :
                                         "无法开始写入目标标签");
        }
        pthread_mutex_unlock(&state_lock);
        break;
    case NFA_WRITE_CPLT_EVT:
        if (service_mode != ServiceMode::copy) break;
        pthread_mutex_lock(&state_lock);
        if (write_pending) {
            write_pending = false;
            write_status(status == NFA_STATUS_OK ? "copy_complete" : "error",
                         status == NFA_STATUS_OK,
                         status == NFA_STATUS_OK ?
                             "复制完成，可继续贴近其他目标标签" :
                             "写入目标标签失败");
            NFA_Deactivate(false);
        }
        pthread_mutex_unlock(&state_lock);
        break;
    default:
        break;
    }
}

static void prepare_wait(WaitFor event) {
    pthread_mutex_lock(&event_lock);
    expected_event = event;
    event_status = NFA_STATUS_FAILED;
    pthread_mutex_unlock(&event_lock);
}

static bool wait_for_event(unsigned seconds) {
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += seconds;
    pthread_mutex_lock(&event_lock);
    while (expected_event != WaitFor::none) {
        int result = pthread_cond_timedwait(&event_cond, &event_lock, &deadline);
        if (result == ETIMEDOUT) {
            expected_event = WaitFor::none;
            pthread_mutex_unlock(&event_lock);
            return false;
        }
    }
    bool ok = event_status == NFA_STATUS_OK;
    pthread_mutex_unlock(&event_lock);
    return ok;
}

static bool run_and_wait(WaitFor event, tNFA_STATUS status,
                         unsigned seconds = 8) {
    if (status != NFA_STATUS_OK) {
        pthread_mutex_lock(&event_lock);
        expected_event = WaitFor::none;
        pthread_mutex_unlock(&event_lock);
        return false;
    }
    return wait_for_event(seconds);
}

static void signal_handler(int) { stop_requested = 1; }

int main(int argc, char **argv) {
    if (argc != 2 || (std::strcmp(argv[1], "share") &&
                      std::strcmp(argv[1], "copy"))) {
        std::fprintf(stderr, "usage: xm14pro-nfc-service share|copy\n");
        return 2;
    }
    service_mode = !std::strcmp(argv[1], "copy") ? ServiceMode::copy :
                                                    ServiceMode::share;
    if (service_mode == ServiceMode::share &&
        !make_hotspot_ndef(share_message)) {
        write_status("error", false, "热点参数无效，无法生成分享标签");
        return 2;
    }
    if (service_mode == ServiceMode::copy) {
        unlink(kCopyPath);
        unlink(kMctDumpPath);
    }
    write_status("starting", true, "正在启动 NFC 协议栈");
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    // The vendor HAL and this upper stack are separate procd instances.
    // Give the HAL time to publish its AIDL service after a joint restart.
    sleep(3);
    LeakedStrongPointer process_state = init_process_state("/dev/binder");
    if (!process_state.pointer || !ABinderProcess_setThreadPoolMaxThreadCount(1)) {
        write_status("error", false, "Binder 初始化失败");
        return 3;
    }
    pthread_t binder_thread;
    if (pthread_create(&binder_thread, nullptr, binder_thread_main, nullptr) != 0) {
        write_status("error", false, "Binder 回调线程创建失败");
        return 3;
    }
    pthread_detach(binder_thread);

    NfcAdaptation &adaptation = NfcAdaptation::GetInstance();
    adaptation.Initialize();
    NFA_Init(adaptation.GetHalEntryFuncs());
    prepare_wait(WaitFor::enabled);
    if (!run_and_wait(WaitFor::enabled,
                      NFA_Enable(device_management_callback,
                                 connection_callback))) {
        write_status("error", false, "NFA 协议栈启动失败");
        adaptation.Finalize();
        return 4;
    }

    bool polling = false, listening = false, discovery = false;
    if (service_mode == ServiceMode::share) {
        prepare_wait(WaitFor::local_tag);
        bool configured = run_and_wait(
            WaitFor::local_tag,
            NFA_CeConfigureLocalTag(NFA_PROTOCOL_MASK_ISO_DEP,
                                    share_message.data(), share_message.size(),
                                    share_message.size(), true, 0, nullptr));
        if (configured) {
            prepare_wait(WaitFor::listening);
            listening = run_and_wait(WaitFor::listening,
                                     NFA_EnableListening());
        }
    } else {
        NFA_RegisterNDefTypeHandler(true, NFA_TNF_DEFAULT, nullptr, 0,
                                    ndef_callback);
        const uint16_t mask = NFA_TECHNOLOGY_MASK_A |
                              NFA_TECHNOLOGY_MASK_B |
                              NFA_TECHNOLOGY_MASK_F |
                              NFA_TECHNOLOGY_MASK_V;
        prepare_wait(WaitFor::polling);
        polling = run_and_wait(WaitFor::polling, NFA_EnablePolling(mask));
    }
    if ((service_mode == ServiceMode::share && listening) ||
        (service_mode == ServiceMode::copy && polling)) {
        prepare_wait(WaitFor::discovery);
        discovery = run_and_wait(WaitFor::discovery, NFA_StartRfDiscovery());
    }
    if (!discovery) {
        write_status("error", false, "NFC 射频发现启动失败");
        stop_requested = 1;
    } else if (service_mode == ServiceMode::share) {
        write_status("share_active", true,
                     "Android NFC 分享已开启；iPhone 使用屏幕二维码");
    } else {
        write_status("await_source", true,
                     "请贴近源标签（NDEF 或 MIFARE Classic）");
    }

    while (!stop_requested) sleep(1);
    if (discovery) {
        prepare_wait(WaitFor::discovery_stopped);
        run_and_wait(WaitFor::discovery_stopped, NFA_StopRfDiscovery(), 5);
    }
    if (polling) {
        prepare_wait(WaitFor::polling_stopped);
        run_and_wait(WaitFor::polling_stopped, NFA_DisablePolling(), 5);
    }
    if (listening) {
        prepare_wait(WaitFor::listening_stopped);
        run_and_wait(WaitFor::listening_stopped, NFA_DisableListening(), 5);
        NFA_CeConfigureLocalTag(0, nullptr, 0, 0, true, 0, nullptr);
    }
    prepare_wait(WaitFor::disabled);
    run_and_wait(WaitFor::disabled, NFA_Disable(true), 5);
    adaptation.Finalize();
    return discovery ? 0 : 5;
}
