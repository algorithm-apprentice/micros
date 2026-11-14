#include "lib/vm/vm_keys.h"

#include <stdatomic.h>
#include "micros/frame_allocator.h"

enum {
    VM_KEY_PENDING_CAPACITY = 13,
};

struct vm_key_partition {
    uint16_t first;
    uint16_t end;
    uint8_t bit;
    uint8_t reserved;
};

struct vm_key_workspace {
    union {
        uint32_t keys32[MICROS_VM_KEYS32_CAPACITY];
        uint64_t keys64[MICROS_VM_KEYS64_CAPACITY];
    } values;
    struct vm_key_partition key_partitions[VM_KEY_PENDING_CAPACITY];
    size_t count;
    size_t pending_count;
    uint8_t width;
    _Atomic(struct micros_vm_key_view *) borrower;
};

_Static_assert(ATOMIC_POINTER_LOCK_FREE == 2, "VM key borrowing must be lock-free");
_Static_assert(MICROS_FRAME_SIZE == 4096, "page keys reserve twelve ordinal bits");
_Static_assert(sizeof(struct vm_key_workspace) <= 16 * 1024 + 256,
               "VM key storage must fit its Accepted image budget");

static struct vm_key_workspace workspace;

static bool begin_keys(struct micros_vm_key_view *view, uint8_t width)
{
    struct micros_vm_key_view *expected = NULL;

    if (view == NULL || (width != 32 && width != 64)
        || !atomic_compare_exchange_strong_explicit(
            &workspace.borrower, &expected, view, memory_order_acquire, memory_order_relaxed)) {
        return false;
    }
    if (workspace.count != 0 || workspace.pending_count != 0 || workspace.width != 0) {
        atomic_store_explicit(&workspace.borrower, NULL, memory_order_release);
        return false;
    }
    workspace.width = width;
    *view = (struct micros_vm_key_view){
        .keys32 = width == 32 ? workspace.values.keys32 : NULL,
        .keys64 = width == 64 ? workspace.values.keys64 : NULL,
        .count = 0,
    };
    return true;
}

bool micros_vm_keys_begin32(struct micros_vm_key_view *view)
{
    return begin_keys(view, 32);
}

bool micros_vm_keys_begin64(struct micros_vm_key_view *view)
{
    return begin_keys(view, 64);
}

bool micros_vm_keys_view_is_valid(const struct micros_vm_key_view *view)
{
    if (view == NULL
        || atomic_load_explicit(&workspace.borrower, memory_order_acquire) != view
        || workspace.pending_count > VM_KEY_PENDING_CAPACITY
        || view->count != workspace.count) {
        return false;
    }
    if (workspace.width == 32) {
        return workspace.count <= MICROS_VM_KEYS32_CAPACITY
            && view->keys32 == workspace.values.keys32 && view->keys64 == NULL;
    }
    return workspace.width == 64 && workspace.count <= MICROS_VM_KEYS64_CAPACITY
        && view->keys64 == workspace.values.keys64 && view->keys32 == NULL;
}

bool micros_vm_keys_append32(struct micros_vm_key_view *view, uint32_t value)
{
    if (!micros_vm_keys_view_is_valid(view) || workspace.width != 32
        || workspace.count == MICROS_VM_KEYS32_CAPACITY) {
        return false;
    }
    workspace.values.keys32[workspace.count] = value;
    ++workspace.count;
    view->count = workspace.count;
    return true;
}

bool micros_vm_keys_append64(struct micros_vm_key_view *view, uint64_t value)
{
    if (!micros_vm_keys_view_is_valid(view) || workspace.width != 64
        || workspace.count == MICROS_VM_KEYS64_CAPACITY) {
        return false;
    }
    workspace.values.keys64[workspace.count] = value;
    ++workspace.count;
    view->count = workspace.count;
    return true;
}

bool micros_vm_keys_append_page(
    struct micros_vm_key_view *view, uint64_t physical_address, size_t ordinal)
{
    if (!micros_vm_keys_view_is_valid(view) || workspace.width != 64 || ordinal >= MICROS_FRAME_SIZE) {
        return false;
    }
    /* Leave an unaligned input's error at its original per-item boundary. */
    return physical_address % MICROS_FRAME_SIZE != 0
        || micros_vm_keys_append64(view, physical_address | ordinal);
}

bool micros_vm_keys_finish_pages(struct micros_vm_key_view *view, size_t *first_duplicate)
{
    size_t duplicate = SIZE_MAX;
    bool valid = first_duplicate != NULL && micros_vm_keys_view_is_valid(view)
        && workspace.width == 64 && micros_vm_keys_sort(view);

    if (valid) {
        for (size_t index = 1; index < view->count; ++index) {
            uint64_t previous = view->keys64[index - 1];
            uint64_t current = view->keys64[index];
            size_t ordinal = (size_t)(current & (MICROS_FRAME_SIZE - 1));

            if (previous / MICROS_FRAME_SIZE == current / MICROS_FRAME_SIZE
                && ordinal < duplicate) {
                duplicate = ordinal;
            }
        }
    }
    if (!micros_vm_keys_end(view) || !valid) {
        return false;
    }
    *first_duplicate = duplicate;
    return true;
}

static uint64_t read_key(size_t index)
{
    return workspace.width == 32
        ? workspace.values.keys32[index] : workspace.values.keys64[index];
}

static void write_key(size_t index, uint64_t value)
{
    if (workspace.width == 32) {
        workspace.values.keys32[index] = (uint32_t)value;
    } else {
        workspace.values.keys64[index] = value;
    }
}

static uint16_t partition_keys(struct vm_key_partition partition)
{
    uint64_t mask = UINT64_C(1) << partition.bit;
    uint16_t middle = partition.first;

    for (uint16_t index = partition.first; index < partition.end; ++index) {
        uint64_t value = read_key(index);

        if ((value & mask) == 0) {
            if (middle != index) {
                uint64_t displaced = read_key(middle);

                write_key(index, displaced);
                write_key(middle, value);
            }
            ++middle;
        }
    }
    return middle;
}

bool micros_vm_keys_sort(struct micros_vm_key_view *view)
{
    struct vm_key_partition current;

    if (!micros_vm_keys_view_is_valid(view) || workspace.pending_count != 0) {
        return false;
    }
    current = (struct vm_key_partition){0, (uint16_t)workspace.count,
                                        (uint8_t)(workspace.width - 1), 0};
    for (;;) {
        if (current.first > current.end || current.end > workspace.count
            || current.bit >= workspace.width || current.reserved != 0) {
            return false;
        }
        if (current.end - current.first > 1) {
            uint16_t middle = partition_keys(current);
            uint16_t lower_count = (uint16_t)(middle - current.first);
            uint16_t upper_count = (uint16_t)(current.end - middle);

            if (current.bit != 0 && (lower_count > 1 || upper_count > 1)) {
                struct vm_key_partition lower = {
                    current.first, middle, (uint8_t)(current.bit - 1), 0,
                };
                struct vm_key_partition upper = {
                    middle, current.end, (uint8_t)(current.bit - 1), 0,
                };

                if (lower_count > 1 && upper_count > 1) {
                    if (workspace.pending_count == VM_KEY_PENDING_CAPACITY) {
                        return false;
                    }
                    workspace.key_partitions[workspace.pending_count++] =
                        lower_count <= upper_count ? upper : lower;
                    current = lower_count <= upper_count ? lower : upper;
                } else {
                    current = lower_count > 1 ? lower : upper;
                }
                continue;
            }
        }
        if (workspace.pending_count == 0) {
            return true;
        }
        --workspace.pending_count;
        current = workspace.key_partitions[workspace.pending_count];
        workspace.key_partitions[workspace.pending_count] = (struct vm_key_partition){0};
    }
}

bool micros_vm_keys_end(struct micros_vm_key_view *view)
{
    if (!micros_vm_keys_view_is_valid(view)) {
        return false;
    }
    for (size_t index = 0; index < workspace.count; ++index) {
        write_key(index, 0);
    }
    for (size_t index = 0; index < workspace.pending_count; ++index) {
        workspace.key_partitions[index] = (struct vm_key_partition){0};
    }
    workspace.count = 0;
    workspace.pending_count = 0;
    workspace.width = 0;
    *view = (struct micros_vm_key_view){0};
    atomic_store_explicit(&workspace.borrower, NULL, memory_order_release);
    return true;
}
