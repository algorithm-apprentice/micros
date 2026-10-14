#ifndef MICROS_SERVERS_VM_SERVICE_CORE_H
#define MICROS_SERVERS_VM_SERVICE_CORE_H

#include "micros/runtime.h"
#include "micros/vm.h"
#include "micros/vm_service.h"
#include "servers/vm/vm_core.h"

enum micros_vm_service_error {
    MICROS_VM_SERVICE_OK = 0,
    MICROS_VM_SERVICE_ERROR_ARGUMENT,
    MICROS_VM_SERVICE_ERROR_INVARIANT,
};

struct micros_vm_service_bindings {
    micros_endpoint_t self;
    micros_endpoint_t pm;
    micros_endpoint_t vfs;
    uint32_t reserved;
};

struct micros_vm_service {
    uint64_t initialization_magic;
    struct micros_vm_state *state;
    struct micros_vm_service_bindings bindings;
};

struct micros_vm_service_backend {
    micros_runtime_result_t (*copy_from)(
        void *context, micros_endpoint_t grantor, micros_grant_t grant,
        size_t offset, uintptr_t destination, size_t size
    );
    micros_runtime_result_t (*load_begin)(
        void *context, uint64_t reservation, struct micros_vm_load_begin_result *result
    );
    micros_runtime_result_t (*scratch_map)(
        void *context, uint64_t token, const struct micros_vm_load_page_action *page
    );
    uint8_t *(*scratch_page)(void *context);
    micros_runtime_result_t (*scratch_commit)(
        void *context, uint64_t token, const struct micros_vm_load_page_action *page
    );
    micros_runtime_result_t (*freeze)(
        void *context, const struct micros_vm_load_freeze_plan *plan,
        struct micros_vm_load_freeze_result *result
    );
    micros_runtime_result_t (*abort_load)(void *context, uint64_t token, uint64_t generation);
    micros_runtime_result_t (*commit_load)(void *context, uint64_t token, uint64_t generation);
    micros_runtime_result_t (*exit_release)(
        void *context, uint64_t exit_token, uint64_t load_token, uint64_t generation
    );
};

enum micros_vm_service_error micros_vm_service_initialize(
    struct micros_vm_service *service, struct micros_vm_state *state,
    const struct micros_vm_service_bindings *bindings
);
const struct micros_vm_service_backend *micros_vm_service_runtime_backend(void);
enum micros_vm_service_error micros_vm_service_handle_message(
    struct micros_vm_service *service, const struct micros_vm_service_backend *backend,
    void *backend_context, const struct micros_ipc_message *request,
    struct micros_ipc_message *reply
);

#endif
