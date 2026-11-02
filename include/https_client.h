#ifndef HTTPS_CLIENT_H
#define HTTPS_CLIENT_H

#include <stddef.h>
#include <stdint.h>

int https_client_post_json(const char *host, const char *path,
    const char *request_body, uint8_t *response_body, size_t response_capacity,
    size_t *response_length, int *http_status);
int https_client_probe(int *http_status);
int https_client_post_json_verified(const char *host, const char *path,
    const char *ca_file, const char *request_body, uint8_t *response_body,
    size_t response_capacity, size_t *response_length, int *http_status,
    int *submission_started);
void https_client_shutdown(void);

#endif
