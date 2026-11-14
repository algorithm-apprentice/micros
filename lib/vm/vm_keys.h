#ifndef MICROS_LIB_VM_KEYS_H
#define MICROS_LIB_VM_KEYS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    MICROS_VM_KEYS32_CAPACITY = 4096,
    MICROS_VM_KEYS64_CAPACITY = 2048,
};

struct micros_vm_key_view {
    uint32_t *keys32;
    uint64_t *keys64;
    size_t count;
};

_Static_assert(sizeof(struct micros_vm_key_view) <= 64,
               "VM key borrows must remain bounded");

bool micros_vm_keys_begin32(struct micros_vm_key_view *view);
bool micros_vm_keys_begin64(struct micros_vm_key_view *view);
bool micros_vm_keys_view_is_valid(const struct micros_vm_key_view *view);
bool micros_vm_keys_append32(struct micros_vm_key_view *view, uint32_t value);
bool micros_vm_keys_append64(struct micros_vm_key_view *view, uint64_t value);
bool micros_vm_keys_append_page(
    struct micros_vm_key_view *view, uint64_t physical_address, size_t ordinal);
bool micros_vm_keys_finish_pages(struct micros_vm_key_view *view, size_t *first_duplicate);
bool micros_vm_keys_sort(struct micros_vm_key_view *view);
bool micros_vm_keys_end(struct micros_vm_key_view *view);

#endif
