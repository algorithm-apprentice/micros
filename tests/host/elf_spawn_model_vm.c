#include "tests/host/elf_spawn_model_internal.h"

#include "micros/sv39.h"

static struct elf_model_kernel_process *load_process(
    struct elf_spawn_model_context *context, uint64_t token
)
{
    size_t slot;

    for (slot = ELF_MODEL_FIRST_PROCESS; slot < MICROS_PROCESS_CAPACITY; ++slot) {
        if (context->kernel[slot].phase != ELF_KERNEL_FREE && context->kernel[slot].load == token) {
            return &context->kernel[slot];
        }
    }
    return NULL;
}

static size_t model_frame_index(uint64_t physical)
{
    uint64_t offset = physical - UINT64_C(0x80001000);

    return physical >= UINT64_C(0x80001000) && offset % 4096 == 0
        && offset / 4096 < TEST_MANAGED_FRAME_COUNT
        ? (size_t)(offset / 4096) : TEST_MANAGED_FRAME_COUNT;
}

static micros_runtime_result_t model_vm_copy(
    void *opaque, micros_endpoint_t grantor, micros_grant_t grant,
    size_t offset, uintptr_t destination, size_t length
)
{
    struct elf_spawn_model_context *context = opaque;
    const struct vfs_test_backend_grant *record =
        vfs_test_fixture_backend_grant(&context->filesystem, grant);

    if (!elf_model_kernel_require(context, grantor == ELF_MODEL_VFS
        && record != NULL && record->active && record->endpoint == ELF_MODEL_VM
        && record->permission == MICROS_GRANT_PERMISSION_READ
        && length <= 4096 && offset <= record->length && length <= record->length - offset)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    memcpy((void *)destination, &record->local[offset], length);
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t model_vm_begin(
    void *opaque, uint64_t reservation, struct micros_vm_load_begin_result *result
)
{
    struct elf_spawn_model_context *context = opaque;
    struct elf_model_kernel_process *process;

    if (!elf_model_kernel_require(context, context->kernel_slot < MICROS_PROCESS_CAPACITY)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    process = &context->kernel[context->kernel_slot];
    if (!elf_model_kernel_require(context,
        process->phase == ELF_KERNEL_RESERVED && process->reservation == reservation)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    process->phase = ELF_KERNEL_LOADING;
    process->load = ++context->last_load;
    context->supplied_load = process->load;
    memset(result, 0, sizeof(*result));
    result->version = MICROS_VM_LOAD_BEGIN_RESULT_VERSION;
    result->size = MICROS_VM_LOAD_BEGIN_RESULT_SIZE;
    result->load_token = process->load;
    result->process_slot = (uint16_t)context->kernel_slot;
    result->process_generation = process->generation;
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t model_vm_map(
    void *opaque, uint64_t token, const struct micros_vm_load_page_action *page
)
{
    struct elf_spawn_model_context *context = opaque;
    struct elf_model_kernel_process *process = load_process(context, token);
    size_t frame = model_frame_index(page->physical_address);
    uint32_t index = process == NULL ? 4 : process->page_count;
    uint32_t permissions = MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ;
    uint64_t address = index < 3 ? MICROS_APPLICATION_ENTRY + index * UINT64_C(4096)
        : MICROS_APPLICATION_STACK_BASE;

    permissions |= index == 0 ? MICROS_SV39_PERMISSION_EXECUTE
        : index >= 2 ? MICROS_SV39_PERMISSION_WRITE : 0;
    if (!elf_model_kernel_require(context, process != NULL && process->phase == ELF_KERNEL_LOADING
        && index < 4 && !process->scratch_mapped && page->page_index == index
        && page->virtual_address == address && page->permissions == permissions
        && frame < TEST_MANAGED_FRAME_COUNT && context->frame_owners[frame] == ELF_FRAME_FREE)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    process->page_frames[index] = (uint32_t)frame;
    process->page_addresses[index] = address;
    process->page_permissions[index] = permissions;
    ++process->page_count;
    process->scratch_mapped = true;
    context->frame_owners[frame] = ELF_FRAME_TRANSFERABLE;
    return MICROS_SYSCALL_ABI_OK;
}

static uint8_t *model_vm_scratch(void *opaque)
{
    struct elf_spawn_model_context *context = opaque;

    return context->vm->scratch_page;
}

static micros_runtime_result_t model_vm_scratch_commit(
    void *opaque, uint64_t token, const struct micros_vm_load_page_action *page
)
{
    struct elf_spawn_model_context *context = opaque;
    struct elf_model_kernel_process *process = load_process(context, token);

    if (!elf_model_kernel_require(context, process != NULL && process->phase == ELF_KERNEL_LOADING
        && process->scratch_mapped && process->page_count == page->page_index + 1
        && process->page_frames[page->page_index] == model_frame_index(page->physical_address))) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    process->scratch_mapped = false;
    return MICROS_SYSCALL_ABI_OK;
}

static uint64_t kernel_digest_word(uint64_t digest, uint64_t word, size_t bytes)
{
    size_t index;

    for (index = 0; index < bytes; ++index) {
        digest ^= (uint8_t)(word >> (index * 8));
        digest *= UINT64_C(1099511628211);
    }
    return digest;
}

static uint64_t kernel_page_digest(const struct elf_model_kernel_process *process)
{
    uint64_t digest = UINT64_C(14695981039346656037);
    size_t index;

    digest = kernel_digest_word(digest, 3, 4);
    digest = kernel_digest_word(digest, 4, 4);
    for (index = 0; index < process->page_count; ++index) {
        digest = kernel_digest_word(digest, process->page_addresses[index], 8);
        digest = kernel_digest_word(digest,
            UINT64_C(0x80001000) + process->page_frames[index] * UINT64_C(4096), 8);
        digest = kernel_digest_word(digest, process->page_permissions[index], 4);
        digest = kernel_digest_word(digest, 0, 4);
    }
    return digest;
}

static bool kernel_table_plan_is_valid(
    const struct elf_spawn_model_context *context, const struct micros_vm_load_freeze_plan *plan
)
{
    size_t index;
    size_t previous;

    for (index = 0; index < 4; ++index) {
        size_t frame = model_frame_index(plan->table_frames[index]);

        if (frame >= TEST_MANAGED_FRAME_COUNT || context->frame_owners[frame] != ELF_FRAME_FREE) {
            return false;
        }
        for (previous = 0; previous < index; ++previous) {
            if (plan->table_frames[previous] == plan->table_frames[index]) {
                return false;
            }
        }
    }
    return true;
}

static micros_runtime_result_t model_vm_freeze(
    void *opaque, const struct micros_vm_load_freeze_plan *plan,
    struct micros_vm_load_freeze_result *result
)
{
    struct elf_spawn_model_context *context = opaque;
    struct elf_model_kernel_process *process = load_process(context, plan->load_token);
    size_t index;

    if (!elf_model_kernel_require(context, process != NULL && process->phase == ELF_KERNEL_LOADING
        && !process->scratch_mapped && process->page_count == 4 && plan->version == 1
        && plan->size == 96 && plan->entry == MICROS_APPLICATION_ENTRY
        && plan->stack_pointer == UINT64_C(0x7fffff70) && plan->image_page_count == 3
        && plan->data_page_count == 4 && plan->reserved[0] == 0 && plan->reserved[1] == 0
        && kernel_table_plan_is_valid(context, plan) && plan->page_plan_digest == kernel_page_digest(process))) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    for (index = 0; index < 4; ++index) {
        process->table_frames[index] = (uint32_t)model_frame_index(plan->table_frames[index]);
        context->frame_owners[process->table_frames[index]] = ELF_FRAME_TABLE;
        context->frame_owners[process->page_frames[index]] = ELF_FRAME_USER;
    }
    process->phase = ELF_KERNEL_FROZEN;
    process->mapping = ++context->last_mapping;
    context->supplied_mapping = process->mapping;
    memset(result, 0, sizeof(*result));
    result->version = MICROS_VM_LOAD_FREEZE_RESULT_VERSION;
    result->size = MICROS_VM_LOAD_FREEZE_RESULT_SIZE;
    result->mapping_generation = process->mapping;
    return MICROS_SYSCALL_ABI_OK;
}

static void kernel_release_frames(
    struct elf_spawn_model_context *context, const struct elf_model_kernel_process *process
)
{
    size_t index;

    for (index = 0; index < 4; ++index) {
        context->frame_owners[process->table_frames[index]] = ELF_FRAME_FREE;
        context->frame_owners[process->page_frames[index]] = ELF_FRAME_FREE;
    }
}

static micros_runtime_result_t model_vm_abort(void *opaque, uint64_t token, uint64_t mapping)
{
    struct elf_spawn_model_context *context = opaque;
    struct elf_model_kernel_process *process = load_process(context, token);
    uint64_t reservation;
    uint32_t generation;

    if (!elf_model_kernel_require(context, process != NULL
        && process->phase == ELF_KERNEL_FROZEN && process->mapping == mapping)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    kernel_release_frames(context, process);
    reservation = process->reservation;
    generation = process->generation;
    memset(process, 0, sizeof(*process));
    process->phase = ELF_KERNEL_RESERVED;
    process->reservation = reservation;
    process->generation = generation;
    return MICROS_SYSCALL_ABI_OK;
}

static micros_runtime_result_t model_vm_commit(void *opaque, uint64_t token, uint64_t mapping)
{
    struct elf_spawn_model_context *context = opaque;
    struct elf_model_kernel_process *process = load_process(context, token);

    return elf_model_kernel_require(context, process != NULL
        && process->phase == ELF_KERNEL_ACTIVE && process->mapping == mapping
        && process->reservation == 0 && process->prepared == 0)
        ? MICROS_SYSCALL_ABI_OK : MICROS_SYSCALL_ABI_STATE;
}

static micros_runtime_result_t model_vm_exit(
    void *opaque, uint64_t exit_token, uint64_t token, uint64_t mapping
)
{
    struct elf_spawn_model_context *context = opaque;
    struct elf_model_kernel_process *process = load_process(context, token);
    uint32_t generation;

    if (!elf_model_kernel_require(context, process != NULL && process->phase == ELF_KERNEL_DETACHED
        && process->exit == exit_token && process->mapping == mapping)) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    kernel_release_frames(context, process);
    generation = process->generation;
    memset(process, 0, sizeof(*process));
    process->phase = ELF_KERNEL_EMPTY;
    process->generation = generation;
    process->exit = exit_token;
    return MICROS_SYSCALL_ABI_OK;
}

const struct micros_vm_service_backend *elf_model_vm_backend(void)
{
    static const struct micros_vm_service_backend backend = {
        .copy_from = model_vm_copy,
        .load_begin = model_vm_begin,
        .scratch_map = model_vm_map,
        .scratch_page = model_vm_scratch,
        .scratch_commit = model_vm_scratch_commit,
        .freeze = model_vm_freeze,
        .abort_load = model_vm_abort,
        .commit_load = model_vm_commit,
        .exit_release = model_vm_exit,
    };

    return &backend;
}
