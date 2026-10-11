#ifndef MICROS_VM_H
#define MICROS_VM_H

#include <stddef.h>
#include <stdint.h>

enum {
    MICROS_VM_LOAD_BEGIN_RESULT_VERSION = 1,
    MICROS_VM_LOAD_BEGIN_RESULT_SIZE = 32,
    MICROS_VM_LOAD_FREEZE_PLAN_VERSION = 1,
    MICROS_VM_LOAD_FREEZE_PLAN_SIZE = 96,
    MICROS_VM_LOAD_FREEZE_RESULT_VERSION = 1,
    MICROS_VM_LOAD_FREEZE_RESULT_SIZE = 32,
    MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT = 4,
};

#define MICROS_VM_SCRATCH_VIRTUAL_ADDRESS UINT64_C(0x000000007fffe000)

struct micros_vm_load_begin_result {
    uint32_t version;
    uint32_t size;
    uint64_t load_token;
    uint16_t process_slot;
    uint16_t reserved0;
    uint32_t process_generation;
    uint64_t reserved1;
};

struct micros_vm_load_freeze_plan {
    uint32_t version;
    uint32_t size;
    uint64_t load_token;
    uint64_t table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT];
    uint64_t entry;
    uint64_t stack_pointer;
    uint32_t image_page_count;
    uint32_t data_page_count;
    uint64_t page_plan_digest;
    uint64_t reserved[2];
};

struct micros_vm_load_freeze_result {
    uint32_t version;
    uint32_t size;
    uint64_t mapping_generation;
    uint64_t reserved[2];
};

_Static_assert(sizeof(struct micros_vm_load_begin_result) == MICROS_VM_LOAD_BEGIN_RESULT_SIZE,
               "VM load-begin result ABI changed");
_Static_assert(offsetof(struct micros_vm_load_begin_result, version) == 0 &&
                   offsetof(struct micros_vm_load_begin_result, size) == 4 &&
                   offsetof(struct micros_vm_load_begin_result, load_token) == 8 &&
                   offsetof(struct micros_vm_load_begin_result, process_slot) == 16 &&
                   offsetof(struct micros_vm_load_begin_result, reserved0) == 18 &&
                   offsetof(struct micros_vm_load_begin_result, process_generation) == 20 &&
                   offsetof(struct micros_vm_load_begin_result, reserved1) == 24,
               "VM load-begin result offsets changed");
_Static_assert(sizeof(struct micros_vm_load_freeze_plan) == MICROS_VM_LOAD_FREEZE_PLAN_SIZE,
               "VM load-freeze plan ABI changed");
_Static_assert(offsetof(struct micros_vm_load_freeze_plan, version) == 0 &&
                   offsetof(struct micros_vm_load_freeze_plan, size) == 4 &&
                   offsetof(struct micros_vm_load_freeze_plan, load_token) == 8 &&
                   offsetof(struct micros_vm_load_freeze_plan, table_frames) == 16 &&
                   offsetof(struct micros_vm_load_freeze_plan, entry) == 48 &&
                   offsetof(struct micros_vm_load_freeze_plan, stack_pointer) == 56 &&
                   offsetof(struct micros_vm_load_freeze_plan, image_page_count) == 64 &&
                   offsetof(struct micros_vm_load_freeze_plan, data_page_count) == 68 &&
                   offsetof(struct micros_vm_load_freeze_plan, page_plan_digest) == 72 &&
                   offsetof(struct micros_vm_load_freeze_plan, reserved) == 80,
               "VM load-freeze plan offsets changed");
_Static_assert(sizeof(struct micros_vm_load_freeze_result) == MICROS_VM_LOAD_FREEZE_RESULT_SIZE,
               "VM load-freeze result ABI changed");
_Static_assert(offsetof(struct micros_vm_load_freeze_result, version) == 0 &&
                   offsetof(struct micros_vm_load_freeze_result, size) == 4 &&
                   offsetof(struct micros_vm_load_freeze_result, mapping_generation) == 8 &&
                   offsetof(struct micros_vm_load_freeze_result, reserved) == 16,
               "VM load-freeze result offsets changed");

#endif
