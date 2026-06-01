/* 飞书 WebSocket protobuf 帧编解码。 */

#include "channels/feishu/feishu_ws_client.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool pb_read_varint(const uint8_t *buf, size_t len, size_t *pos, uint64_t *out)
{
    uint64_t v = 0;
    int shift = 0;
    while (*pos < len && shift <= 63) {
        uint8_t b = buf[(*pos)++];
        v |= ((uint64_t)(b & 0x7F)) << shift;
        if ((b & 0x80) == 0) {
            *out = v;
            return true;
        }
        shift += 7;
    }
    return false;
}

static bool pb_skip_field(const uint8_t *buf, size_t len, size_t *pos, uint8_t wire_type)
{
    uint64_t n = 0;
    switch (wire_type) {
        case 0:
            return pb_read_varint(buf, len, pos, &n);
        case 1:
            if (*pos + 8 > len) return false;
            *pos += 8;
            return true;
        case 2:
            if (!pb_read_varint(buf, len, pos, &n)) return false;
            if (*pos + (size_t)n > len) return false;
            *pos += (size_t)n;
            return true;
        case 5:
            if (*pos + 4 > len) return false;
            *pos += 4;
            return true;
        default:
            return false;
    }
}

static bool pb_parse_header_msg(const uint8_t *buf, size_t len, feishu_ws_header_t *header)
{
    memset(header, 0, sizeof(*header));
    size_t pos = 0;
    while (pos < len) {
        uint64_t tag = 0, slen = 0;
        if (!pb_read_varint(buf, len, &pos, &tag)) return false;
        uint32_t field = (uint32_t)(tag >> 3);
        uint8_t wt = (uint8_t)(tag & 0x07);
        if (wt != 2) {
            if (!pb_skip_field(buf, len, &pos, wt)) return false;
            continue;
        }
        if (!pb_read_varint(buf, len, &pos, &slen)) return false;
        if (pos + (size_t)slen > len) return false;
        if (field == 1) {
            size_t n = (slen < sizeof(header->key) - 1) ? (size_t)slen : sizeof(header->key) - 1;
            memcpy(header->key, buf + pos, n);
            header->key[n] = '\0';
        } else if (field == 2) {
            size_t n = (slen < sizeof(header->value) - 1) ? (size_t)slen : sizeof(header->value) - 1;
            memcpy(header->value, buf + pos, n);
            header->value[n] = '\0';
        }
        pos += (size_t)slen;
    }
    return true;
}

bool feishu_ws_parse_frame(const uint8_t *buf, size_t len, feishu_ws_frame_t *frame)
{
    if (!buf || !frame) return false;
    memset(frame, 0, sizeof(*frame));
    size_t pos = 0;
    while (pos < len) {
        uint64_t tag = 0, v = 0, blen = 0;
        if (!pb_read_varint(buf, len, &pos, &tag)) return false;
        uint32_t field = (uint32_t)(tag >> 3);
        uint8_t wt = (uint8_t)(tag & 0x07);
        if (field == 1 && wt == 0) {
            if (!pb_read_varint(buf, len, &pos, &frame->seq_id)) return false;
        } else if (field == 2 && wt == 0) {
            if (!pb_read_varint(buf, len, &pos, &frame->log_id)) return false;
        } else if (field == 3 && wt == 0) {
            if (!pb_read_varint(buf, len, &pos, &v)) return false;
            frame->service = (int32_t)v;
        } else if (field == 4 && wt == 0) {
            if (!pb_read_varint(buf, len, &pos, &v)) return false;
            frame->method = (int32_t)v;
        } else if (field == 5 && wt == 2) {
            if (!pb_read_varint(buf, len, &pos, &blen)) return false;
            if (pos + (size_t)blen > len) return false;
            if (frame->header_count < FEISHU_WS_MAX_HEADERS) {
                pb_parse_header_msg(buf + pos, (size_t)blen, &frame->headers[frame->header_count++]);
            }
            pos += (size_t)blen;
        } else if (field == 8 && wt == 2) {
            if (!pb_read_varint(buf, len, &pos, &blen)) return false;
            if (pos + (size_t)blen > len) return false;
            frame->payload = buf + pos;
            frame->payload_len = (size_t)blen;
            pos += (size_t)blen;
        } else {
            if (!pb_skip_field(buf, len, &pos, wt)) return false;
        }
    }
    return true;
}

const char *feishu_ws_frame_header_value(const feishu_ws_frame_t *frame, const char *key)
{
    if (!frame || !key) return NULL;
    for (size_t i = 0; i < frame->header_count; i++) {
        if (strcmp(frame->headers[i].key, key) == 0) {
            return frame->headers[i].value;
        }
    }
    return NULL;
}

static bool pb_write_varint(uint8_t *buf, size_t cap, size_t *pos, uint64_t value)
{
    while (*pos < cap) {
        uint8_t byte = (uint8_t)(value & 0x7F);
        value >>= 7;
        if (value) {
            buf[(*pos)++] = byte | 0x80;
        } else {
            buf[(*pos)++] = byte;
            return true;
        }
    }
    return false;
}

static bool pb_write_tag(uint8_t *buf, size_t cap, size_t *pos, uint32_t field, uint8_t wire_type)
{
    uint64_t tag = ((uint64_t)field << 3) | (wire_type & 0x07);
    return pb_write_varint(buf, cap, pos, tag);
}

static bool pb_write_bytes(uint8_t *buf, size_t cap, size_t *pos, uint32_t field,
                           const uint8_t *data, size_t len)
{
    if (!pb_write_tag(buf, cap, pos, field, 2)) return false;
    if (!pb_write_varint(buf, cap, pos, len)) return false;
    if (*pos + len > cap) return false;
    memcpy(buf + *pos, data, len);
    *pos += len;
    return true;
}

static bool pb_write_string(uint8_t *buf, size_t cap, size_t *pos, uint32_t field, const char *s)
{
    return pb_write_bytes(buf, cap, pos, field, (const uint8_t *)s, strlen(s));
}

static bool ws_encode_header(uint8_t *dst, size_t cap, size_t *out_len,
                             const char *key, const char *value)
{
    size_t pos = 0;
    if (!pb_write_string(dst, cap, &pos, 1, key)) return false;
    if (!pb_write_string(dst, cap, &pos, 2, value)) return false;
    *out_len = pos;
    return true;
}

int feishu_ws_send_frame(feishu_ws_conn_t *conn,
                         const feishu_ws_frame_t *frame,
                         const uint8_t *payload,
                         size_t payload_len)
{
    if (!conn || !frame) return -1;

    uint8_t out[2048];
    size_t pos = 0;
    if (!pb_write_tag(out, sizeof(out), &pos, 1, 0) || !pb_write_varint(out, sizeof(out), &pos, frame->seq_id)) return -1;
    if (!pb_write_tag(out, sizeof(out), &pos, 2, 0) || !pb_write_varint(out, sizeof(out), &pos, frame->log_id)) return -1;
    if (!pb_write_tag(out, sizeof(out), &pos, 3, 0) || !pb_write_varint(out, sizeof(out), &pos, (uint32_t)frame->service)) return -1;
    if (!pb_write_tag(out, sizeof(out), &pos, 4, 0) || !pb_write_varint(out, sizeof(out), &pos, (uint32_t)frame->method)) return -1;

    for (size_t i = 0; i < frame->header_count; i++) {
        uint8_t hb[256];
        size_t hlen = 0;
        if (!ws_encode_header(hb, sizeof(hb), &hlen, frame->headers[i].key, frame->headers[i].value)) return -1;
        if (!pb_write_bytes(out, sizeof(out), &pos, 5, hb, hlen)) return -1;
    }
    if (payload && payload_len > 0) {
        if (!pb_write_bytes(out, sizeof(out), &pos, 8, payload, payload_len)) return -1;
    }
    return feishu_ws_send_frame_raw(conn, FEISHU_WS_OPCODE_BINARY, out, pos) ? (int)pos : -1;
}
