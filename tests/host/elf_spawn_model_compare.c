#include "tests/host/elf_spawn_model_internal.h"

#include "micros/sv39.h"

static micros_endpoint_t reference_endpoint(const struct elf_reference_process *process)
{
    return process->kernel_generation << MICROS_ENDPOINT_SLOT_BITS | process->kernel_slot;
}

static void expected_pm_record(
    const struct elf_reference_state *reference, size_t slot, struct micros_pm_process_record *record
)
{
    const struct elf_reference_process *process = &reference->processes[slot];

    memset(record, 0, sizeof(*record));
    record->generation = process->generation;
    record->endpoint = MICROS_ENDPOINT_NONE;
    record->spawn.prepared_endpoint = MICROS_ENDPOINT_NONE;
    if (process->state == ELF_REFERENCE_FREE) {
        return;
    }
    record->pid = process->pid;
    if (process->parent != ELF_MODEL_NO_SLOT) {
        record->parent.slot = process->parent;
        record->parent.generation = reference->processes[process->parent].generation;
    }
    if (process->state == ELF_REFERENCE_ZOMBIE) {
        record->state = MICROS_PM_PROCESS_ZOMBIE;
        record->exit_kind = MICROS_PM_EXIT_NORMAL;
        record->exit_code = process->status;
        return;
    }
    record->state = MICROS_PM_PROCESS_RUNNING;
    record->reaper = process->pid == 1;
    record->endpoint = reference_endpoint(process);
    record->load_token = process->load;
    record->mapping_generation = process->mapping;
    if (process->waiting) {
        record->wait.active = true;
        record->wait.flags = process->wait_flags;
        record->wait.selector = process->wait_selector;
        record->wait.reply_token = process->wait_token;
    }
}

static bool compare_pm(const struct elf_spawn_model_context *context)
{
    size_t slot;

    if (micros_pm_table_validate(&context->pm) != MICROS_PM_MODEL_OK
        || !context->pm.runtime_enabled || !context->pm.init_attempted || context->pm.pid_exhausted
        || context->pm.last_pid != context->reference.last_pid
        || context->pm.reaper.slot != 0 || context->pm.reaper.reserved != 0
        || context->pm.reaper.generation != context->reference.processes[0].generation
        || context->coordinator.last_spawn_token != context->reference.spawn_tokens
        || context->coordinator.spawn_pending || context->coordinator.parent_lost
        || context->coordinator.pending_parent_reply != 0) {
        return false;
    }
    for (slot = 0; slot < MICROS_PM_PROCESS_CAPACITY; ++slot) {
        struct micros_pm_process_record expected;

        expected_pm_record(&context->reference, slot, &expected);
        if (memcmp(&expected, &context->pm.records[slot], sizeof(expected)) != 0) {
            return false;
        }
    }
    return true;
}

static uint32_t expected_page_permissions(size_t index)
{
    uint32_t permissions = MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ;

    return permissions | (index == 0 ? MICROS_SV39_PERMISSION_EXECUTE
        : index >= 2 ? MICROS_SV39_PERMISSION_WRITE : 0);
}

static void expected_vm_record(
    const struct elf_reference_process *process, struct micros_vm_mapping_record *record
)
{
    size_t index;

    memset(record, 0, sizeof(*record));
    record->load_token = process->load;
    record->mapping_generation = process->mapping;
    record->semantic_pid = process->pid;
    record->process_generation = process->kernel_generation;
    record->process_slot = process->kernel_slot;
    record->phase = MICROS_VM_MAPPING_RECORD_ACTIVE;
    record->entry_count = 8;
    for (index = 0; index < 8; ++index) {
        struct micros_vm_mapping_entry *entry = &record->entries[index];

        entry->physical_address = UINT64_C(0x80001000) + process->frames[index] * UINT64_C(4096);
        if (index < 4) {
            entry->role = (uint8_t)(MICROS_VM_MAPPING_ENTRY_ROOT_PAGE_TABLE + index);
        } else {
            size_t page = index - 4;

            entry->virtual_address = page < 3
                ? MICROS_APPLICATION_ENTRY + page * UINT64_C(4096) : MICROS_APPLICATION_STACK_BASE;
            entry->permissions = expected_page_permissions(page);
            entry->role = MICROS_VM_MAPPING_ENTRY_USER_PAGE;
        }
    }
}

static const struct elf_reference_process *reference_for_kernel_slot(
    const struct elf_reference_state *reference, size_t kernel_slot
)
{
    size_t slot;

    for (slot = 0; slot < MICROS_PM_PROCESS_CAPACITY; ++slot) {
        if (reference->processes[slot].state == ELF_REFERENCE_RUNNING
            && reference->processes[slot].kernel_slot == kernel_slot) {
            return &reference->processes[slot];
        }
    }
    return NULL;
}

static bool compare_vm_records(const struct elf_spawn_model_context *context)
{
    size_t slot;

    for (slot = 0; slot < MICROS_VM_MAPPING_RECORD_CAPACITY; ++slot) {
        const struct elf_reference_process *process = reference_for_kernel_slot(&context->reference, slot);
        struct micros_vm_mapping_record expected = {0};
        struct elf_model_kernel_process kernel = {.generation = context->kernel[slot].generation};

        if (process != NULL) {
            size_t index;

            expected_vm_record(process, &expected);
            kernel.phase = ELF_KERNEL_ACTIVE;
            kernel.load = process->load;
            kernel.mapping = process->mapping;
            kernel.page_count = 4;
            for (index = 0; index < 4; ++index) {
                kernel.table_frames[index] = process->frames[index];
                kernel.page_frames[index] = process->frames[4 + index];
                kernel.page_addresses[index] = expected.entries[4 + index].virtual_address;
                kernel.page_permissions[index] = expected_page_permissions(index);
            }
            if (kernel.generation != process->kernel_generation) {
                return false;
            }
        } else if (slot < ELF_MODEL_FIRST_PROCESS) {
            kernel.phase = ELF_KERNEL_STATIC;
        }
        if (memcmp(&expected, &context->vm->state.mapping_records[slot], sizeof(expected)) != 0
            || memcmp(&kernel, &context->kernel[slot], sizeof(kernel)) != 0) {
            return false;
        }
    }
    return true;
}

static bool compare_vm_frames(const struct elf_spawn_model_context *context)
{
    uint8_t expected[TEST_MANAGED_FRAME_COUNT];
    enum elf_model_frame_owner kernel[TEST_MANAGED_FRAME_COUNT];
    size_t slot;
    size_t index;

    memcpy(expected, context->initial_frames, sizeof(expected));
    for (index = 0; index < TEST_MANAGED_FRAME_COUNT; ++index) {
        kernel[index] = index < TEST_FIRST_FREE_FRAME ? ELF_FRAME_BOOT : ELF_FRAME_FREE;
    }
    for (slot = 0; slot < MICROS_PM_PROCESS_CAPACITY; ++slot) {
        const struct elf_reference_process *process = &context->reference.processes[slot];

        if (process->state != ELF_REFERENCE_RUNNING) {
            continue;
        }
        for (index = 0; index < 8; ++index) {
            uint32_t frame = process->frames[index];

            if (frame >= TEST_MANAGED_FRAME_COUNT || kernel[frame] != ELF_FRAME_FREE) {
                return false;
            }
            expected[frame] = index < 4 ? MICROS_VM_LOCAL_FRAME_PROCESS_PAGE_TABLE
                : MICROS_VM_LOCAL_FRAME_PROCESS_WIRED;
            kernel[frame] = index < 4 ? ELF_FRAME_TABLE : ELF_FRAME_USER;
        }
    }
    return memcmp(expected, context->vm->boot_info.frame_states, sizeof(expected)) == 0
        && memcmp(kernel, context->frame_owners, sizeof(kernel)) == 0;
}

static size_t expected_vfs_process(
    const struct elf_reference_state *reference, size_t vfs_slot, struct micros_vfs_process_record *record
)
{
    size_t slot;
    size_t running = 0;

    memset(record, 0, sizeof(*record));
    for (slot = 0; slot < MICROS_PM_PROCESS_CAPACITY; ++slot) {
        const struct elf_reference_process *process = &reference->processes[slot];

        if (process->state != ELF_REFERENCE_RUNNING) {
            continue;
        }
        ++running;
        if (process->vfs_slot == vfs_slot) {
            record->state = MICROS_VFS_PROCESS_ACTIVE;
            record->endpoint = reference_endpoint(process);
            record->root_vnode = 1;
            record->working_directory_vnode = 1;
            record->descriptors[0] = 1;
            record->descriptors[1] = 2;
            record->descriptors[2] = 3;
        }
    }
    return running;
}

static bool compare_vfs_ownership(const struct elf_spawn_model_context *context)
{
    size_t slot;
    size_t running = 0;

    for (slot = 0; slot < MICROS_VFS_PROCESS_CAPACITY; ++slot) {
        struct micros_vfs_process_record expected;

        running = expected_vfs_process(&context->reference, slot, &expected);
        if (memcmp(&expected, &context->vfs.processes[slot], sizeof(expected)) != 0) {
            return false;
        }
    }
    for (slot = 0; slot < MICROS_VFS_OPEN_FILE_CAPACITY; ++slot) {
        struct micros_vfs_open_file expected = {0};

        if (slot < 3) {
            expected.state = MICROS_VFS_OPEN_FILE_ACTIVE;
            expected.kind = MICROS_VFS_OBJECT_CONSOLE;
            expected.access = slot == 0 ? MICROS_VFS_ACCESS_READ : MICROS_VFS_ACCESS_WRITE;
            expected.open_flags = slot == 0 ? MICROS_VFS_OPEN_READ : MICROS_VFS_OPEN_WRITE;
            expected.reference_count = (uint32_t)running;
        }
        if (memcmp(&expected, &context->vfs.open_files[slot], sizeof(expected)) != 0) {
            return false;
        }
    }
    for (slot = 0; slot < MICROS_VFS_VNODE_CAPACITY; ++slot) {
        struct micros_vfs_vnode expected = {0};

        if (slot == 0) {
            expected = context->root_vnode;
            expected.local_reference_count = 1 + (uint32_t)running * 2;
        }
        if (memcmp(&expected, &context->vfs.vnodes[slot], sizeof(expected)) != 0
            || context->filesystem.nodes[slot].backend_references != (uint32_t)(slot == 0)) {
            return false;
        }
    }
    return true;
}

static bool compare_idle_resources(const struct elf_spawn_model_context *context)
{
    size_t slot;

    if (context->vm->state.phase != MICROS_VM_LOADER_IDLE || context->kernel_slot != ELF_MODEL_NO_SLOT
        || !vm_core_test_bytes_are_zero(&context->vm->state.transaction, sizeof(context->vm->state.transaction))
        || !vm_core_test_bytes_are_zero(context->vm->state.executable_bytes, sizeof(context->vm->state.executable_bytes))
        || !vm_core_test_bytes_are_zero(context->vm->state.request_bytes, sizeof(context->vm->state.request_bytes))
        || !vm_core_test_bytes_are_zero(context->vfs.spawn_request_bytes, sizeof(context->vfs.spawn_request_bytes))
        || !vm_core_test_bytes_are_zero(context->vfs.spawn_executable_bytes, sizeof(context->vfs.spawn_executable_bytes))
        || memcmp(&context->vfs.spawn, &context->idle_vfs_spawn, sizeof(context->vfs.spawn)) != 0
        || memcmp(&context->vfs.pending, &context->idle_vfs_pending, sizeof(context->vfs.pending)) != 0
        || context->vfs.client_page_owner != MICROS_VFS_PAGE_FREE
        || context->vfs.backend_page_owner != MICROS_VFS_PAGE_FREE
        || context->filesystem.backend_grant_count != 0 || context->filesystem.tty_output_length != 0
        || context->filesystem.pending_read.active || context->filesystem.read_completion.active
        || context->filesystem.write_completion.active || context->filesystem.tty_output_busy) {
        return false;
    }
    for (slot = 0; slot < MICROS_GRANT_CAPACITY; ++slot) {
        if (context->filesystem.backend_grants[slot].active) {
            return false;
        }
    }
    for (slot = 0; slot < VFS_TEST_APPLICATION_GRANT_CAPACITY; ++slot) {
        if (context->filesystem.application_grants[slot].active) {
            return false;
        }
    }
    return true;
}

bool elf_model_compare(const struct elf_spawn_model_context *context)
{
    size_t index;

    if (context->violation || !compare_pm(context)
        || micros_vm_state_validate(&context->vm->state) != MICROS_VM_CORE_OK
        || micros_vfs_state_validate(&context->vfs) != MICROS_VFS_CORE_OK
        || !compare_vm_records(context) || !compare_vm_frames(context)
        || !compare_vfs_ownership(context) || !compare_idle_resources(context)
        || context->vfs.last_image_token != context->reference.image_tokens
        || context->vfs.last_descriptor_token != context->reference.descriptor_tokens
        || context->vfs.next_tty_request_id != context->initial_tty_request
        || context->stage_count != context->expected_stage_count
        || memcmp(context->stages, context->expected_stages, context->stage_count * sizeof(uint32_t)) != 0
        || context->reply_count != context->expected_reply_count) {
        return false;
    }
    for (index = 0; index < context->reply_count; ++index) {
        if (context->replies[index].token != context->expected_replies[index].token
            || context->replies[index].pid != context->expected_replies[index].pid
            || context->replies[index].status != context->expected_replies[index].status) {
            return false;
        }
    }
    return true;
}

bool elf_model_compare_response(
    const struct elf_spawn_model_context *context, const struct micros_pm_coordinator_response *response,
    enum micros_pm_coordinator_response_kind kind, uint32_t type,
    enum micros_pm_result result, uint64_t pid
)
{
    if (response->kind != kind || response->reserved != 0) {
        return false;
    }
    if (kind != MICROS_PM_COORDINATOR_RESPONSE_REPLY) {
        return response->reply_token == 0 && response->message.source == MICROS_ENDPOINT_NONE
            && response->message.type == 0 && response->message.reply_token == 0
            && vm_core_test_bytes_are_zero(response->message.payload, sizeof(response->message.payload));
    }
    return response->reply_token == context->operation_reply
        && response->message.type == MICROS_PM_MESSAGE_RESULT && response->message.reply_token == 0
        && elf_model_read_word(&response->message.payload[0]) == (UINT64_C(1) | (uint64_t)type << 32)
        && elf_model_read_word(&response->message.payload[8]) == (uint32_t)result
        && elf_model_read_word(&response->message.payload[16]) == pid
        && vm_core_test_bytes_are_zero(&response->message.payload[24], 24);
}

bool elf_model_oracle_self_test(struct elf_spawn_model_context *context)
{
    uint64_t pid = context->pm.records[0].pid;
    uint32_t references = context->vfs.open_files[0].reference_count;
    uint64_t physical = context->vm->state.mapping_records[ELF_MODEL_FIRST_PROCESS].entries[0].physical_address;
    bool rejected_pm;
    bool rejected_vfs;
    bool rejected_vm;

    ++context->pm.records[0].pid;
    rejected_pm = !elf_model_compare(context);
    context->pm.records[0].pid = pid;
    ++context->vfs.open_files[0].reference_count;
    rejected_vfs = !elf_model_compare(context);
    context->vfs.open_files[0].reference_count = references;
    context->vm->state.mapping_records[ELF_MODEL_FIRST_PROCESS].entries[0].physical_address += 4096;
    rejected_vm = !elf_model_compare(context);
    context->vm->state.mapping_records[ELF_MODEL_FIRST_PROCESS].entries[0].physical_address = physical;
    return rejected_pm && rejected_vfs && rejected_vm && elf_model_compare(context);
}
