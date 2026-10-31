// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <gio/gio.h>
#include <libqrtr-glib.h>
#include <libqmi-glib.h>

typedef enum { CMD_DIAL, CMD_ANSWER, CMD_END, CMD_DTMF, CMD_SMS_SEND, CMD_SMS_READ, CMD_SMSC_GET, CMD_SMS_LISTEN, CMD_VOICE_LISTEN } Command;

static GMainLoop *loop;
static QrtrBus *bus;
static QmiDevice *device;
static QmiClient *client;
static Command command;
static const char *argument1;
static const char *argument2;
static guint32 message_index;
static int exit_status = EXIT_FAILURE;
static GPtrArray *sms_pdus;
static guint sms_pdu_index;
static guint16 sms_last_message_id;
static gboolean sms_on_ims;
static gboolean voice_query_pending;

static void write_call_state(guint count, guint8 id, QmiVoiceCallState state,
                             QmiVoiceCallType type, QmiVoiceCallDirection direction,
                             QmiVoiceCallMode mode, const char *number)
{
    char *temporary;
    FILE *file;

    temporary = g_strdup_printf("%s.%ld", argument1, (long)getpid());
    file = fopen(temporary, "w");
    if (!file) { fprintf(stderr, "cannot write call state: %s\n", g_strerror(errno)); g_free(temporary); return; }
    fprintf(file, "active_call_count=%u\n", count);
    if (count) {
        fprintf(file, "active_call_id=%u\n", id);
        fprintf(file, "active_call_state=%s\n", qmi_voice_call_state_get_string(state));
        fprintf(file, "active_call_type=%s\n", qmi_voice_call_type_get_string(type));
        fprintf(file, "active_call_direction=%s\n", qmi_voice_call_direction_get_string(direction));
        fprintf(file, "active_call_mode=%s\n", qmi_voice_call_mode_get_string(mode));
        if (number && *number) fprintf(file, "active_call_number=%s\n", number);
    }
    fclose(file);
    if (rename(temporary, argument1) != 0) fprintf(stderr, "cannot publish call state: %s\n", g_strerror(errno));
    g_free(temporary);
}

static const char *indication_number(GArray *numbers, guint8 id)
{
    guint i;
    if (!numbers) return NULL;
    for (i = 0; i < numbers->len; i++) {
        QmiIndicationVoiceAllCallStatusOutputRemotePartyNumberCall item =
            g_array_index(numbers, QmiIndicationVoiceAllCallStatusOutputRemotePartyNumberCall, i);
        if (item.id == id) return item.type;
    }
    return NULL;
}

static const char *response_number(GArray *numbers, guint8 id)
{
    guint i;
    if (!numbers) return NULL;
    for (i = 0; i < numbers->len; i++) {
        QmiMessageVoiceGetAllCallInfoOutputRemotePartyNumberCall item =
            g_array_index(numbers, QmiMessageVoiceGetAllCallInfoOutputRemotePartyNumberCall, i);
        if (item.id == id) return item.type;
    }
    return NULL;
}

static void all_call_status(QmiClientVoice *voice, QmiIndicationVoiceAllCallStatusOutput *report, gpointer unused)
{
    GArray *calls = NULL, *numbers = NULL;
    GError *error = NULL;
    QmiIndicationVoiceAllCallStatusOutputCallInformationCall call;

    if (!qmi_indication_voice_all_call_status_output_get_call_information(report, &calls, &error)) {
        g_clear_error(&error); return;
    }
    qmi_indication_voice_all_call_status_output_get_remote_party_number(report, &numbers, NULL);
    if (!calls || !calls->len) { write_call_state(0, 0, 0, 0, 0, 0, NULL); return; }
    call = g_array_index(calls, QmiIndicationVoiceAllCallStatusOutputCallInformationCall, 0);
    write_call_state(calls->len, call.id, call.state, call.type, call.direction, call.mode,
                     indication_number(numbers, call.id));
}

static gboolean parse_hex(const char *hex, GArray **result)
{
    gsize length = strlen(hex), i;
    GArray *data;
    if (!length || (length & 1)) return FALSE;
    data = g_array_sized_new(FALSE, FALSE, sizeof(guint8), length / 2);
    for (i = 0; i < length; i += 2) {
        char pair[3] = { hex[i], hex[i + 1], 0 }, *end = NULL;
        guint8 value;
        errno = 0; value = (guint8)strtoul(pair, &end, 16);
        if (errno || !end || *end) { g_array_unref(data); return FALSE; }
        g_array_append_val(data, value);
    }
    *result = data;
    return TRUE;
}

static void write_hex(FILE *file, const guint8 *data, gsize length)
{
    gsize i;
    for (i = 0; i < length; i++) fprintf(file, "%02X", data[i]);
}

static void json_string(const char *text)
{
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    putchar('"');
    for (; *p; p++) {
        switch (*p) {
        case '"': fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\b': fputs("\\b", stdout); break;
        case '\f': fputs("\\f", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        default:
            if (*p < 0x20) printf("\\u%04x", *p);
            else putchar(*p);
        }
    }
    putchar('"');
}

static gboolean next_utf8(const unsigned char **cursor, guint32 *codepoint)
{
    const unsigned char *p = *cursor;
    guint32 cp;
    if (!*p) return FALSE;
    if (*p < 0x80) { *codepoint = *p; *cursor = p + 1; return TRUE; }
    if ((*p & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) {
        cp = ((*p & 0x1f) << 6) | (p[1] & 0x3f); *cursor = p + 2;
    } else if ((*p & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
        cp = ((*p & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f); *cursor = p + 3;
    } else if ((*p & 0xf8) == 0xf0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80) {
        cp = ((*p & 7) << 18) | ((p[1] & 0x3f) << 12) | ((p[2] & 0x3f) << 6) | (p[3] & 0x3f); *cursor = p + 4;
    } else return FALSE;
    if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return FALSE;
    *codepoint = cp;
    return TRUE;
}

static void append_utf8(GString *out, guint32 cp)
{
    if (cp < 0x80) g_string_append_c(out, (char)cp);
    else if (cp < 0x800) {
        g_string_append_c(out, 0xc0 | (cp >> 6)); g_string_append_c(out, 0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        g_string_append_c(out, 0xe0 | (cp >> 12)); g_string_append_c(out, 0x80 | ((cp >> 6) & 0x3f)); g_string_append_c(out, 0x80 | (cp & 0x3f));
    } else {
        g_string_append_c(out, 0xf0 | (cp >> 18)); g_string_append_c(out, 0x80 | ((cp >> 12) & 0x3f));
        g_string_append_c(out, 0x80 | ((cp >> 6) & 0x3f)); g_string_append_c(out, 0x80 | (cp & 0x3f));
    }
}

struct sms_chunk {
    gsize offset;
    gsize length;
};

static gboolean append_submit_pdu(GPtrArray *result, const guint8 *digits,
                                  gsize digit_count, gboolean international,
                                  const guint8 *text, gsize text_length,
                                  guint8 reference, guint8 total, guint8 part,
                                  GError **error)
{
    guint8 bytes[192];
    gsize n = 0, i;
    gboolean multipart = total > 1;
    GArray *pdu;

    if (text_length + (multipart ? 6 : 0) > 140) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "encoded SMS segment exceeds 140 octets");
        return FALSE;
    }
    bytes[n++] = 0x00; /* SMSC: use network default */
    bytes[n++] = multipart ? 0x41 : 0x01; /* SMS-SUBMIT, optionally TP-UDHI */
    bytes[n++] = 0x00; /* TP-MR */
    bytes[n++] = (guint8)digit_count;
    bytes[n++] = international ? 0x91 : 0x81;
    for (i = 0; i < digit_count; i += 2)
        bytes[n++] = digits[i] | ((i + 1 < digit_count ? digits[i + 1] : 0x0f) << 4);
    bytes[n++] = 0x00; /* PID */
    bytes[n++] = 0x08; /* UCS-2 */
    bytes[n++] = (guint8)(text_length + (multipart ? 6 : 0));
    if (multipart) {
        bytes[n++] = 0x05; /* UDH length */
        bytes[n++] = 0x00; /* 8-bit concatenation IE */
        bytes[n++] = 0x03;
        bytes[n++] = reference;
        bytes[n++] = total;
        bytes[n++] = part;
    }
    memcpy(bytes + n, text, text_length);
    n += text_length;
    pdu = g_array_sized_new(FALSE, FALSE, sizeof(guint8), n);
    g_array_append_vals(pdu, bytes, n);
    g_ptr_array_add(result, pdu);
    return TRUE;
}

static gboolean build_submit_pdus(const char *number, const char *text, GPtrArray **result, GError **error)
{
    guint8 digits[32];
    gsize digit_count = 0, i, offset;
    const unsigned char *p = (const unsigned char *)text;
    gboolean international = number[0] == '+';
    guint32 cp;
    GByteArray *utf16 = g_byte_array_new();
    GArray *chunks = g_array_new(FALSE, FALSE, sizeof(struct sms_chunk));
    GPtrArray *pdus = g_ptr_array_new_with_free_func((GDestroyNotify)g_array_unref);
    guint8 reference;

    if (international) number++;
    for (; *number; number++) {
        if (*number < '0' || *number > '9' || digit_count >= sizeof(digits) * 2) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "invalid recipient number"); goto fail;
        }
        digits[digit_count++] = (guint8)(*number - '0');
    }
    if (!digit_count) { g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "empty recipient number"); goto fail; }
    while (*p) {
        guint8 encoded[4];
        gsize encoded_length;
        if (!next_utf8(&p, &cp)) { g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "message is not valid UTF-8"); goto fail; }
        if (cp <= 0xffff) {
            encoded[0] = cp >> 8; encoded[1] = cp & 0xff; encoded_length = 2;
        } else {
            guint32 value = cp - 0x10000;
            guint16 high = 0xd800 | (value >> 10), low = 0xdc00 | (value & 0x3ff);
            encoded[0] = high >> 8; encoded[1] = high & 0xff;
            encoded[2] = low >> 8; encoded[3] = low & 0xff; encoded_length = 4;
        }
        g_byte_array_append(utf16, encoded, encoded_length);
    }
    if (!utf16->len) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "empty SMS text");
        goto fail;
    }

    offset = 0;
    while (offset < utf16->len) {
        struct sms_chunk chunk;
        gsize limit = utf16->len <= 140 ? 140 : 134;
        chunk.offset = offset;
        chunk.length = MIN(limit, utf16->len - offset);
        /* Never split a UTF-16 surrogate pair between concatenated parts. */
        if (offset + chunk.length < utf16->len && chunk.length >= 2 &&
            utf16->data[offset + chunk.length - 2] >= 0xd8 &&
            utf16->data[offset + chunk.length - 2] <= 0xdb)
            chunk.length -= 2;
        if (!chunk.length || chunks->len >= 10) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "SMS needs more than 10 segments");
            goto fail;
        }
        g_array_append_val(chunks, chunk);
        offset += chunk.length;
    }

    reference = (guint8)(((guint64)g_get_real_time() ^ (guint64)getpid()) & 0xff);
    for (i = 0; i < chunks->len; i++) {
        struct sms_chunk chunk = g_array_index(chunks, struct sms_chunk, i);
        if (!append_submit_pdu(pdus, digits, digit_count, international,
                               utf16->data + chunk.offset, chunk.length,
                               reference, (guint8)chunks->len, (guint8)(i + 1), error))
            goto fail;
    }
    g_array_unref(chunks);
    g_byte_array_unref(utf16);
    *result = pdus;
    return TRUE;

fail:
    g_array_unref(chunks);
    g_byte_array_unref(utf16);
    g_ptr_array_unref(pdus);
    return FALSE;
}

static int semi(guint8 value) { return (value & 0x0f) * 10 + ((value >> 4) & 0x0f); }

static char *decode_number(const guint8 *data, gsize length, guint digits, guint8 toa)
{
    GString *out = g_string_new((toa & 0x70) == 0x10 ? "+" : "");
    guint i;
    for (i = 0; i < digits && i / 2 < length; i++) {
        guint value = (i & 1) ? (data[i / 2] >> 4) : (data[i / 2] & 0x0f);
        if (value <= 9) g_string_append_c(out, '0' + value);
    }
    return g_string_free(out, FALSE);
}

static void write_smsc_state(const char *type, const char *digits)
{
    const char *path = "/var/run/xm14pro/smsc-state";
    char *temporary;
    FILE *file;

    if (!digits || !*digits) return;
    temporary = g_strdup_printf("%s.%ld", path, (long)getpid());
    file = fopen(temporary, "w");
    if (!file) { g_free(temporary); return; }
    fprintf(file, "smsc_address=%s\n", digits);
    if (type && *type) fprintf(file, "smsc_type=%s\n", type);
    fclose(file);
    if (rename(temporary, path) != 0) unlink(temporary);
    g_free(temporary);
}

static const gunichar gsm7_table[128] = {
    '@',0x00a3,'$',0x00a5,0x00e8,0x00e9,0x00f9,0x00ec,0x00f2,0x00c7,'\n',0x00d8,0x00f8,'\r',0x00c5,0x00e5,
    0x0394,'_',0x03a6,0x0393,0x039b,0x03a9,0x03a0,0x03a8,0x03a3,0x0398,0x039e,0x001b,0x00c6,0x00e6,0x00df,0x00c9,
    ' ','!','"','#',0x00a4,'%','&','\'', '(',')','*','+',',','-','.','/',
    '0','1','2','3','4','5','6','7','8','9',':',';','<','=','>','?',
    0x00a1,'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O',
    'P','Q','R','S','T','U','V','W','X','Y','Z',0x00c4,0x00d6,0x00d1,0x00dc,0x00a7,
    0x00bf,'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',
    'p','q','r','s','t','u','v','w','x','y','z',0x00e4,0x00f6,0x00f1,0x00fc,0x00e0
};

static char *decode_text(const guint8 *data, gsize length, guint8 dcs, guint udl, guint bit_offset)
{
    GString *out = g_string_new(NULL);
    guint i;
    if ((dcs & 0x0c) == 0x08) {
        gsize bytes = MIN((gsize)udl, length);
        for (i = 0; i + 1 < bytes; i += 2) {
            guint32 cp = ((guint32)data[i] << 8) | data[i + 1];
            if (cp >= 0xd800 && cp <= 0xdbff && i + 3 < bytes) {
                guint32 low = ((guint32)data[i + 2] << 8) | data[i + 3];
                if (low >= 0xdc00 && low <= 0xdfff) { cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00); i += 2; }
            }
            append_utf8(out, cp);
        }
    } else {
        for (i = 0; i < udl; i++) {
            guint bit = bit_offset + i * 7, pos = bit / 8, shift = bit % 8;
            guint value;
            if (pos >= length) break;
            value = (data[pos] >> shift) & 0x7f;
            if (shift > 1 && pos + 1 < length) value |= (data[pos + 1] << (8 - shift)) & 0x7f;
            if (value != 0x1b) append_utf8(out, gsm7_table[value]);
        }
    }
    return g_string_free(out, FALSE);
}

static gboolean print_pdu_json(const guint8 *pdu, gsize length)
{
    gsize pos = 0, address_bytes;
    guint8 smsc_len, first, toa, dcs, udl;
    guint digits, concat_ref = 0, concat_total = 0, concat_part = 0, text_udl, bit_offset = 0;
    gsize text_pos;
    char *smsc = NULL, *number = NULL, *text = NULL, timestamp[32] = "";
    const char *direction;
    if (length < 4) return FALSE;
    smsc_len = pdu[pos++];
    if (pos + smsc_len >= length) return FALSE;
    if (smsc_len >= 2) {
        smsc = decode_number(pdu + pos + 1, smsc_len - 1, (smsc_len - 1) * 2, pdu[pos]);
        write_smsc_state("pdu", smsc);
    }
    pos += smsc_len;
    first = pdu[pos++];
    if ((first & 3) == 0) {
        direction = "in";
        if (pos + 2 > length) return FALSE;
        digits = pdu[pos++]; toa = pdu[pos++]; address_bytes = (digits + 1) / 2;
        if (pos + address_bytes + 10 > length) return FALSE;
        number = decode_number(pdu + pos, address_bytes, digits, toa); pos += address_bytes;
        pos++; dcs = pdu[pos++];
        snprintf(timestamp, sizeof(timestamp), "%02d-%02d-%02d %02d:%02d:%02d",
                 semi(pdu[pos]), semi(pdu[pos+1]), semi(pdu[pos+2]), semi(pdu[pos+3]), semi(pdu[pos+4]), semi(pdu[pos+5]));
        pos += 7; udl = pdu[pos++];
    } else if ((first & 3) == 1) {
        direction = "out";
        if (pos + 3 > length) return FALSE;
        pos++; digits = pdu[pos++]; toa = pdu[pos++]; address_bytes = (digits + 1) / 2;
        if (pos + address_bytes + 3 > length) return FALSE;
        number = decode_number(pdu + pos, address_bytes, digits, toa); pos += address_bytes;
        pos++; dcs = pdu[pos++];
        switch ((first >> 3) & 3) { case 2: pos++; break; case 1: pos += 7; break; case 3: pos += 7; break; default: break; }
        if (pos >= length) { g_free(smsc); g_free(number); return FALSE; }
        udl = pdu[pos++];
    } else return FALSE;
    if (pos > length) { g_free(smsc); g_free(number); return FALSE; }
    text_pos = pos;
    text_udl = udl;
    if ((first & 0x40) && pos < length) {
        guint header_octets = (guint)pdu[pos] + 1;
        gsize ie = pos + 1, end = pos + header_octets;
        if (end > length) { g_free(smsc); g_free(number); return FALSE; }
        while (ie + 2 <= end) {
            guint8 id = pdu[ie++], ie_length = pdu[ie++];
            if (ie + ie_length > end) break;
            if (id == 0x00 && ie_length == 3) {
                concat_ref = pdu[ie]; concat_total = pdu[ie + 1]; concat_part = pdu[ie + 2];
            } else if (id == 0x08 && ie_length == 4) {
                concat_ref = ((guint)pdu[ie] << 8) | pdu[ie + 1];
                concat_total = pdu[ie + 2]; concat_part = pdu[ie + 3];
            }
            ie += ie_length;
        }
        if ((dcs & 0x0c) == 0x08) {
            text_pos += header_octets;
            text_udl = udl > header_octets ? udl - header_octets : 0;
        } else {
            guint header_septets = (header_octets * 8 + 6) / 7;
            text_udl = udl > header_septets ? udl - header_septets : 0;
            bit_offset = header_octets * 8;
        }
    }
    text = decode_text(pdu + text_pos, length - text_pos, dcs, text_udl, bit_offset);
    fputs("{\"ok\":true,\"direction\":", stdout); json_string(direction);
    fputs(",\"number\":", stdout); json_string(number);
    fputs(",\"smsc\":", stdout); json_string(smsc);
    fputs(",\"timestamp\":", stdout); json_string(timestamp);
    fputs(",\"text\":", stdout); json_string(text);
    if (concat_total > 1) printf(",\"concat_ref\":%u,\"concat_total\":%u,\"concat_part\":%u", concat_ref, concat_total, concat_part);
    fputs("}\n", stdout);
    g_free(smsc); g_free(number); g_free(text);
    return TRUE;
}

static void close_ready(QmiDevice *dev, GAsyncResult *res, gpointer unused)
{
    GError *error = NULL;
    qmi_device_close_finish(dev, res, &error);
    g_clear_error(&error);
    g_main_loop_quit(loop);
}

static void release_ready(QmiDevice *dev, GAsyncResult *res, gpointer unused)
{
    GError *error = NULL;
    qmi_device_release_client_finish(dev, res, &error);
    g_clear_error(&error);
    qmi_device_close_async(device, 5, NULL, (GAsyncReadyCallback)close_ready, NULL);
}

static void finish(gboolean success)
{
    exit_status = success ? EXIT_SUCCESS : EXIT_FAILURE;
    if (device && client)
        qmi_device_release_client(device, client, QMI_DEVICE_RELEASE_CLIENT_FLAGS_RELEASE_CID, 5, NULL, (GAsyncReadyCallback)release_ready, NULL);
    else if (device)
        qmi_device_close_async(device, 5, NULL, (GAsyncReadyCallback)close_ready, NULL);
    else
        g_main_loop_quit(loop);
}

static void fail_error(const char *operation, GError *error)
{
    fprintf(stderr, "%s: %s\n", operation, error ? error->message : "unknown error");
    g_clear_error(&error); finish(FALSE);
}

static void dial_ready(QmiClientVoice *voice, GAsyncResult *res, gpointer unused)
{
    GError *error = NULL; guint8 id = 0;
    QmiMessageVoiceDialCallOutput *out = qmi_client_voice_dial_call_finish(voice, res, &error);
    if (!out || !qmi_message_voice_dial_call_output_get_result(out, &error)) { if (out) qmi_message_voice_dial_call_output_unref(out); fail_error("dial failed", error); return; }
    qmi_message_voice_dial_call_output_get_call_id(out, &id, NULL);
    printf("{\"ok\":true,\"call_id\":%u}\n", id); qmi_message_voice_dial_call_output_unref(out); finish(TRUE);
}

static void answer_ready(QmiClientVoice *voice, GAsyncResult *res, gpointer unused)
{
    GError *error = NULL; QmiMessageVoiceAnswerCallOutput *out = qmi_client_voice_answer_call_finish(voice, res, &error);
    if (!out || !qmi_message_voice_answer_call_output_get_result(out, &error)) { if (out) qmi_message_voice_answer_call_output_unref(out); fail_error("answer failed", error); return; }
    fputs("{\"ok\":true}\n", stdout); qmi_message_voice_answer_call_output_unref(out); finish(TRUE);
}

static void end_ready(QmiClientVoice *voice, GAsyncResult *res, gpointer unused)
{
    GError *error = NULL; QmiMessageVoiceEndCallOutput *out = qmi_client_voice_end_call_finish(voice, res, &error);
    if (!out || !qmi_message_voice_end_call_output_get_result(out, &error)) { if (out) qmi_message_voice_end_call_output_unref(out); fail_error("hangup failed", error); return; }
    fputs("{\"ok\":true}\n", stdout); qmi_message_voice_end_call_output_unref(out); finish(TRUE);
}

static void dtmf_ready(QmiClientVoice *voice, GAsyncResult *res, gpointer unused)
{
    GError *error = NULL;
    QmiMessageVoiceBurstDtmfOutput *out = qmi_client_voice_burst_dtmf_finish(voice, res, &error);
    if (!out || !qmi_message_voice_burst_dtmf_output_get_result(out, &error)) {
        if (out) qmi_message_voice_burst_dtmf_output_unref(out);
        fail_error("DTMF failed", error);
        return;
    }
    fputs("{\"ok\":true}\n", stdout);
    qmi_message_voice_burst_dtmf_output_unref(out);
    finish(TRUE);
}

static void send_current_sms(void);

static void print_sms_send_failure_details(QmiMessageWmsRawSendOutput *out, GError *error)
{
    QmiWmsMessageDeliveryFailureType failure_type;
    QmiWmsGsmUmtsRpCause rp_cause;
    QmiWmsGsmUmtsTpCause tp_cause;
    const char *name;

    if (error)
        fprintf(stderr, "SMS QMI error: domain=%s code=%d\n",
                g_quark_to_string(error->domain), error->code);
    if (!out)
        return;

    if (qmi_message_wms_raw_send_output_get_message_delivery_failure_type(
            out, &failure_type, NULL)) {
        name = qmi_wms_message_delivery_failure_type_get_string(failure_type);
        fprintf(stderr, "SMS modem detail: delivery_failure=%s(%u)\n",
                name ? name : "unknown", (unsigned)failure_type);
    }
    if (qmi_message_wms_raw_send_output_get_gsm_wcdma_cause_info(
            out, &rp_cause, &tp_cause, NULL)) {
        name = qmi_wms_gsm_umts_rp_cause_get_string(rp_cause);
        fprintf(stderr, "SMS modem detail: rp_cause=%s(%u)\n",
                name ? name : "unknown", (unsigned)rp_cause);
        name = qmi_wms_gsm_umts_tp_cause_get_string(tp_cause);
        fprintf(stderr, "SMS modem detail: tp_cause=%s(%u)\n",
                name ? name : "unknown", (unsigned)tp_cause);
    }
}

static void sms_send_ready(QmiClientWms *wms, GAsyncResult *res, gpointer unused)
{
    GError *error = NULL; guint16 id = 0; QmiMessageWmsRawSendOutput *out = qmi_client_wms_raw_send_finish(wms, res, &error);
    if (!out || !qmi_message_wms_raw_send_output_get_result(out, &error)) {
        print_sms_send_failure_details(out, error);
        if (out) qmi_message_wms_raw_send_output_unref(out);
        fail_error(sms_on_ims ? "SMS send over IMS failed" : "SMS send over circuit-switched WMS failed", error);
        return;
    }
    qmi_message_wms_raw_send_output_get_message_id(out, &id, NULL);
    sms_last_message_id = id;
    qmi_message_wms_raw_send_output_unref(out);
    sms_pdu_index++;
    if (sms_pdu_index < sms_pdus->len) {
        send_current_sms();
        return;
    }
    printf("{\"ok\":true,\"message_id\":%u,\"segments\":%u,\"transport\":\"%s\"}\n",
           sms_last_message_id, sms_pdus->len, sms_on_ims ? "ims" : "cs");
    finish(TRUE);
}

static void send_current_sms(void)
{
    GError *error = NULL;
    GArray *pdu = g_ptr_array_index(sms_pdus, sms_pdu_index);
    QmiMessageWmsRawSendInput *in = qmi_message_wms_raw_send_input_new();
    if (!qmi_message_wms_raw_send_input_set_raw_message_data(
            in, QMI_WMS_MESSAGE_FORMAT_GSM_WCDMA_POINT_TO_POINT, pdu, &error)) {
        qmi_message_wms_raw_send_input_unref(in);
        fail_error("invalid SMS PDU", error);
        return;
    }
    if (sms_on_ims && !qmi_message_wms_raw_send_input_set_sms_on_ims(in, TRUE, &error)) {
        qmi_message_wms_raw_send_input_unref(in);
        fail_error("cannot select SMS over IMS", error);
        return;
    }
    qmi_client_wms_raw_send(QMI_CLIENT_WMS(client), in, 30, NULL,
                            (GAsyncReadyCallback)sms_send_ready, NULL);
    qmi_message_wms_raw_send_input_unref(in);
}

static void sms_read_ready(QmiClientWms *wms, GAsyncResult *res, gpointer unused)
{
    GError *error = NULL; QmiWmsMessageTagType tag; QmiWmsMessageFormat format; GArray *raw = NULL;
    QmiMessageWmsRawReadOutput *out = qmi_client_wms_raw_read_finish(wms, res, &error);
    if (!out || !qmi_message_wms_raw_read_output_get_result(out, &error) ||
        !qmi_message_wms_raw_read_output_get_raw_message_data(out, &tag, &format, &raw, &error)) {
        if (out) qmi_message_wms_raw_read_output_unref(out); fail_error("SMS read failed", error); return;
    }
    if (format != QMI_WMS_MESSAGE_FORMAT_GSM_WCDMA_POINT_TO_POINT || !raw || !print_pdu_json((guint8 *)raw->data, raw->len)) {
        qmi_message_wms_raw_read_output_unref(out); fprintf(stderr, "SMS PDU format is unsupported\n"); finish(FALSE); return;
    }
    qmi_message_wms_raw_read_output_unref(out); finish(TRUE);
}

/* libqmi knows the WMS SMSC indication but qmicli does not expose the older
 * Get SMSC Address request (0x0034).  Send that standard read-only request
 * through the already allocated WMS client and decode its documented TLV. */
static void smsc_ready(QmiDevice *dev, GAsyncResult *res, gpointer unused)
{
    GError *error = NULL;
    QmiMessage *reply;
    gsize tlv, offset = 0;
    guint16 result = 1, protocol_error = 0;
    char type[4] = { 0 };
    char *digits = NULL;

    reply = qmi_device_command_full_finish(dev, res, &error);
    if (!reply) { fail_error("SMSC query failed", error); return; }

    tlv = qmi_message_tlv_read_init(reply, 0x02, NULL, &error);
    if (!tlv ||
        !qmi_message_tlv_read_guint16(reply, tlv, &offset, QMI_ENDIAN_LITTLE, &result, &error) ||
        !qmi_message_tlv_read_guint16(reply, tlv, &offset, QMI_ENDIAN_LITTLE, &protocol_error, &error) ||
        result != 0) {
        if (!error)
            error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED, "modem error %u", protocol_error);
        qmi_message_unref(reply); fail_error("SMSC query failed", error); return;
    }

    offset = 0;
    tlv = qmi_message_tlv_read_init(reply, 0x01, NULL, &error);
    if (!tlv ||
        !qmi_message_tlv_read_fixed_size_string(reply, tlv, &offset, 3, type, &error) ||
        !qmi_message_tlv_read_string(reply, tlv, &offset, 1, 0, &digits, &error)) {
        qmi_message_unref(reply); fail_error("SMSC response decode failed", error); return;
    }

    fputs("{\"ok\":true,\"address\":", stdout); json_string(digits);
    fputs(",\"type\":", stdout); json_string(type); fputs("}\n", stdout);
    g_free(digits); qmi_message_unref(reply); finish(TRUE);
}

static void ack_ready(QmiClientWms *wms, GAsyncResult *res, gpointer unused)
{
    GError *error = NULL;
    QmiMessageWmsSendAckOutput *out = qmi_client_wms_send_ack_finish(wms, res, &error);
    if (!out || !qmi_message_wms_send_ack_output_get_result(out, &error))
        fprintf(stderr, "SMS acknowledgement failed: %s\n", error ? error->message : "unknown error");
    g_clear_error(&error);
    if (out) qmi_message_wms_send_ack_output_unref(out);
}

static void smsc_address(QmiClientWms *wms, QmiIndicationWmsSmscAddressOutput *report, gpointer unused)
{
    const gchar *type = NULL, *digits = NULL;
    GError *error = NULL;
    (void)wms; (void)unused;
    if (!qmi_indication_wms_smsc_address_output_get_address(report, &type, &digits, &error)) {
        g_clear_error(&error);
        return;
    }
    write_smsc_state(type, digits);
}

static void event_report(QmiClientWms *wms, QmiIndicationWmsEventReportOutput *report, gpointer unused)
{
    QmiWmsAckIndicator ack = QMI_WMS_ACK_INDICATOR_DO_NOT_SEND;
    QmiWmsMessageFormat format;
    guint32 transaction = 0;
    GArray *raw = NULL;
    GError *error = NULL;
    char *path;
    FILE *file;

    if (!qmi_indication_wms_event_report_output_get_transfer_route_mt_message(report, &ack, &transaction, &format, &raw, &error)) {
        g_clear_error(&error);
        return;
    }
    if (format == QMI_WMS_MESSAGE_FORMAT_GSM_WCDMA_POINT_TO_POINT && raw && raw->len) {
        path = g_strdup_printf("%s/%" G_GINT64_FORMAT ".pdu", argument1, g_get_real_time());
        file = fopen(path, "wx");
        if (file) {
            write_hex(file, (guint8 *)raw->data, raw->len);
            fputc('\n', file);
            fclose(file);
            fprintf(stderr, "received SMS PDU in %s\n", path);
        } else fprintf(stderr, "cannot store received SMS: %s\n", g_strerror(errno));
        g_free(path);
    }
    if (ack == QMI_WMS_ACK_INDICATOR_SEND) {
        QmiMessageWmsSendAckInput *in = qmi_message_wms_send_ack_input_new();
        qmi_message_wms_send_ack_input_set_information(in, transaction, QMI_WMS_MESSAGE_PROTOCOL_WCDMA, TRUE, NULL);
        qmi_client_wms_send_ack(wms, in, 10, NULL, (GAsyncReadyCallback)ack_ready, NULL);
        qmi_message_wms_send_ack_input_unref(in);
    }
}

static void event_report_ready(QmiClientWms *wms, GAsyncResult *res, gpointer unused)
{
    GError *error = NULL;
    QmiMessageWmsSetEventReportOutput *out = qmi_client_wms_set_event_report_finish(wms, res, &error);
    if (!out || !qmi_message_wms_set_event_report_output_get_result(out, &error)) {
        if (out) qmi_message_wms_set_event_report_output_unref(out);
        fail_error("SMS receive registration failed", error);
        return;
    }
    qmi_message_wms_set_event_report_output_unref(out);
    {
        FILE *ready = fopen("/var/run/xm14pro/sms-listener.ready", "w");
        if (ready) { fputs("ready\n", ready); fclose(ready); }
    }
    fprintf(stderr, "SMS receive listener ready\n");
}

static void voice_seed_ready(QmiClientVoice *voice, GAsyncResult *res, gpointer unused)
{
    GError *error = NULL;
    GArray *calls = NULL, *numbers = NULL;
    QmiMessageVoiceGetAllCallInfoOutputCallInformationCall call;
    QmiMessageVoiceGetAllCallInfoOutput *out = qmi_client_voice_get_all_call_info_finish(voice, res, &error);

    voice_query_pending = FALSE;

    if (!out || !qmi_message_voice_get_all_call_info_output_get_result(out, &error)) {
        fprintf(stderr, "initial call state query failed: %s\n", error ? error->message : "unknown error");
        g_clear_error(&error);
        if (out) qmi_message_voice_get_all_call_info_output_unref(out);
        return;
    }
    if (!qmi_message_voice_get_all_call_info_output_get_call_information(out, &calls, &error)) {
        g_clear_error(&error);
        write_call_state(0, 0, 0, 0, 0, 0, NULL);
        qmi_message_voice_get_all_call_info_output_unref(out);
        return;
    }
    qmi_message_voice_get_all_call_info_output_get_remote_party_number(out, &numbers, NULL);
    if (!calls || !calls->len) write_call_state(0, 0, 0, 0, 0, 0, NULL);
    else {
        call = g_array_index(calls, QmiMessageVoiceGetAllCallInfoOutputCallInformationCall, 0);
        write_call_state(calls->len, call.id, call.state, call.type, call.direction, call.mode,
                         response_number(numbers, call.id));
    }
    qmi_message_voice_get_all_call_info_output_unref(out);
    fprintf(stderr, "Voice call listener ready\n");
}

static gboolean voice_poll(gpointer unused)
{
    (void)unused;
    if (!voice_query_pending && client) {
        voice_query_pending = TRUE;
        qmi_client_voice_get_all_call_info(QMI_CLIENT_VOICE(client), NULL, 10, NULL,
                                           (GAsyncReadyCallback)voice_seed_ready, NULL);
    }
    return G_SOURCE_CONTINUE;
}

static void run_command(void)
{
    GError *error = NULL;
    if (command == CMD_DIAL) {
        QmiMessageVoiceDialCallInput *in = qmi_message_voice_dial_call_input_new();
        if (!qmi_message_voice_dial_call_input_set_calling_number(in, argument1, &error)) { qmi_message_voice_dial_call_input_unref(in); fail_error("invalid number", error); return; }
        qmi_client_voice_dial_call(QMI_CLIENT_VOICE(client), in, 20, NULL, (GAsyncReadyCallback)dial_ready, NULL); qmi_message_voice_dial_call_input_unref(in);
    } else if (command == CMD_ANSWER) {
        QmiMessageVoiceAnswerCallInput *in = qmi_message_voice_answer_call_input_new();
        qmi_message_voice_answer_call_input_set_call_id(in, (guint8)strtoul(argument1, NULL, 10), NULL);
        qmi_client_voice_answer_call(QMI_CLIENT_VOICE(client), in, 20, NULL, (GAsyncReadyCallback)answer_ready, NULL); qmi_message_voice_answer_call_input_unref(in);
    } else if (command == CMD_END) {
        QmiMessageVoiceEndCallInput *in = qmi_message_voice_end_call_input_new();
        qmi_message_voice_end_call_input_set_call_id(in, (guint8)strtoul(argument1, NULL, 10), NULL);
        qmi_client_voice_end_call(QMI_CLIENT_VOICE(client), in, 20, NULL, (GAsyncReadyCallback)end_ready, NULL); qmi_message_voice_end_call_input_unref(in);
    } else if (command == CMD_DTMF) {
        QmiMessageVoiceBurstDtmfInput *in = qmi_message_voice_burst_dtmf_input_new();
        if (!qmi_message_voice_burst_dtmf_input_set_data(in, (guint8)strtoul(argument1, NULL, 10), argument2, &error)) {
            qmi_message_voice_burst_dtmf_input_unref(in);
            fail_error("invalid DTMF", error);
            return;
        }
        qmi_client_voice_burst_dtmf(QMI_CLIENT_VOICE(client), in, 20, NULL, (GAsyncReadyCallback)dtmf_ready, NULL);
        qmi_message_voice_burst_dtmf_input_unref(in);
    } else if (command == CMD_SMS_SEND) {
        send_current_sms();
    } else if (command == CMD_SMS_READ) {
        QmiMessageWmsRawReadInput *in = qmi_message_wms_raw_read_input_new();
        QmiWmsStorageType storage = !strcmp(argument1, "nv") ? QMI_WMS_STORAGE_TYPE_NV : QMI_WMS_STORAGE_TYPE_UIM;
        qmi_message_wms_raw_read_input_set_message_memory_storage_id(in, storage, message_index, NULL);
        qmi_message_wms_raw_read_input_set_message_mode(in, QMI_WMS_MESSAGE_MODE_GSM_WCDMA, NULL);
        qmi_client_wms_raw_read(QMI_CLIENT_WMS(client), in, 20, NULL, (GAsyncReadyCallback)sms_read_ready, NULL); qmi_message_wms_raw_read_input_unref(in);
    } else if (command == CMD_SMSC_GET) {
        QmiMessage *request = qmi_message_new(QMI_SERVICE_WMS,
                                              qmi_client_get_cid(client),
                                              qmi_client_get_next_transaction_id(client),
                                              0x0034);
        qmi_device_command_full(device, request, NULL, 15, NULL,
                                (GAsyncReadyCallback)smsc_ready, NULL);
        qmi_message_unref(request);
    } else if (command == CMD_VOICE_LISTEN) {
        g_signal_connect(client, "all-call-status", G_CALLBACK(all_call_status), NULL);
        voice_query_pending = TRUE;
        qmi_client_voice_get_all_call_info(QMI_CLIENT_VOICE(client), NULL, 15, NULL,
                                           (GAsyncReadyCallback)voice_seed_ready, NULL);
        g_timeout_add_seconds(3, voice_poll, NULL);
    } else {
        QmiMessageWmsSetEventReportInput *in = qmi_message_wms_set_event_report_input_new();
        g_signal_connect(client, "event-report", G_CALLBACK(event_report), NULL);
        g_signal_connect(client, "smsc-address", G_CALLBACK(smsc_address), NULL);
        qmi_message_wms_set_event_report_input_set_new_mt_message_indicator(in, TRUE, NULL);
        qmi_client_wms_set_event_report(QMI_CLIENT_WMS(client), in, 15, NULL, (GAsyncReadyCallback)event_report_ready, NULL);
        qmi_message_wms_set_event_report_input_unref(in);
    }
}

static void allocate_ready(QmiDevice *dev, GAsyncResult *res, gpointer unused)
{
    GError *error = NULL; client = qmi_device_allocate_client_finish(dev, res, &error);
    if (!client) { fail_error("QMI client allocation failed", error); return; }
    run_command();
}

static void open_ready(QmiDevice *dev, GAsyncResult *res, gpointer unused)
{
    GError *error = NULL; QmiService service;
    if (!qmi_device_open_finish(dev, res, &error)) { fail_error("QMI device open failed", error); return; }
    service = (command <= CMD_DTMF || command == CMD_VOICE_LISTEN) ? QMI_SERVICE_VOICE : QMI_SERVICE_WMS;
    qmi_device_allocate_client(device, service, QMI_CID_NONE, 10, NULL, (GAsyncReadyCallback)allocate_ready, NULL);
}

static void device_ready(GObject *source, GAsyncResult *res, gpointer unused)
{
    GError *error = NULL; device = qmi_device_new_from_node_finish(res, &error);
    if (!device) { fail_error("QMI device creation failed", error); return; }
    qmi_device_open(device, QMI_DEVICE_OPEN_FLAGS_NONE, 10, NULL, (GAsyncReadyCallback)open_ready, NULL);
}

static void bus_ready(GObject *source, GAsyncResult *res, gpointer unused)
{
    GError *error = NULL; QrtrNode *node;
    bus = qrtr_bus_new_finish(res, &error);
    if (!bus) { fail_error("QRTR bus open failed", error); return; }
    node = qrtr_bus_peek_node(bus, 0);
    if (!node) { fprintf(stderr, "QRTR node 0 is unavailable\n"); finish(FALSE); return; }
    qmi_device_new_from_node(node, NULL, (GAsyncReadyCallback)device_ready, NULL);
}

static gboolean timeout_cb(gpointer unused)
{
    fprintf(stderr, "telephony operation timed out\n"); finish(FALSE); return G_SOURCE_REMOVE;
}

int main(int argc, char **argv)
{
    GError *error = NULL; char *end = NULL;
	if (argc == 3 && !strcmp(argv[1], "pdu-decode")) {
		GArray *raw = NULL;
		if (!parse_hex(argv[2], &raw) || !print_pdu_json((guint8 *)raw->data, raw->len)) {
			if (raw) g_array_unref(raw);
			fprintf(stderr, "invalid or unsupported SMS PDU\n");
			return EXIT_FAILURE;
		}
		g_array_unref(raw);
		return EXIT_SUCCESS;
	}
    if (argc < 3) {
        fprintf(stderr, "usage: %s dial NUMBER | answer CALL_ID | end CALL_ID | dtmf CALL_ID DIGIT | sms-send NUMBER TEXT | sms-send-ims NUMBER TEXT | sms-read uim|nv INDEX | smsc-get live | sms-listen DIRECTORY | voice-listen STATE_FILE | pdu-decode HEX\n", argv[0]);
        return EXIT_FAILURE;
    }
    argument1 = argv[2]; argument2 = argc > 3 ? argv[3] : NULL;
    if (!strcmp(argv[1], "dial") && argc == 3) command = CMD_DIAL;
    else if (!strcmp(argv[1], "answer") && argc == 3) command = CMD_ANSWER;
    else if (!strcmp(argv[1], "end") && argc == 3) command = CMD_END;
    else if (!strcmp(argv[1], "dtmf") && argc == 4) command = CMD_DTMF;
    else if (!strcmp(argv[1], "sms-send") && argc == 4) command = CMD_SMS_SEND;
    else if (!strcmp(argv[1], "sms-send-ims") && argc == 4) { command = CMD_SMS_SEND; sms_on_ims = TRUE; }
    else if (!strcmp(argv[1], "sms-read") && argc == 4 && (!strcmp(argument1, "uim") || !strcmp(argument1, "nv"))) command = CMD_SMS_READ;
	else if (!strcmp(argv[1], "smsc-get") && argc == 3 && !strcmp(argument1, "live")) command = CMD_SMSC_GET;
	else if (!strcmp(argv[1], "sms-listen") && argc == 3) command = CMD_SMS_LISTEN;
	else if (!strcmp(argv[1], "voice-listen") && argc == 3) command = CMD_VOICE_LISTEN;
    else { fprintf(stderr, "invalid telephony command\n"); return EXIT_FAILURE; }

    if (command == CMD_DIAL && strspn(argument1, "+0123456789*#") != strlen(argument1)) { fprintf(stderr, "invalid phone number\n"); return EXIT_FAILURE; }
    if (command == CMD_ANSWER || command == CMD_END || command == CMD_DTMF || command == CMD_SMS_READ) {
        errno = 0; message_index = strtoul(command == CMD_SMS_READ ? argument2 : argument1, &end, 10);
        if (errno || !end || *end || message_index > G_MAXUINT8 && command != CMD_SMS_READ) { fprintf(stderr, "invalid numeric identifier\n"); return EXIT_FAILURE; }
    }
    if (command == CMD_DTMF && (!argument2 || strlen(argument2) != 1 || !strchr("0123456789*#ABCD", argument2[0]))) {
        fprintf(stderr, "invalid DTMF digit\n");
        return EXIT_FAILURE;
    }
    if (command == CMD_SMS_SEND && !build_submit_pdus(argument1, argument2, &sms_pdus, &error)) {
        fprintf(stderr, "cannot encode SMS: %s\n", error->message); g_error_free(error); return EXIT_FAILURE;
    }
	if (command == CMD_SMS_LISTEN && g_mkdir_with_parents(argument1, 0700) != 0) {
		fprintf(stderr, "cannot create SMS inbox: %s\n", g_strerror(errno));
		return EXIT_FAILURE;
	}
    loop = g_main_loop_new(NULL, FALSE);
	if (command != CMD_SMS_LISTEN && command != CMD_VOICE_LISTEN)
		g_timeout_add_seconds(command == CMD_SMS_SEND ? 20 + 30 * sms_pdus->len : 40, timeout_cb, NULL);
    qrtr_bus_new(1500, NULL, (GAsyncReadyCallback)bus_ready, NULL);
    g_main_loop_run(loop);
    if (sms_pdus) g_ptr_array_unref(sms_pdus);
    g_clear_object(&client); g_clear_object(&device); g_clear_object(&bus); g_main_loop_unref(loop);
    return exit_status;
}
