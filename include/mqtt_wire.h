#ifndef PRINTER_DEMO_MQTT_WIRE_H
#define PRINTER_DEMO_MQTT_WIRE_H

#include <stddef.h>
#include <stdint.h>

#define MQTT_WIRE_BODY_MAX 4352U

struct mqtt_wire_io {
    void *context;
    int (*read)(void *context, void *buffer, size_t length);
    int (*write)(void *context, const void *buffer, size_t length);
};

int mqtt_wire_connect(const struct mqtt_wire_io *io, const char *client_id);
int mqtt_wire_subscribe(const struct mqtt_wire_io *io, const char *topic);
int mqtt_wire_suback(uint8_t header, const uint8_t *body, size_t length);
int mqtt_wire_ping(const struct mqtt_wire_io *io);
int mqtt_wire_send_publish(const struct mqtt_wire_io *io, const char *topic,
    const void *payload, size_t length);
int mqtt_wire_read(const struct mqtt_wire_io *io, uint8_t *header,
    uint8_t *body, size_t capacity, size_t *length);
int mqtt_wire_publish(const uint8_t header, const uint8_t *body, size_t length,
    const char *topic, const uint8_t **payload, size_t *payload_length);

#endif
