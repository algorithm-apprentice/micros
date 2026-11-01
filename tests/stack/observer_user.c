#include "micros/stack_observation.h"

#include <stddef.h>
#include <stdint.h>

#include "kernel/boot/bootstrap_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "micros/endpoint.h"
#include "stack_observer_images.h"

static struct micros_stack_observation user_copies[7];

#ifdef MICROS_STACK_OBSERVER_APPLICATIONS
struct application_observation {
    struct micros_process_handle process;
    struct micros_thread_handle thread;
    uintptr_t root;
    const struct micros_stack_user_image *image;
    struct micros_stack_observation copy;
};

static struct application_observation application_live[MICROS_THREAD_CAPACITY];
static struct application_observation application_closed[16];
static size_t application_closed_count;

static void retain_application(
    struct application_observation *destination,
    const struct application_observation *source
)
{
    size_t index;

    destination->process.slot = source->process.slot;
    destination->process.generation = source->process.generation;
    destination->thread.slot = source->thread.slot;
    destination->thread.generation = source->thread.generation;
    destination->root = source->root;
    destination->image = source->image;
    destination->copy.generation = source->copy.generation;
    destination->copy.use = source->copy.use;
    destination->copy.samples = source->copy.samples;
    destination->copy.failure = source->copy.failure;
    destination->copy.closed = source->copy.closed;
    for (index = 0; index < 3; ++index) {
        destination->copy.reserved[index] = source->copy.reserved[index];
    }
}
#endif

static bool immutable_image_matches(
    struct micros_process_handle process,
    const struct micros_stack_user_image *image
)
{
    size_t region;

    if (image->region_count != 2) {
        return false;
    }
    for (region = 0; region < image->region_count; ++region) {
        uint64_t address = image->regions[region].address;
        uint64_t remaining = image->regions[region].size;
        uint64_t digest = UINT64_C(0xcbf29ce484222325);

        while (remaining != 0) {
            uint64_t physical;
            uint32_t permissions;
            size_t contiguous;
            size_t count;
            size_t index;
            const uint8_t *bytes;

            if (micros_user_address_space_translate(
                process, address, &physical, &permissions, &contiguous
            ) != MICROS_USER_ADDRESS_SPACE_OK
                || (permissions != MICROS_SV39_PERMISSION_READ
                    && permissions != (MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE))
                || contiguous == 0) {
                return false;
            }
            count = remaining < contiguous ? (size_t)remaining : contiguous;
            bytes = (const uint8_t *)(uintptr_t)physical;
            for (index = 0; index < count; ++index) {
                digest = (digest ^ bytes[index]) * UINT64_C(0x100000001b3);
            }
            address += count;
            remaining -= count;
        }
        if (digest != image->regions[region].digest) {
            return false;
        }
    }
    return true;
}

static const struct micros_stack_user_image *image_record(uint32_t image_id)
{
    size_t index;

    for (index = 0; index < MICROS_STACK_USER_IMAGE_COUNT; ++index) {
        if (micros_stack_user_images[index].image_id == image_id) {
            return &micros_stack_user_images[index];
        }
    }
    return NULL;
}

static uint64_t read_user_record(
    struct micros_process_handle handle,
    uintptr_t expected_root,
    const struct micros_stack_user_image *image,
    struct micros_stack_observation *copy
)
{
    const struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_registry();
    const struct micros_process *process;
    const struct micros_stack_observation *source;
    uint64_t physical;
    uint32_t permissions;
    size_t contiguous;
    size_t index;

    if (image == NULL || image->size != sizeof(*copy)) {
        return 0x103;
    }
    if (micros_process_resolve(objects, handle, &process) != MICROS_KERNEL_OBJECT_OK) {
        return 0x104;
    }
    if (process->address_space_root != expected_root) {
        return 0x105;
    }
    if (!immutable_image_matches(handle, image)) {
        return 0x10b;
    }
    if (micros_user_address_space_translate(
        handle, image->address, &physical, &permissions, &contiguous
    ) != MICROS_USER_ADDRESS_SPACE_OK) {
        return 0x106;
    }
    if (permissions != (MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE)) {
        return 0x107;
    }
    if (contiguous < sizeof(*copy) || physical % 8 != 0) {
        return 0x108;
    }
    source = (const struct micros_stack_observation *)(uintptr_t)physical;
    copy->generation = source->generation;
    copy->use = source->use;
    copy->samples = source->samples;
    copy->failure = source->failure;
    copy->closed = source->closed;
    for (index = 0; index < 3; ++index) {
        copy->reserved[index] = source->reserved[index];
        if (copy->reserved[index] != 0) {
            return 0x109;
        }
    }
    return (
        copy->generation == 0 && copy->closed == 0
        && copy->samples != 0 && copy->failure == 0 && copy->use <= 4096
    ) ? 0 : 0x10a;
}

void micros_stack_observer_capture_bootstrap_users(void)
{
    const struct micros_bootstrap_control_state *state =
        micros_bootstrap_runtime_state();
    size_t index;

    if (
        state == NULL || state->entry_count == 0 || state->entry_count > 7
        || state->phase != MICROS_BOOTSTRAP_PHASE_SEALED
        || micros_bootstrap_runtime_validate() != MICROS_BOOTSTRAP_OK
    ) {
        micros_stack_observer_failure = 0x101;
        return;
    }
    for (index = 0; index < state->entry_count; ++index) {
        const struct micros_bootstrap_binding *binding = &state->bindings[index];
        const struct micros_stack_user_image *image;
        uint32_t id;
        uint64_t error;

        if (
            binding->manifest_index >= state->entry_count
            || state->manifest.entries[binding->manifest_index].process_slot != binding->process.slot
        ) {
            micros_stack_observer_failure = 0x102;
            return;
        }
        id = state->manifest.entries[binding->manifest_index].image_id;
        image = image_record(id);
        error = read_user_record(binding->process, binding->root, image, &user_copies[index]);
        if (error != 0) {
            micros_stack_observer_failure = error;
            return;
        }
        micros_stack_observer_write_record(
            image->name, "user", binding->process.slot, binding->process.generation,
            &user_copies[index], UINT64_C(0x7ffff000), UINT64_C(0x80000000)
        );
    }
#ifdef MICROS_STACK_OBSERVER_APPLICATIONS
    for (index = 0; index < application_closed_count; ++index) {
        const struct application_observation *record = &application_closed[index];

        micros_stack_observer_write_record(
            record->image->name, "user", record->process.slot, record->process.generation,
            &record->copy, UINT64_C(0x7ffff000), UINT64_C(0x80000000)
        );
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        struct application_observation *record = &application_live[index];
        uint64_t error;

        if (record->image == NULL) {
            continue;
        }
        error = read_user_record(record->process, record->root, record->image, &record->copy);
        if (error != 0) {
            micros_stack_observer_failure = error;
            return;
        }
        micros_stack_observer_write_record(
            record->image->name, "user", record->process.slot, record->process.generation,
            &record->copy, UINT64_C(0x7ffff000), UINT64_C(0x80000000)
        );
    }
#endif
}

#ifdef MICROS_STACK_OBSERVER_APPLICATIONS
void micros_stack_observer_user_attached(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
)
{
    const struct micros_thread *resolved;
    const struct micros_process *process;
    const struct micros_stack_user_image *image = NULL;
    struct application_observation *record;
    size_t index;

    if (micros_thread_resolve(objects, thread, &resolved) != MICROS_KERNEL_OBJECT_OK
        || micros_process_resolve(objects, resolved->owner, &process) != MICROS_KERNEL_OBJECT_OK) {
        micros_stack_observer_failure = 0x110;
        return;
    }
    if (process->privilege_profile != MICROS_PRIVILEGE_PROFILE_APPLICATION) {
        return;
    }
    record = &application_live[thread.slot];
    if (record->image != NULL) {
        micros_stack_observer_failure = 0x111;
        return;
    }
    for (index = 0; index < MICROS_STACK_USER_IMAGE_COUNT; ++index) {
        if (micros_stack_user_images[index].image_id < 200) {
            continue;
        }
        if (immutable_image_matches(resolved->owner, &micros_stack_user_images[index])) {
            if (image != NULL) {
                micros_stack_observer_failure = 0x112;
                return;
            }
            image = &micros_stack_user_images[index];
        }
    }
    if (image == NULL) {
        micros_stack_observer_failure = 0x113;
        return;
    }
    record->process = resolved->owner;
    record->thread = thread;
    record->root = process->address_space_root;
    record->image = image;
}

void micros_stack_observer_user_detaching(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
)
{
    struct application_observation *record = &application_live[thread.slot];
    const struct micros_thread *resolved;
    uint64_t error;

    if (record->image == NULL) {
        return;
    }
    if (record->thread.generation != thread.generation
        || micros_thread_resolve(objects, thread, &resolved) != MICROS_KERNEL_OBJECT_OK
        || resolved->owner.slot != record->process.slot
        || resolved->owner.generation != record->process.generation) {
        micros_stack_observer_failure = 0x114;
        return;
    }
    if (micros_stack_observations[thread.slot + 3].samples != 0) {
        error = read_user_record(record->process, record->root, record->image, &record->copy);
        if (error != 0 || application_closed_count == 16) {
            micros_stack_observer_failure = error == 0 ? 0x115 : error;
            return;
        }
        retain_application(&application_closed[application_closed_count], record);
        ++application_closed_count;
    }
    record->image = NULL;
}
#endif
