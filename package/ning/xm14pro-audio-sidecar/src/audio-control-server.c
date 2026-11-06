// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include "pal-compat.h"

#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define SAMPLE_RATE 48000U
#define CHANNELS 2U
#define CHUNK_FRAMES 480U
#define PI 3.14159265358979323846

struct pal_api {
	void *library;
	int32_t (*init)(void);
	void (*deinit)(void);
	int32_t (*open)(struct pal_stream_attributes *, uint32_t,
		struct pal_device *, uint32_t, struct modifier_kv *,
		pal_stream_callback, uint64_t, pal_stream_handle_t **);
	int32_t (*close)(pal_stream_handle_t *);
	int32_t (*start)(pal_stream_handle_t *);
	int32_t (*stop)(pal_stream_handle_t *);
	int32_t (*drain)(pal_stream_handle_t *, pal_drain_type_t);
	ssize_t (*write)(pal_stream_handle_t *, struct pal_buffer *);
	int32_t (*set_volume)(pal_stream_handle_t *, struct pal_volume_data *);
	int32_t (*set_device)(pal_stream_handle_t *, uint32_t,
		struct pal_device *);
	int32_t (*set_mute)(pal_stream_handle_t *, bool);
};

struct single_volume {
	uint32_t no_of_volpair;
	struct pal_channel_vol_kv volume_pair[1];
};

static pthread_mutex_t pal_lock = PTHREAD_MUTEX_INITIALIZER;
static struct pal_api pal;
static void *audio_primary;
static int agm_service_initialized;
static unsigned int media_volume = 60;
static unsigned int call_volume = 50;
static pal_stream_handle_t *active_output_stream;
static const char *active_volume_context = "media";
static pal_stream_handle_t *voice_stream;
static char voice_route[16] = "earpiece";
static int voice_muted;
static unsigned int voice_slot = 1;
static uint32_t voice_vsid = 0x11c05000U;

static int load_symbol(void **target, const char *name)
{
	*target = dlsym(pal.library, name);
	return *target ? 0 : -1;
}

/* Audio's DIAG logging is optional. On NWRT there is normally no Android
 * diag-router. Letting the stock library initialize against an absent socket
 * creates a reader that loops on EOF and consumes an entire CPU core. Keep
 * the failure at the initialization boundary; real audio/PAL and Android
 * error logging remain available. Delegate when a real transport exists. */
static int diag_transport_available(void)
{
	struct stat st;
	char line[512];
	FILE *sockets;
	int available = 0;

	if (stat("/dev/diag", &st) == 0 && S_ISCHR(st.st_mode))
		return 1;
	sockets = fopen("/proc/net/unix", "r");
	if (!sockets)
		return 0;
	while (fgets(line, sizeof(line), sockets)) {
		char path[256];
		unsigned int flags, type;
		if (sscanf(line, "%*s %*s %*s %x %x %*s %*s %255s",
			   &flags, &type, path) != 3)
			continue;
		if (!(flags & 0x10000U) && type != SOCK_DGRAM)
			continue;
		if (!strncmp(path, "@diag", 5) ||
		    !strncmp(path, "/dev/socket/diag", 16)) {
			available = 1;
			break;
		}
	}
	fclose(sockets);
	return available;
}

unsigned char Diag_LSM_Init(unsigned char *parameter)
{
	static unsigned char (*original)(unsigned char *);
	void *library;

	if (getenv("XM14_PROPERTY_SERVICE_ABSENT") && !diag_transport_available())
		return 0;
	if (!original) {
		library = dlopen("/vendor/lib64/libdiag.so", RTLD_NOW | RTLD_NOLOAD);
		if (library)
			original = (unsigned char (*)(unsigned char *))dlsym(
				library, "Diag_LSM_Init");
	}
	return original ? original(parameter) : 0;
}

/*
 * Android's audioserver opens the primary Audio HAL before any PAL client.
 * That open path invokes AudioExtn::audio_extn_hidl_init(), which owns and
 * registers vendor.qti.hardware.AGMIPC@1.0.  nwrt intentionally does not run
 * audioserver, so reproduce only that missing initialization edge inside the
 * already-running stock Audio HAL process.  The implementation and service
 * remain the unmodified Xiaomi/Qualcomm objects from audio.primary.pineapple.
 */
static int ensure_agm_service(void)
{
	void (*initialize)(void);

	if (agm_service_initialized)
		return 0;
	audio_primary = dlopen("/vendor/lib64/hw/audio.primary.pineapple.so",
		RTLD_NOW | RTLD_GLOBAL);
	if (!audio_primary)
		return -ENOENT;
	initialize = (void (*)(void))dlsym(audio_primary,
		"_ZN9AudioExtn20audio_extn_hidl_initEv");
	if (!initialize)
		return -ENOSYS;
	initialize();
	agm_service_initialized = 1;
	return 0;
}

static int load_pal(void)
{
	int result;

	if (pal.library)
		return 0;
	result = ensure_agm_service();
	if (result)
		return result;
	pal.library = dlopen("/vendor/lib64/libar-pal.so", RTLD_NOW | RTLD_LOCAL);
	if (!pal.library)
		return -ENOENT;
#define LOAD(name, member) \
	do { if (load_symbol((void **)&pal.member, name) < 0) goto missing; } while (0)
	LOAD("pal_init", init);
	LOAD("pal_deinit", deinit);
	LOAD("pal_stream_open", open);
	LOAD("pal_stream_close", close);
	LOAD("pal_stream_start", start);
	LOAD("pal_stream_stop", stop);
	LOAD("pal_stream_drain", drain);
	LOAD("pal_stream_write", write);
	LOAD("pal_stream_set_volume", set_volume);
	LOAD("pal_stream_set_device", set_device);
	LOAD("pal_stream_set_mute", set_mute);
#undef LOAD
	result = pal.init();
	if (result) {
		dlclose(pal.library);
		memset(&pal, 0, sizeof(pal));
		return result;
	}
	return 0;

missing:
	dlclose(pal.library);
	memset(&pal, 0, sizeof(pal));
	return -ENOSYS;
}

static void configure_media(struct pal_media_config *config)
{
	memset(config, 0, sizeof(*config));
	config->sample_rate = SAMPLE_RATE;
	config->bit_width = 16;
	config->aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE;
	config->ch_info.channels = CHANNELS;
	config->ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;
	config->ch_info.ch_map[1] = PAL_CHMAP_CHANNEL_FR;
}

static void configure_voice_devices(const char *route,
	struct pal_device devices[2])
{
	memset(devices, 0, sizeof(struct pal_device) * 2);
	devices[0].id = strcmp(route, "speaker") == 0 ?
		PAL_DEVICE_IN_SPEAKER_MIC : PAL_DEVICE_IN_HANDSET_MIC;
	devices[0].config.sample_rate = SAMPLE_RATE;
	devices[0].config.bit_width = 16;
	devices[0].config.aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE;
	devices[0].config.ch_info.channels = 1;
	devices[0].config.ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;
	devices[1].id = strcmp(route, "speaker") == 0 ?
		PAL_DEVICE_OUT_SPEAKER : PAL_DEVICE_OUT_HANDSET;
	devices[1].config.sample_rate = SAMPLE_RATE;
	devices[1].config.bit_width = 16;
	devices[1].config.aud_fmt_id = PAL_AUDIO_FMT_PCM_S16_LE;
	/* Xiaomi declares both voice-call RX routes as mono. Stereo can open but
	 * leaves the speaker voice graph disconnected from modem RX. */
	devices[1].config.ch_info.channels = 1;
	devices[1].config.ch_info.ch_map[0] = PAL_CHMAP_CHANNEL_FL;
}

static int apply_voice_volume(void)
{
	struct single_volume volume;

	if (!voice_stream)
		return 0;
	memset(&volume, 0, sizeof(volume));
	volume.no_of_volpair = 1;
	volume.volume_pair[0].channel_mask = 0x01;
	volume.volume_pair[0].vol = (float)call_volume / 100.0f;
	return pal.set_volume(voice_stream,
		(struct pal_volume_data *)&volume);
}

static int start_voice_call(const char *route, unsigned int slot)
{
	struct pal_stream_attributes attributes;
	struct pal_device devices[2];
	int result;

	if (strcmp(route, "speaker") != 0 && strcmp(route, "earpiece") != 0)
		return -EINVAL;
	if (slot != 1 && slot != 2)
		return -EINVAL;
	result = load_pal();
	if (result)
		return result;
	configure_voice_devices(route, devices);
	if (voice_stream) {
		if (slot != voice_slot)
			return -EBUSY;
		result = pal.set_device(voice_stream, 2, devices);
		if (!result)
			strcpy(voice_route, route);
		return result;
	}
	memset(&attributes, 0, sizeof(attributes));
	attributes.type = PAL_STREAM_VOICE_CALL;
	voice_vsid = slot == 2 ? 0x11dc5000U : 0x11c05000U;
	attributes.info.voice_call_info.VSID = voice_vsid;
	attributes.info.voice_call_info.tty_mode = 0;
	attributes.direction = PAL_AUDIO_INPUT_OUTPUT;
	attributes.in_media_config = devices[0].config;
	attributes.out_media_config = devices[1].config;
	result = pal.open(&attributes, 2, devices, 0, NULL, NULL, 0,
		&voice_stream);
	if (result || !voice_stream) {
		voice_stream = NULL;
		return result ? result : -EIO;
	}
	result = apply_voice_volume();
	if (!result)
		result = pal.start(voice_stream);
	if (!result && voice_muted)
		result = pal.set_mute(voice_stream, true);
	if (result) {
		pal.stop(voice_stream);
		pal.close(voice_stream);
		voice_stream = NULL;
		return result;
	}
	strcpy(voice_route, route);
	voice_slot = slot;
	return 0;
}

static int stop_voice_call(void)
{
	int stop_result;
	int close_result;

	if (!voice_stream)
		return 0;
	stop_result = pal.stop(voice_stream);
	close_result = pal.close(voice_stream);
	voice_stream = NULL;
	voice_muted = 0;
	return stop_result ? stop_result : close_result;
}

static int set_voice_route(const char *route)
{
	struct pal_device devices[2];
	int result;

	if (!voice_stream)
		return -ENOTCONN;
	if (strcmp(route, "speaker") != 0 && strcmp(route, "earpiece") != 0)
		return -EINVAL;
	configure_voice_devices(route, devices);
	result = pal.set_device(voice_stream, 2, devices);
	if (!result)
		strcpy(voice_route, route);
	return result;
}

static int set_voice_mute(int muted)
{
	int result;

	if (!voice_stream)
		return -ENOTCONN;
	result = pal.set_mute(voice_stream, muted != 0);
	if (!result)
		voice_muted = muted != 0;
	return result;
}

static void generate_tone(int16_t *samples, size_t first_frame,
	size_t frames, size_t total_frames)
{
	size_t frame;
	size_t fade_frames = SAMPLE_RATE / 20;

	for (frame = 0; frame < frames; frame++) {
		size_t position = first_frame + frame;
		double envelope = 1.0;
		double phase;
		int16_t sample;

		if (position < fade_frames)
			envelope = (double)position / (double)fade_frames;
		else if (total_frames - position < fade_frames)
			envelope = (double)(total_frames - position) /
				(double)fade_frames;
		phase = 2.0 * PI * 660.0 * (double)position /
			(double)SAMPLE_RATE;
		sample = (int16_t)(sin(phase) * envelope * 8192.0);
		samples[frame * 2] = sample;
		samples[frame * 2 + 1] = sample;
	}
}

static int play_tone(pal_device_id_t device_id, unsigned int volume_percent,
	unsigned int duration_ms)
{
	struct pal_stream_attributes attributes;
	struct pal_device device;
	struct single_volume volume;
	pal_stream_handle_t *stream = NULL;
	int16_t samples[CHUNK_FRAMES * CHANNELS];
	struct pal_buffer buffer;
	size_t total_frames = (size_t)SAMPLE_RATE * duration_ms / 1000U;
	size_t position = 0;
	int opened = 0;
	int started = 0;
	int result;

	result = load_pal();
	if (result)
		return result;
	memset(&attributes, 0, sizeof(attributes));
	attributes.type = PAL_STREAM_DEEP_BUFFER;
	attributes.info.opt_stream_info.version = 1;
	attributes.info.opt_stream_info.size =
		sizeof(attributes.info.opt_stream_info);
	attributes.info.opt_stream_info.duration_us =
		(int64_t)duration_ms * 1000;
	attributes.direction = PAL_AUDIO_OUTPUT;
	attributes.flags = PAL_STREAM_FLAG_NONE;
	configure_media(&attributes.out_media_config);
	memset(&device, 0, sizeof(device));
	device.id = device_id;
	configure_media(&device.config);

	result = pal.open(&attributes, 1, &device, 0, NULL, NULL, 0, &stream);
	if (result || !stream)
		goto out;
	opened = 1;
	memset(&volume, 0, sizeof(volume));
	volume.no_of_volpair = 1;
	volume.volume_pair[0].channel_mask = 0x03;
	volume.volume_pair[0].vol = (float)volume_percent / 100.0f;
	result = pal.set_volume(stream, (struct pal_volume_data *)&volume);
	if (result)
		goto out;
	result = pal.start(stream);
	if (result)
		goto out;
	started = 1;
	active_output_stream = stream;
	active_volume_context = "media";
	memset(&buffer, 0, sizeof(buffer));
	buffer.buffer = (uint8_t *)samples;
	buffer.alloc_info.alloc_handle = -1;
	while (position < total_frames) {
		size_t frames = total_frames - position;
		ssize_t written;

		if (frames > CHUNK_FRAMES)
			frames = CHUNK_FRAMES;
		generate_tone(samples, position, frames, total_frames);
		buffer.size = frames * CHANNELS * sizeof(samples[0]);
		buffer.frame_index = position;
		written = pal.write(stream, &buffer);
		if (written <= 0) {
			result = written < 0 ? (int)written : -EIO;
			goto out;
		}
		position += (size_t)written /
			(CHANNELS * sizeof(samples[0]));
	}
	result = pal.drain(stream, PAL_DRAIN);

out:
	active_output_stream = NULL;
	if (started)
		pal.stop(stream);
	if (opened)
		pal.close(stream);
	return result;
}

static unsigned int bounded(const char *text, unsigned int minimum,
	unsigned int maximum, unsigned int fallback)
{
	char *end = NULL;
	unsigned long value;

	if (!text || !*text)
		return fallback;
	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno || !end || *end || value < minimum || value > maximum)
		return fallback;
	return (unsigned int)value;
}

static void handle_client(int client)
{
	char request[256];
	char response[256];
	char route[32] = "";
	char slot_text[32] = "";
	char volume_text[32] = "";
	char duration_text[32] = "";
	ssize_t length;
	int result;

	length = read(client, request, sizeof(request) - 1);
	if (length <= 0)
		return;
	request[length] = '\0';
	if (sscanf(request, "volume %31s %31s", route, volume_text) == 2 &&
	    (strcmp(route, "media") == 0 || strcmp(route, "call") == 0)) {
		unsigned int value = bounded(volume_text, 0, 100,
			strcmp(route, "call") == 0 ? call_volume : media_volume);
		struct single_volume volume;

		if (strcmp(route, "call") == 0)
			call_volume = value;
		else
			media_volume = value;
		result = 0;
		if (!strcmp(route, "call") && voice_stream) {
			result = apply_voice_volume();
		} else if (active_output_stream &&
			   !strcmp(route, active_volume_context)) {
			memset(&volume, 0, sizeof(volume));
			volume.no_of_volpair = 1;
			volume.volume_pair[0].channel_mask = 0x03;
			volume.volume_pair[0].vol = (float)value / 100.0f;
			result = pal.set_volume(active_output_stream,
				(struct pal_volume_data *)&volume);
		}
		snprintf(response, sizeof(response),
			"{\"ok\":%s,\"command\":\"volume\",\"context\":\"%s\","
			"\"volume\":%u,\"result\":%d}\n",
			result ? "false" : "true", route, value, result);
	} else if (sscanf(request, "call-start %31s %31s", route,
		   slot_text) >= 1) {
		unsigned int slot = !strcmp(slot_text, "2") ? 2U : 1U;
		if (slot_text[0] && strcmp(slot_text, "1") &&
		    strcmp(slot_text, "2"))
			result = -EINVAL;
		else
			result = start_voice_call(route, slot);
		snprintf(response, sizeof(response),
			"{\"ok\":%s,\"command\":\"call-start\","
			"\"active\":%s,\"route\":\"%s\",\"slot\":%u,"
			"\"vsid\":\"0x%08x\",\"result\":%d}\n",
			result ? "false" : "true", voice_stream ? "true" : "false",
			voice_route, voice_slot, voice_vsid, result);
	} else if (strncmp(request, "call-stop", 9) == 0) {
		result = stop_voice_call();
		snprintf(response, sizeof(response),
			"{\"ok\":%s,\"command\":\"call-stop\","
			"\"active\":false,\"result\":%d}\n",
			result ? "false" : "true", result);
	} else if (sscanf(request, "call-route %31s", route) == 1) {
		result = set_voice_route(route);
		snprintf(response, sizeof(response),
			"{\"ok\":%s,\"command\":\"call-route\","
			"\"active\":%s,\"route\":\"%s\",\"result\":%d}\n",
			result ? "false" : "true", voice_stream ? "true" : "false",
			voice_route, result);
	} else if (sscanf(request, "call-mute %31s", volume_text) == 1 &&
		   (!strcmp(volume_text, "0") || !strcmp(volume_text, "1"))) {
		result = set_voice_mute(!strcmp(volume_text, "1"));
		snprintf(response, sizeof(response),
			"{\"ok\":%s,\"command\":\"call-mute\","
			"\"active\":%s,\"muted\":%s,\"result\":%d}\n",
			result ? "false" : "true", voice_stream ? "true" : "false",
			voice_muted ? "true" : "false", result);
	} else if (sscanf(request, "tone %31s %31s %31s", route, volume_text,
		   duration_text) >= 1 &&
	    (strcmp(route, "speaker") == 0 || strcmp(route, "earpiece") == 0)) {
		unsigned int volume = bounded(volume_text, 1, 40, 10);
		unsigned int duration = bounded(duration_text, 100, 3000, 800);
		result = play_tone(strcmp(route, "speaker") == 0 ?
			PAL_DEVICE_OUT_SPEAKER : PAL_DEVICE_OUT_HANDSET,
			volume, duration);
		snprintf(response, sizeof(response),
			"{\"ok\":%s,\"command\":\"tone\",\"route\":\"%s\","
			"\"volume\":%u,\"duration_ms\":%u,\"result\":%d}\n",
			result ? "false" : "true", route, volume, duration, result);
	} else if (strncmp(request, "status", 6) == 0) {
		/* Reading status must not initialize a DSP session or vendor logging. */
		result = 0;
		snprintf(response, sizeof(response),
			"{\"ok\":%s,\"pal_ready\":%s,\"media_volume\":%u,"
			"\"call_volume\":%u,\"voice_active\":%s,"
			"\"voice_route\":\"%s\",\"voice_slot\":%u,"
			"\"voice_vsid\":\"0x%08x\",\"mic_muted\":%s,"
			"\"voice_capable\":true,\"result\":%d}\n",
			"true", pal.library ? "true" : "false",
			media_volume, call_volume, voice_stream ? "true" : "false",
			voice_route, voice_slot, voice_vsid,
			voice_muted ? "true" : "false", result);
	} else {
		snprintf(response, sizeof(response),
			"{\"ok\":false,\"error\":\"unsupported command\"}\n");
	}
	write(client, response, strlen(response));
}

static void *control_thread(void *unused)
{
	const char *socket_value = getenv("ANDROID_SOCKET_xm14pro_audio_control");
	int server;

	(void)unused;
	if (!socket_value || !*socket_value)
		return NULL;
	server = atoi(socket_value);
	for (;;) {
		int client = accept4(server, NULL, NULL, SOCK_CLOEXEC);

		if (client < 0) {
			if (errno == EINTR)
				continue;
			sleep(1);
			continue;
		}
		pthread_mutex_lock(&pal_lock);
		handle_client(client);
		pthread_mutex_unlock(&pal_lock);
		close(client);
	}
}

__attribute__((constructor)) static void start_control_server(void)
{
	pthread_t thread;

	/* Keep the stock PAL process in sync with the values persisted by UCI.
	 * The launcher inherits these two variables from procd; no Android audio
	 * policy or routing state is replaced here. */
	media_volume = bounded(getenv("XM14_MEDIA_VOLUME"), 0, 100, 60);
	call_volume = bounded(getenv("XM14_CALL_VOLUME"), 0, 100, 50);
	if (pthread_create(&thread, NULL, control_thread, NULL) == 0)
		pthread_detach(thread);
}
