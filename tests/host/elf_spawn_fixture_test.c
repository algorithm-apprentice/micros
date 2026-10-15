#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lib/spawn/spawn_request.h"
#include "tests/host/elf_spawn_fixture_test_internal.h"
#include "tests/qemu/elf_spawn_request.h"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); \
            return false; \
        } \
    } while (0)

static bool string_matches(
    const uint8_t *bytes,
    struct micros_spawn_string_reference reference,
    const char *expected
)
{
    return (
        reference.size == strlen(expected) + 1
        && memcmp(bytes + reference.offset, expected, reference.size) == 0
    );
}

static bool check_request(enum micros_elf_spawn_request_kind kind)
{
    uint8_t bytes[MICROS_ELF_SPAWN_REQUEST_BUFFER_SIZE];
    struct micros_spawn_request request;
    struct micros_spawn_stack_layout layout;
    bool init = kind == MICROS_ELF_SPAWN_REQUEST_INIT;
    bool missing = kind == MICROS_ELF_SPAWN_REQUEST_MISSING_DESCRIPTOR;
    size_t size = 0;
    size_t index;

    memset(bytes, 0xa5, sizeof(bytes));
    CHECK(micros_elf_spawn_build_request(bytes, sizeof(bytes), kind, &size));
    CHECK(size <= sizeof(bytes));
    CHECK(micros_spawn_request_decode(bytes, size, &request)
          == MICROS_SPAWN_REQUEST_OK);
    CHECK(micros_spawn_stack_layout_plan(&request, &layout)
          == MICROS_SPAWN_STACK_LAYOUT_OK);
    CHECK(layout.occupied_size <= MICROS_APPLICATION_STACK_OCCUPIED_MAX);
    CHECK(request.argument_count == (init ? 1 : 2));
    CHECK(request.environment_count == 1);
    CHECK(request.descriptor_action_count == (init ? 0 : missing ? 1 : 3));
    CHECK(string_matches(bytes, request.path,
                         init ? MICROS_ELF_SPAWN_INIT_PATH
                              : MICROS_ELF_SPAWN_CHILD_PATH));
    CHECK(string_matches(bytes, request.arguments[0],
                         init ? MICROS_ELF_SPAWN_INIT_ARGUMENT
                              : MICROS_ELF_SPAWN_CHILD_ARGUMENT));
    if (!init) {
        CHECK(string_matches(bytes, request.arguments[1],
                             MICROS_ELF_SPAWN_CHILD_SECOND_ARGUMENT));
    }
    CHECK(string_matches(bytes, request.environment[0],
                         MICROS_ELF_SPAWN_ENVIRONMENT));
    for (index = 0; index < request.descriptor_action_count; ++index) {
        CHECK(request.descriptor_actions[index].operation
              == MICROS_SPAWN_DESCRIPTOR_DUPLICATE);
        CHECK(request.descriptor_actions[index].destination == index);
        CHECK(request.descriptor_actions[index].source
              == (missing ? MICROS_SPAWN_DESCRIPTOR_CAPACITY - 1 : index));
    }
    for (index = size; index < sizeof(bytes); ++index) {
        CHECK(bytes[index] == 0xa5);
    }
    return true;
}

static bool test_canonical_init_request(void)
{
    return check_request(MICROS_ELF_SPAWN_REQUEST_INIT);
}

static bool test_canonical_child_and_post_freeze_descriptor_failure(void)
{
    return (
        check_request(MICROS_ELF_SPAWN_REQUEST_CHILD)
        && check_request(MICROS_ELF_SPAWN_REQUEST_MISSING_DESCRIPTOR)
    );
}

static bool test_request_failure_preserves_outputs_and_exact_capacity(void)
{
    uint8_t bytes[MICROS_ELF_SPAWN_REQUEST_BUFFER_SIZE];
    uint8_t before[sizeof(bytes)];
    size_t needed;
    size_t size = SIZE_MAX;

    CHECK(micros_elf_spawn_build_request(
        bytes, sizeof(bytes), MICROS_ELF_SPAWN_REQUEST_CHILD, &needed
    ));
    memset(bytes, 0xa5, sizeof(bytes));
    memcpy(before, bytes, sizeof(before));
    CHECK(!micros_elf_spawn_build_request(
        NULL, sizeof(bytes), MICROS_ELF_SPAWN_REQUEST_CHILD, &size
    ));
    CHECK(!micros_elf_spawn_build_request(
        bytes, sizeof(bytes), MICROS_ELF_SPAWN_REQUEST_CHILD, NULL
    ));
    CHECK(!micros_elf_spawn_build_request(bytes, sizeof(bytes), 0, &size));
    CHECK(!micros_elf_spawn_build_request(bytes, sizeof(bytes), 4, &size));
    CHECK(!micros_elf_spawn_build_request(
        bytes, needed - 1, MICROS_ELF_SPAWN_REQUEST_CHILD, &size
    ));
    CHECK(size == SIZE_MAX);
    CHECK(memcmp(bytes, before, sizeof(bytes)) == 0);
    CHECK(micros_elf_spawn_build_request(
        bytes, needed, MICROS_ELF_SPAWN_REQUEST_CHILD, &size
    ));
    CHECK(size == needed);
    CHECK(memcmp(bytes + needed, before + needed, sizeof(bytes) - needed) == 0);
    return true;
}

int main(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } cases[] = {
        {"canonical-init", test_canonical_init_request},
        {"canonical-child-descriptor", test_canonical_child_and_post_freeze_descriptor_failure},
        {"request-failure-capacity", test_request_failure_preserves_outputs_and_exact_capacity},
        {"kernel-resource-oracle", test_elf_spawn_kernel_resource_oracle},
        {"service-resource-oracle", test_elf_spawn_service_resource_oracle},
        {"resource-counter-policy", test_elf_spawn_resource_counter_and_inactive_byte_policy},
        {"fixture-client-protocol", test_elf_spawn_client_protocol},
    };
    size_t index;

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        if (!cases[index].run()) {
            fprintf(stderr, "FAIL %s\n", cases[index].name);
            return 1;
        }
        printf("PASS %s\n", cases[index].name);
    }
    return 0;
}
