#ifndef PRINTER_DEMO_MQTT_LISTENER_H
#define PRINTER_DEMO_MQTT_LISTENER_H

#include "soundbox_event.h"

int mqtt_listener_start(void);
void mqtt_listener_stop(void);
void mqtt_listener_watch(int enabled);
int mqtt_listener_connected(void);
int mqtt_listener_take_success(void);
int mqtt_listener_take_payment(struct soundbox_event *event);
int mqtt_listener_take_update(struct soundbox_event *event);
unsigned int mqtt_listener_pause(void);
int mqtt_listener_resume(unsigned int token);
unsigned int mqtt_listener_dropped(void);

#endif
