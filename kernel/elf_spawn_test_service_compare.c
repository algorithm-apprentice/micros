#include "tests/qemu/elf_spawn_resources.h"

#define SAME(a, b, member) \
    micros_elf_spawn_resource_bytes_equal( \
        &(a)->member, &(b)->member, sizeof((a)->member) \
    )

static bool bytes_are_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static bool same_pm(
    const struct micros_pm_table *a, const struct micros_pm_table *b
)
{
    size_t index;

    if (
        a->initialization_magic != b->initialization_magic
        || a->runtime_enabled != b->runtime_enabled
        || a->init_attempted != b->init_attempted
        || a->pid_exhausted != b->pid_exhausted
        || b->last_pid < a->last_pid
        || !SAME(a, b, reaper)
    ) {
        return false;
    }
    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        struct micros_pm_process_record current = b->records[index];

        if (current.state == MICROS_PM_PROCESS_FREE) {
            current.generation = a->records[index].generation;
        }
        if (!micros_elf_spawn_resource_bytes_equal(
            &a->records[index], &current, sizeof(current)
        )) {
            return false;
        }
    }
    return true;
}

static bool same_coordinator(
    const struct micros_pm_coordinator *a, const struct micros_pm_coordinator *b
)
{
    return (
        a->initialization_magic == b->initialization_magic
        && a->table == b->table
        && SAME(a, b, bindings)
        && b->last_spawn_token >= a->last_spawn_token
        && a->pending_parent_reply == b->pending_parent_reply
        && a->spawn_pending == b->spawn_pending
        && a->parent_lost == b->parent_lost
        && SAME(a, b, reserved)
    );
}

static bool same_vfs(
    const struct micros_vfs_state *a, const struct micros_vfs_state *b
)
{
    size_t metadata = offsetof(struct micros_vfs_state, client_page_owner);
    size_t pages = offsetof(struct micros_vfs_state, client_page);

    return (
        micros_elf_spawn_resource_bytes_equal(
            a, b, offsetof(struct micros_vfs_state, next_tty_request_id)
        )
        && b->next_tty_request_id >= a->next_tty_request_id
        && b->last_image_token >= a->last_image_token
        && b->last_descriptor_token >= a->last_descriptor_token
        && micros_elf_spawn_resource_bytes_equal(
            (const uint8_t *)a + metadata,
            (const uint8_t *)b + metadata, pages - metadata
        )
        && bytes_are_zero(a->spawn_request_bytes, sizeof(a->spawn_request_bytes))
        && bytes_are_zero(b->spawn_request_bytes, sizeof(b->spawn_request_bytes))
        && bytes_are_zero(a->spawn_executable_bytes, sizeof(a->spawn_executable_bytes))
        && bytes_are_zero(b->spawn_executable_bytes, sizeof(b->spawn_executable_bytes))
    );
}

static bool same_terminal(
    const struct micros_tty_state *a, const struct micros_tty_state *b
)
{
    return (
        a->initialization_magic == b->initialization_magic
        && SAME(a, b, input)
        && a->input_head == b->input_head
        && a->input_count == b->input_count
        && a->current_line_length == b->current_line_length
        && a->completed_line_count == b->completed_line_count
        && SAME(a, b, echo)
        && a->echo_head == b->echo_head
        && a->echo_count == b->echo_count
        && a->read_staging_length == b->read_staging_length
        && a->write_length == b->write_length
        && a->write_cursor == b->write_cursor
        && a->write_resident == b->write_resident
        && a->write_pending_lf == b->write_pending_lf
        && a->transmit_interrupt_enabled == b->transmit_interrupt_enabled
        && a->write_retry_armed == b->write_retry_armed
        && a->receive_error_count == b->receive_error_count
        && a->input_drop_count == b->input_drop_count
        && a->echo_drop_count == b->echo_drop_count
    );
}

static bool same_tty(
    const struct micros_tty_service_state *a, const struct micros_tty_service_state *b
)
{
    return (
        a->initialization_magic == b->initialization_magic
        && a->self_endpoint == b->self_endpoint
        && a->vfs_endpoint == b->vfs_endpoint
        && a->owned == b->owned
        && b->last_accepted_request_id >= a->last_accepted_request_id
        && same_terminal(&a->terminal, &b->terminal)
        && SAME(a, b, pending_read)
        && SAME(a, b, read_completion)
        && SAME(a, b, write_completion)
    );
}

bool micros_elf_spawn_same_service_resources(
    const struct micros_elf_spawn_service_resources *baseline,
    const struct micros_elf_spawn_service_resources *current
)
{
    return (
        baseline != NULL && current != NULL
        && same_pm(&baseline->pm, &current->pm)
        && same_coordinator(&baseline->coordinator, &current->coordinator)
        && SAME(baseline, current, vm)
        && SAME(baseline, current, boot_info)
        && same_vfs(&baseline->vfs, &current->vfs)
        && micros_elf_spawn_resource_bytes_equal(
            &baseline->ramfs, &current->ramfs,
            offsetof(struct micros_ramfs_state, scratch)
        )
        && same_tty(&baseline->tty, &current->tty)
        && SAME(baseline, current, uart)
    );
}
