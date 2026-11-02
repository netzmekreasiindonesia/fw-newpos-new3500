#include "mqtt_wire.h"

#include <string.h>

static int send_packet(const struct mqtt_wire_io *io, uint8_t header,
    const uint8_t *body, size_t length)
{
    uint8_t packet[256];
    size_t position = 1;
    size_t remaining = length;

    if (!io || !io->write || length > sizeof(packet) - 5U)
        return -1;
    packet[0] = header;
    do {
        uint8_t digit = (uint8_t)(remaining % 128U);
        remaining /= 128U;
        packet[position++] = digit | (remaining ? 0x80U : 0U);
    } while (remaining);
    if (length)
        memcpy(packet + position, body, length);
    return io->write(io->context, packet, position + length);
}

int mqtt_wire_read(const struct mqtt_wire_io *io, uint8_t *header,
    uint8_t *body, size_t capacity, size_t *length)
{
    size_t value = 0;
    size_t multiplier = 1;
    size_t index;
    uint8_t digit;

    if (!io || !io->read || !header || !body || !length ||
        io->read(io->context, header, 1) != 0)
        return -1;
    for (index = 0; index < 4U; ++index) {
        if (io->read(io->context, &digit, 1) != 0)
            return -1;
        value += (size_t)(digit & 0x7fU) * multiplier;
        if (!(digit & 0x80U)) {
            if ((index && digit == 0) || value > capacity)
                return -1;
            *length = value;
            return value ? io->read(io->context, body, value) : 0;
        }
        multiplier *= 128U;
    }
    return -1;
}

int mqtt_wire_connect(const struct mqtt_wire_io *io, const char *client_id)
{
    static const uint8_t prefix[] = {0, 4, 'M', 'Q', 'T', 'T', 4, 2, 0, 60};
    uint8_t body[128];
    uint8_t header;
    size_t length;
    size_t id_length;

    if (!client_id || !(id_length = strlen(client_id)) ||
        id_length > sizeof(body) - sizeof(prefix) - 2U)
        return -1;
    memcpy(body, prefix, sizeof(prefix));
    body[sizeof(prefix)] = (uint8_t)(id_length >> 8);
    body[sizeof(prefix) + 1U] = (uint8_t)id_length;
    memcpy(body + sizeof(prefix) + 2U, client_id, id_length);
    if (send_packet(io, 0x10, body, sizeof(prefix) + 2U + id_length) != 0 ||
        mqtt_wire_read(io, &header, body, sizeof(body), &length) != 0)
        return -1;
    return header == 0x20 && length == 2U && body[0] == 0 && body[1] == 0 ? 0 : -1;
}

int mqtt_wire_subscribe(const struct mqtt_wire_io *io, const char *topic)
{
    uint8_t body[128];
    size_t topic_length;

    if (!topic || !(topic_length = strlen(topic)) || topic_length > sizeof(body) - 5U)
        return -1;
    body[0] = 0;
    body[1] = 1;
    body[2] = (uint8_t)(topic_length >> 8);
    body[3] = (uint8_t)topic_length;
    memcpy(body + 4, topic, topic_length);
    body[4U + topic_length] = 0;
    return send_packet(io, 0x82, body, topic_length + 5U);
}

int mqtt_wire_suback(uint8_t header, const uint8_t *body, size_t length)
{
    return body && header == 0x90 && length == 3U && body[0] == 0 &&
        body[1] == 1 && body[2] == 0 ? 0 : -1;
}

int mqtt_wire_ping(const struct mqtt_wire_io *io)
{
    return send_packet(io, 0xc0, NULL, 0);
}

int mqtt_wire_send_publish(const struct mqtt_wire_io *io, const char *topic,
    const void *payload, size_t length)
{
    uint8_t body[251];
    size_t topic_length;
    if (!topic || !(topic_length = strlen(topic)) || topic_length > sizeof(body) - 2U ||
        (!payload && length) || length > sizeof(body) - 2U - topic_length)
        return -1;
    body[0] = (uint8_t)(topic_length >> 8);
    body[1] = (uint8_t)topic_length;
    memcpy(body + 2, topic, topic_length);
    if (length)
        memcpy(body + 2U + topic_length, payload, length);
    return send_packet(io, 0x30, body, topic_length + length + 2U);
}

int mqtt_wire_publish(const uint8_t header, const uint8_t *body, size_t length,
    const char *topic, const uint8_t **payload, size_t *payload_length)
{
    size_t topic_length;
    size_t expected;

    if (!body || !topic || !payload || !payload_length ||
        (header & 0xfeU) != 0x30U || length < 2U)
        return -1;
    topic_length = ((size_t)body[0] << 8) | body[1];
    expected = strlen(topic);
    if (topic_length != expected || topic_length > length - 2U ||
        memcmp(body + 2, topic, topic_length) != 0)
        return -1;
    *payload = body + 2U + topic_length;
    *payload_length = length - 2U - topic_length;
    return 0;
}
