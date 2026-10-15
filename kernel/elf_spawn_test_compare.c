#include "tests/qemu/elf_spawn_resources.h"

#define SAME(a, b, member) \
    micros_elf_spawn_resource_bytes_equal( \
        &(a)->member, &(b)->member, sizeof((a)->member) \
    )

void micros_elf_spawn_resource_copy(void *destination, const void *source, size_t size)
{
    uint8_t *to = destination;
    const uint8_t *from = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        to[index] = from[index];
    }
}

bool micros_elf_spawn_resource_bytes_equal(
    const void *left, const void *right, size_t size
)
{
    const unsigned char *a = left;
    const unsigned char *b = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (a[index] != b[index]) {
            return false;
        }
    }
    return true;
}

static bool same_process(
    const struct micros_process *a, const struct micros_process *b
)
{
    return (
        a->slot_state == b->slot_state
        && (a->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || a->generation == b->generation)
        && a->live_thread_count == b->live_thread_count
        && a->address_space_root == b->address_space_root
        && a->primary_endpoint == b->primary_endpoint
        && a->privilege_profile == b->privilege_profile
        && a->endpoint_lifecycle_consumed == b->endpoint_lifecycle_consumed
    );
}

static bool same_thread(
    const struct micros_thread *a, const struct micros_thread *b
)
{
    return (
        a->slot_state == b->slot_state
        && (a->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || a->generation == b->generation)
        && SAME(a, b, owner)
        && a->context_attached == b->context_attached
        && a->kernel_stack_bottom == b->kernel_stack_bottom
        && a->kernel_stack_top == b->kernel_stack_top
        && a->runtime_flags == b->runtime_flags
        && a->scheduler_assigned == b->scheduler_assigned
        && a->scheduler_preemptible == b->scheduler_preemptible
        && a->scheduler_priority == b->scheduler_priority
        && SAME(a, b, scheduler_hart)
        && a->quantum_counter_ticks == b->quantum_counter_ticks
        && a->ready_linked == b->ready_linked
        && SAME(a, b, ready_next)
        && a->ipc_queue_kind == b->ipc_queue_kind
        && SAME(a, b, ipc_next)
        && SAME(a, b, ipc_outbound_message)
        && a->ipc_send_destination == b->ipc_send_destination
        && a->ipc_receive_source == b->ipc_receive_source
        && (a->ipc_receive_buffer == 0) == (b->ipc_receive_buffer == 0)
        && a->ipc_delivery_pending == b->ipc_delivery_pending
        && SAME(a, b, ipc_inbound_message)
        && a->ipc_staged_result == b->ipc_staged_result
        && a->ipc_reply_token == b->ipc_reply_token
        && a->ipc_reply_callee == b->ipc_reply_callee
    );
}

static bool same_hart(
    const struct micros_hart *a, const struct micros_hart *b
)
{
    return (
        a->slot_state == b->slot_state
        && (a->slot_state != MICROS_KERNEL_OBJECT_SLOT_LIVE
            || a->generation == b->generation)
        && a->hardware_id == b->hardware_id
        && a->trap_installed == b->trap_installed
        && a->idle_primary_stack_bottom == b->idle_primary_stack_bottom
        && a->idle_primary_stack_top == b->idle_primary_stack_top
        && a->trap.primary_stack_bottom == b->trap.primary_stack_bottom
        && a->trap.primary_stack_top == b->trap.primary_stack_top
        && a->trap.emergency_stack_bottom == b->trap.emergency_stack_bottom
        && a->trap.emergency_stack_top == b->trap.emergency_stack_top
        && SAME(a, b, current_thread)
        && SAME(a, b, ready_head)
        && SAME(a, b, ready_tail)
        && a->interrupt_depth == b->interrupt_depth
        && a->preempt_disable_count == b->preempt_disable_count
        && a->reschedule_pending == b->reschedule_pending
        && a->timer.initialized == b->timer.initialized
        && a->timer.active == b->timer.active
        && a->timer.interval == b->timer.interval
        && a->timer.test_stop_after == b->timer.test_stop_after
    );
}

static bool same_objects(
    const struct micros_kernel_objects *a, const struct micros_kernel_objects *b
)
{
    size_t index;

    if (
        a->initialization_magic != b->initialization_magic
        || a->max_threads_per_process != b->max_threads_per_process
        || a->max_harts != b->max_harts
        || a->live_process_count != b->live_process_count
        || a->live_thread_count != b->live_thread_count
        || a->registered_hart_count != b->registered_hart_count
    ) {
        return false;
    }
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (!same_process(&a->processes[index], &b->processes[index])) {
            return false;
        }
    }
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        if (!same_thread(&a->threads[index], &b->threads[index])) {
            return false;
        }
    }
    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        if (!same_hart(&a->harts[index], &b->harts[index])) {
            return false;
        }
    }
    return true;
}

static bool same_grants(
    const struct micros_grant_registry *a, const struct micros_grant_registry *b
)
{
    size_t index;

    if (
        a->initialization_magic != b->initialization_magic
        || a->active_count != 0 || b->active_count != 0
    ) {
        return false;
    }
    for (index = 0; index < MICROS_GRANT_CAPACITY; ++index) {
        struct micros_grant_record current;

        micros_elf_spawn_resource_copy(&current, &b->grants[index], sizeof(current));

        if (
            a->grants[index].state != current.state
            || current.state == MICROS_GRANT_SLOT_ACTIVE
        ) {
            return false;
        }
        current.generation = a->grants[index].generation;
        if (!micros_elf_spawn_resource_bytes_equal(
            &a->grants[index], &current, sizeof(current)
        )) {
            return false;
        }
    }
    return true;
}

static bool same_frame_owners(
    const struct micros_frame_ownership *a, const struct micros_frame_ownership *b
)
{
    size_t count = a->managed_frame_count;

    return (
        count <= MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES
        && micros_elf_spawn_resource_bytes_equal(
            a, b, offsetof(struct micros_frame_ownership, owners)
        )
        && micros_elf_spawn_resource_bytes_equal(
            a->owners, b->owners, count * sizeof(a->owners[0])
        )
        && micros_elf_spawn_resource_bytes_equal(
            a->handoff_targets, b->handoff_targets, count
        )
    );
}

static bool same_kernel_vm(
    const struct micros_vm_handoff_state *a, const struct micros_vm_handoff_state *b
)
{
    return (
        micros_elf_spawn_resource_bytes_equal(
            a, b, offsetof(struct micros_vm_handoff_state, last_load_token)
        )
        && b->last_load_token >= a->last_load_token
        && b->last_mapping_generation >= a->last_mapping_generation
        && SAME(a, b, load)
        && SAME(a, b, mappings)
    );
}

static bool same_kernel_pm(
    const struct micros_pm_control_state *a, const struct micros_pm_control_state *b
)
{
    return (
        micros_elf_spawn_resource_bytes_equal(
            a, b, offsetof(struct micros_pm_control_state, last_transaction)
        )
        && b->last_transaction >= a->last_transaction
        && b->last_prepared_token >= a->last_prepared_token
        && b->last_exit_token >= a->last_exit_token
        && a->active_transaction == b->active_transaction
        && SAME(a, b, reserved_process)
        && SAME(a, b, prepared)
        && SAME(a, b, exit)
    );
}

bool micros_elf_spawn_same_kernel_resources(
    const struct micros_elf_spawn_kernel_resources *baseline,
    const struct micros_elf_spawn_kernel_resources *current
)
{
    return (
        baseline != NULL && current != NULL
        && same_objects(&baseline->objects, &current->objects)
        && SAME(&baseline->endpoints, &current->endpoints, initialization_magic)
        && SAME(&baseline->endpoints, &current->endpoints, profile_count)
        && current->endpoints.last_reply_token >= baseline->endpoints.last_reply_token
        && SAME(&baseline->endpoints, &current->endpoints, profiles)
        && SAME(&baseline->endpoints, &current->endpoints, endpoints)
        && same_grants(&baseline->grants, &current->grants)
        && SAME(baseline, current, allocator)
        && same_frame_owners(&baseline->ownership, &current->ownership)
        && same_kernel_vm(&baseline->vm, &current->vm)
        && same_kernel_pm(&baseline->pm, &current->pm)
        && baseline->table_count <= MICROS_ELF_SPAWN_TABLE_CAPACITY
        && baseline->table_count == current->table_count
        && baseline->reserved == current->reserved
        && micros_elf_spawn_resource_bytes_equal(
            baseline->tables, current->tables,
            baseline->table_count * sizeof(baseline->tables[0])
        )
    );
}
