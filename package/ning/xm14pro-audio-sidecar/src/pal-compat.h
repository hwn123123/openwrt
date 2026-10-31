/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Minimal Qualcomm PAL client ABI used by xm14pro-audio-control.
 *
 * The definitions intentionally mirror the public SM8650 PAL headers.  Keep
 * this file small: the actual routing, calibration and speaker protection
 * remain in Xiaomi's unmodified PAL service and XML/ACDB payload.
 */
#ifndef XM14PRO_PAL_COMPAT_H
#define XM14PRO_PAL_COMPAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#define PAL_MAX_CHANNELS_SUPPORTED 64
#define PAL_MAX_CUSTOM_KEY_SIZE 128
#define DEVICE_NAME_MAX_SIZE 128

typedef uint64_t pal_stream_handle_t;

typedef enum {
	PAL_AUDIO_FMT_PCM_S16_LE = 1,
} pal_audio_fmt_t;

typedef enum {
	PAL_STREAM_DEEP_BUFFER = 2,
	PAL_STREAM_VOICE_CALL = 14,
} pal_stream_type_t;

typedef enum {
	PAL_DEVICE_OUT_HANDSET = 2,
	PAL_DEVICE_OUT_SPEAKER = 3,
	/* Pineapple/SM8650 adds dedicated ultrasound, BLE, dummy and record
	 * proxy outputs. Its public PAL ABI therefore keeps OUT_MAX at 26;
	 * using the older 23/24 input IDs selects BLE output devices and makes
	 * pal_stream_open(PAL_STREAM_VOICE_CALL) fail with ENODEV. */
	PAL_DEVICE_IN_HANDSET_MIC = 27,
	PAL_DEVICE_IN_SPEAKER_MIC = 28,
} pal_device_id_t;

typedef enum {
	PAL_AUDIO_OUTPUT = 1,
	PAL_AUDIO_INPUT = 2,
	PAL_AUDIO_INPUT_OUTPUT = 3,
} pal_stream_direction_t;

typedef enum {
	PAL_STREAM_FLAG_NONE = 0,
} pal_stream_flags_t;

typedef enum {
	PAL_DRAIN = 0,
} pal_drain_type_t;

enum {
	PAL_CHMAP_CHANNEL_FL = 1,
	PAL_CHMAP_CHANNEL_FR = 2,
};

struct pal_channel_info {
	uint16_t channels;
	uint8_t ch_map[PAL_MAX_CHANNELS_SUPPORTED];
};

struct pal_stream_info {
	int64_t version;
	int64_t size;
	int64_t duration_us;
	bool has_video;
	bool is_streaming;
	int32_t loopback_type;
	int32_t tx_proxy_type;
	int32_t rx_proxy_type;
	int32_t haptics_type;
};

struct pal_voice_record_info {
	int64_t version;
	int64_t size;
	int32_t record_direction;
};

struct pal_voice_call_info {
	uint32_t VSID;
	uint32_t tty_mode;
};

struct pal_incall_music_info {
	bool local_playback;
};

typedef union {
	struct pal_stream_info opt_stream_info;
	struct pal_voice_record_info voice_rec_info;
	struct pal_voice_call_info voice_call_info;
	struct pal_incall_music_info incall_music_info;
} pal_stream_info_t;

struct pal_media_config {
	uint32_t sample_rate;
	uint32_t bit_width;
	pal_audio_fmt_t aud_fmt_id;
	struct pal_channel_info ch_info;
};

struct pal_stream_attributes {
	pal_stream_type_t type;
	pal_stream_info_t info;
	pal_stream_flags_t flags;
	pal_stream_direction_t direction;
	struct pal_media_config in_media_config;
	struct pal_media_config out_media_config;
	bool isComboHeadsetActive;
};

struct modifier_kv {
	uint32_t key;
	uint32_t value;
};

struct pal_usb_device_address {
	int card_id;
	int device_num;
};

typedef struct pal_device_custom_config {
	char custom_key[PAL_MAX_CUSTOM_KEY_SIZE];
} pal_device_custom_config_t;

struct pal_device {
	pal_device_id_t id;
	struct pal_media_config config;
	struct pal_usb_device_address address;
	char sndDevName[DEVICE_NAME_MAX_SIZE];
	pal_device_custom_config_t custom_config;
};

typedef struct pal_extern_alloc_buff_info {
	int alloc_handle;
	uint32_t alloc_size;
	uint32_t offset;
} pal_extern_alloc_buff_info_t;

struct pal_buffer {
	uint8_t *buffer;
	size_t size;
	size_t offset;
	struct timespec *ts;
	uint32_t flags;
	size_t metadata_size;
	uint8_t *metadata;
	pal_extern_alloc_buff_info_t alloc_info;
	uint64_t frame_index;
};

struct pal_channel_vol_kv {
	uint32_t channel_mask;
	float vol;
};

struct pal_volume_data {
	uint32_t no_of_volpair;
	struct pal_channel_vol_kv volume_pair[];
};

typedef int32_t (*pal_stream_callback)(pal_stream_handle_t *stream_handle,
	uint32_t event_id, uint32_t *event_data, uint32_t event_data_size,
	uint64_t cookie);

int32_t pal_init(void);
void pal_deinit(void);
int32_t pal_stream_open(struct pal_stream_attributes *attributes,
	uint32_t no_of_devices, struct pal_device *devices,
	uint32_t no_of_modifiers, struct modifier_kv *modifiers,
	pal_stream_callback callback, uint64_t cookie,
	pal_stream_handle_t **stream_handle);
int32_t pal_stream_close(pal_stream_handle_t *stream_handle);
int32_t pal_stream_start(pal_stream_handle_t *stream_handle);
int32_t pal_stream_stop(pal_stream_handle_t *stream_handle);
int32_t pal_stream_drain(pal_stream_handle_t *stream_handle,
	pal_drain_type_t drain_type);
ssize_t pal_stream_write(pal_stream_handle_t *stream_handle,
	struct pal_buffer *buffer);
int32_t pal_stream_set_volume(pal_stream_handle_t *stream_handle,
	struct pal_volume_data *volume);
int32_t pal_stream_set_device(pal_stream_handle_t *stream_handle,
	uint32_t no_of_devices, struct pal_device *devices);
int32_t pal_stream_set_mute(pal_stream_handle_t *stream_handle, bool state);

#endif
