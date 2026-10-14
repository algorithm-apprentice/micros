#ifndef MICROS_SERVERS_VM_SERVICE_INTERNAL_H
#define MICROS_SERVERS_VM_SERVICE_INTERNAL_H

#include "servers/vm/vm_service_core.h"

struct vm_service_request {
    uint32_t command;
    uint64_t arguments[5];
};

void vm_service_clear_bytes(void *storage, size_t size);
enum micros_vm_control_result vm_service_decode_request(
    const struct micros_vm_service *service, const struct micros_ipc_message *message,
    struct vm_service_request *request
);
void vm_service_build_reply(
    uint32_t command, enum micros_vm_control_result result, const uint64_t outputs[4],
    struct micros_ipc_message *reply
);
enum micros_vm_service_error vm_service_load(
    struct micros_vm_service *service, const struct micros_vm_service_backend *backend,
    void *backend_context, const struct vm_service_request *request,
    enum micros_vm_control_result *result, uint64_t outputs[4]
);
enum micros_vm_service_error vm_service_release(
    struct micros_vm_service *service, const struct micros_vm_service_backend *backend,
    void *backend_context, const struct vm_service_request *request,
    enum micros_vm_control_result *result
);

#endif
