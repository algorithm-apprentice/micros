#ifndef MICROS_KERNEL_ELF_SPAWN_TEST_INTERNAL_H
#define MICROS_KERNEL_ELF_SPAWN_TEST_INTERNAL_H

#include "kernel/bootstrap_runtime.h"
#include "kernel/elf_spawn_test.h"
#include "kernel/elf_spawn_test_fixture.h"
#include "tests/qemu/elf_spawn_resources.h"

enum elf_spawn_test_phase {
    ELF_SPAWN_TEST_BASELINE = 0,
    ELF_SPAWN_TEST_ROLLBACK,
    ELF_SPAWN_TEST_CHILD,
    ELF_SPAWN_TEST_FINAL,
};

struct elf_spawn_test_context {
    enum elf_spawn_test_phase phase;
    struct micros_process_handle init_process;
    micros_endpoint_t init_endpoint;
    struct micros_elf_spawn_resources baseline;
    struct micros_elf_spawn_resources current;
};

extern const struct micros_bootstrap_runtime_config micros_elf_spawn_test_config;

struct elf_spawn_test_context *elf_spawn_test_context_get(void);
bool elf_spawn_test_read_process(
    struct micros_process_handle process, uint64_t address,
    void *destination, size_t size, uint32_t permissions
);
bool elf_spawn_test_current_process(struct micros_process_handle *process, micros_endpoint_t *endpoint);
bool elf_spawn_test_runtime_valid(void);
bool elf_spawn_test_capture_kernel(struct micros_elf_spawn_kernel_resources *snapshot);
bool elf_spawn_test_capture_services(struct micros_elf_spawn_service_resources *snapshot);
bool elf_spawn_test_services_idle(const struct micros_elf_spawn_service_resources *snapshot,
                                   micros_endpoint_t init_endpoint);
bool elf_spawn_test_child_descriptors(const struct micros_elf_spawn_service_resources *snapshot,
                                       micros_endpoint_t init_endpoint, micros_endpoint_t child_endpoint);
bool elf_spawn_test_process_report(struct micros_hart *hart, struct micros_trap_frame *frame);

#endif
