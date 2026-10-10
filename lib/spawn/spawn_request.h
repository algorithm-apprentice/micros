#ifndef MICROS_LIB_SPAWN_REQUEST_H
#define MICROS_LIB_SPAWN_REQUEST_H

#include <stddef.h>
#include <stdint.h>

#include "micros/spawn.h"

enum micros_spawn_request_error {
    MICROS_SPAWN_REQUEST_OK = 0,
    MICROS_SPAWN_REQUEST_ERROR_ARGUMENT,
    MICROS_SPAWN_REQUEST_ERROR_MALFORMED,
};

struct micros_spawn_string_reference {
    uint16_t offset;
    uint16_t size;
};

struct micros_spawn_descriptor_action {
    uint16_t source;
    uint16_t destination;
    uint32_t operation;
};

struct micros_spawn_request {
    uint32_t total_size;
    uint16_t argument_count;
    uint16_t environment_count;
    uint16_t descriptor_action_count;
    uint16_t reserved;
    struct micros_spawn_string_reference path;
    struct micros_spawn_string_reference
        arguments[MICROS_SPAWN_ARGUMENT_CAPACITY];
    struct micros_spawn_string_reference
        environment[MICROS_SPAWN_ENVIRONMENT_CAPACITY];
    struct micros_spawn_descriptor_action
        descriptor_actions[MICROS_SPAWN_DESCRIPTOR_ACTION_CAPACITY];
};

enum micros_spawn_request_error micros_spawn_request_decode(
    const uint8_t *bytes,
    size_t byte_count,
    struct micros_spawn_request *request
);

#endif
