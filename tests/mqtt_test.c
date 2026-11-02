#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mqtt_wire.h"

struct stream {
    const unsigned char *input;
    size_t input_length;
    size_t position;
    unsigned char output[256];
    size_t output_length;
};

static int read_bytes(void *context, void *buffer, size_t length)
{
    struct stream *stream = context;
    if (length > stream->input_length - stream->position)
        return -1;
    memcpy(buffer, stream->input + stream->position, length);
    stream->position += length;
    return 0;
}

static int write_bytes(void *context, const void *buffer, size_t length)
{
    struct stream *stream = context;
    if (length > sizeof(stream->output) - stream->output_length)
        return -1;
    memcpy(stream->output + stream->output_length, buffer, length);
    stream->output_length += length;
    return 0;
}

static struct mqtt_wire_io make_io(struct stream *stream,
    const unsigned char *input, size_t length)
{
    struct mqtt_wire_io io = {stream, read_bytes, write_bytes};
    memset(stream, 0, sizeof(*stream));
    stream->input = input;
    stream->input_length = length;
    return io;
}

int main(void)
{
    static const unsigned char connack[] = {0x20, 2, 0, 0};
    static const unsigned char failed_connack[] = {0x20, 2, 0, 5};
    static const unsigned char suback[] = {0x90, 3, 0, 1, 0};
    static const unsigned char failed_suback[] = {0x90, 3, 0, 1, 0x80};
    static const unsigned char wrong_id[] = {0x90, 3, 0, 2, 0};
    static const unsigned char oversized[] = {0x30, 0x80, 0x22};
    static const unsigned char noncanonical[] = {0x30, 0x80, 0x00};
    static const unsigned char malformed[] = {0x30, 0x80, 0x80, 0x80, 0x80};
    static const unsigned char publish[] = {0, 2, '/', 'a', '{', '}'};
    struct stream stream;
    struct mqtt_wire_io io;
    unsigned char header, body[32];
    const unsigned char *payload;
    size_t length;

    io = make_io(&stream, connack, sizeof(connack));
    assert(mqtt_wire_connect(&io, "client") == 0);
    assert(stream.output[0] == 0x10 && stream.output[stream.output_length - 1] == 't');
    io = make_io(&stream, failed_connack, sizeof(failed_connack));
    assert(mqtt_wire_connect(&io, "client") != 0);
    io = make_io(&stream, connack, 3);
    assert(mqtt_wire_connect(&io, "client") != 0);
    io = make_io(&stream, suback, sizeof(suback));
    assert(mqtt_wire_subscribe(&io, "/a") == 0 && stream.output[0] == 0x82);
    assert(mqtt_wire_read(&io, &header, body, sizeof(body), &length) == 0 &&
        mqtt_wire_suback(header, body, length) == 0);
    assert(mqtt_wire_suback(failed_suback[0], failed_suback + 2, 3) != 0);
    assert(mqtt_wire_suback(wrong_id[0], wrong_id + 2, 3) != 0);
    io = make_io(&stream, oversized, sizeof(oversized));
    assert(mqtt_wire_read(&io, &header, body, sizeof(body), &length) != 0);
    io = make_io(&stream, noncanonical, sizeof(noncanonical));
    assert(mqtt_wire_read(&io, &header, body, sizeof(body), &length) != 0);
    io = make_io(&stream, malformed, sizeof(malformed));
    assert(mqtt_wire_read(&io, &header, body, sizeof(body), &length) != 0);
    assert(mqtt_wire_publish(0x30, publish, sizeof(publish), "/a",
        &payload, &length) == 0 && length == 2 && memcmp(payload, "{}", 2) == 0);
    assert(mqtt_wire_publish(0x32, publish, sizeof(publish), "/a",
        &payload, &length) != 0);
    assert(mqtt_wire_publish(0x38, publish, sizeof(publish), "/a",
        &payload, &length) != 0);
    assert(mqtt_wire_publish(0x30, publish, sizeof(publish), "/b",
        &payload, &length) != 0);
    io = make_io(&stream, NULL, 0);
    assert(mqtt_wire_ping(&io) == 0 && stream.output_length == 2 &&
        stream.output[0] == 0xc0 && stream.output[1] == 0);
    io = make_io(&stream, NULL, 0);
    assert(mqtt_wire_send_publish(&io, "/a", "{}", 2) == 0);
    assert(stream.output_length == 8 && stream.output[0] == 0x30);
    stream.input = stream.output;
    stream.input_length = stream.output_length;
    assert(mqtt_wire_read(&io, &header, body, sizeof(body), &length) == 0);
    assert(mqtt_wire_publish(header, body, length, "/a", &payload, &length) == 0 &&
        length == 2 && memcmp(payload, "{}", 2) == 0);
    assert(mqtt_wire_send_publish(&io, "/a", "{}", 248) != 0);
    assert(mqtt_wire_send_publish(&io, "", "{}", 2) != 0);
    assert(mqtt_wire_send_publish(&io, "/a", NULL, 1) != 0);
    puts("MQTT wire tests passed");
    return 0;
}
