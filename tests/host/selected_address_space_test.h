#ifndef MICROS_SELECTED_ADDRESS_SPACE_TEST_H
#define MICROS_SELECTED_ADDRESS_SPACE_TEST_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/frame_allocator.h"
#include "micros/frame_ownership.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_objects.h"

_Static_assert(sizeof(uintptr_t) == sizeof(uint64_t), "MEM04 native bindings require LP64");

#if defined(MICROS_TEST_WORK_COUNTS)
#include "tests/work/work_counts.h"
#define MICROS_AS_OBSERVE_VALUE(site, owner, kind, value) \
    (micros_test_work_record((owner), (kind), 1), (value))
#endif

struct micros_frame_allocator *micros_as_native_allocator(void);
struct micros_frame_ownership *micros_as_native_ownership(void);
void micros_as_native_bootstrap_reset(void);
void micros_as_native_objects_reset(void);
void micros_as_native_kernel_reset(void);
void micros_as_native_kernel_bind(
    const struct micros_kernel_address_space_report *report
);
void micros_as_native_ipc_reset(void);
void micros_as_native_vm_reset(void);
struct micros_endpoint_registry;
struct micros_vm_handoff_state;
struct micros_endpoint_registry *micros_as_native_ipc_input(void);
struct micros_vm_handoff_state *micros_as_native_vm_input(void);
void micros_as_native_ipc_publish(void);
const uint64_t *micros_as_native_scratch_bitmap(bool release);
bool micros_as_native_scratch_busy(void);

uintptr_t micros_as_native_irq_save(void);
void micros_as_native_irq_restore(uintptr_t status);
bool micros_as_native_irq_enabled(void);
uint64_t micros_as_native_read_satp(void);

#endif
