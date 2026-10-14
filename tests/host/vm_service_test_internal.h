#ifndef MICROS_TESTS_HOST_VM_SERVICE_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_VM_SERVICE_TEST_INTERNAL_H

#include <stdio.h>
#include <string.h>

#include "tests/host/vm_core_test_internal.h"
#include "servers/vm/vm_service_core.h"

#define VM_SERVICE_EXPECT(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "%s:%d: expected %s\n", __FILE__, __LINE__, #expression); \
            return false; \
        } \
    } while (false)

enum vm_service_test_failure_point {
    VM_SERVICE_TEST_FAIL_NONE,
    VM_SERVICE_TEST_FAIL_COPY,
    VM_SERVICE_TEST_FAIL_BEGIN,
    VM_SERVICE_TEST_FAIL_MAP,
    VM_SERVICE_TEST_FAIL_SCRATCH,
    VM_SERVICE_TEST_FAIL_SCRATCH_COMMIT,
    VM_SERVICE_TEST_FAIL_FREEZE,
    VM_SERVICE_TEST_FAIL_ABORT,
    VM_SERVICE_TEST_FAIL_COMMIT,
    VM_SERVICE_TEST_FAIL_EXIT,
};

struct vm_service_test_fixture {
    struct vm_core_test_context *context;
    struct micros_vm_service service;
    size_t executable_size;
    size_t request_size;
    size_t copies;
    size_t copied_executable;
    size_t maps;
    size_t commits;
    size_t begins;
    size_t freezes;
    size_t aborts;
    size_t load_commits;
    size_t exits;
    enum vm_service_test_failure_point failure;
    size_t failure_page;
    unsigned int corrupt_begin;
    unsigned int corrupt_freeze;
    bool mapped;
    bool frozen;
    bool active;
    bool violation;
};

const struct micros_vm_service_backend *vm_service_test_backend(void);
bool vm_service_test_initialize(
    struct vm_service_test_fixture *fixture, struct vm_core_test_context *context
);
struct micros_ipc_message vm_service_test_message(
    uint32_t command, const uint64_t arguments[5]
);
uint64_t vm_service_test_read_word(const uint8_t *bytes);
bool vm_service_test_load(struct vm_service_test_fixture *fixture);
bool test_vm_service_load_and_release(struct vm_core_test_context *context);
bool test_vm_service_abort_and_failure(struct vm_core_test_context *context);
bool test_vm_service_protocol_boundaries(struct vm_core_test_context *context);
bool test_vm_service_release_identity(struct vm_core_test_context *context);
bool test_vm_service_configuration(struct vm_core_test_context *context);
bool test_vm_service_expected_load_rejection(struct vm_core_test_context *context);
bool test_vm_service_kernel_disagreement(struct vm_core_test_context *context);
bool test_vm_service_runtime_backend(struct vm_core_test_context *context);

#endif
