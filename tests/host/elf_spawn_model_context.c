#include "tests/host/elf_spawn_model_internal.h"

static struct elf_spawn_model_context model_context;

struct elf_spawn_model_context *elf_model_context_get(void)
{
    return &model_context;
}

uint64_t elf_model_read_word(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

bool elf_model_kernel_require(struct elf_spawn_model_context *context, bool valid)
{
    if (!valid) {
        context->violation = true;
    }
    return valid;
}

bool elf_model_record_stage(struct elf_spawn_model_context *context, uint32_t stage)
{
    if (
        context->stage_count >= ELF_MODEL_STAGE_CAPACITY
        || stage == 0 || stage > ELF_STAGE_EXIT_RELEASE
    ) {
        context->violation = true;
        return false;
    }
    context->stages[context->stage_count++] = stage;
    ++context->stage_coverage[stage];
    return true;
}

void elf_model_cancel_at(struct elf_spawn_model_context *context, uint32_t stage)
{
    if (
        (context->operation == ELF_MODEL_CANCEL_FROZEN && stage == ELF_STAGE_LOAD)
        || (context->operation == ELF_MODEL_CANCEL_ACTIVE && stage == ELF_STAGE_ACTIVATE)
    ) {
        if (
            micros_pm_coordinator_parent_lost(&context->coordinator, context->operation_reply)
                != MICROS_PM_COORDINATOR_OK
        ) {
            context->violation = true;
        }
    }
}

static bool initialize_services(struct elf_spawn_model_context *context)
{
    const struct micros_pm_coordinator_bindings pm = {
        .self = ELF_MODEL_PM, .vm = ELF_MODEL_VM, .vfs = ELF_MODEL_VFS,
    };
    const struct micros_vm_service_bindings vm = {
        .self = ELF_MODEL_VM, .pm = ELF_MODEL_PM, .vfs = ELF_MODEL_VFS,
    };
    struct micros_ipc_message sealed = {
        .source = MICROS_ENDPOINT_NONE, .type = MICROS_IPC_TYPE_KERNEL_NOTIFICATION,
    };

    vm_core_test_write_u64_le(sealed.payload, MICROS_KERNEL_EVENT_BOOTSTRAP_SEALED);
    ELF_MODEL_EXPECT(context,
        micros_pm_table_initialize(&context->pm) == MICROS_PM_MODEL_OK
        && micros_pm_coordinator_initialize(&context->coordinator, &context->pm, &pm)
            == MICROS_PM_COORDINATOR_OK
        && micros_pm_enable_runtime(&context->pm, &sealed) == MICROS_PM_MODEL_OK
        && micros_vm_service_initialize(&context->vm_service, &context->vm->state, &vm)
            == MICROS_VM_SERVICE_OK
    );
    vfs_test_fixture_initialize(&context->filesystem, ELF_MODEL_VFS, ELF_MODEL_VM,
                                ELF_MODEL_PM, UINT32_C(0x1004), UINT32_C(0x1003));
    ELF_MODEL_EXPECT(context,
        micros_vfs_state_initialize(&context->vfs, ELF_MODEL_VFS, ELF_MODEL_VM,
                                    ELF_MODEL_PM, UINT32_C(0x1004), UINT32_C(0x1003))
            == MICROS_VFS_CORE_OK
        && micros_vfs_mount(&context->vfs, &context->filesystem.io) == MICROS_VFS_CORE_OK
        && vfs_test_fixture_add_root_file_with_mode(&context->filesystem, "app",
             MICROS_RAMFS_MODE_REGULAR | 0755, context->vm->executable, context->executable_size)
    );
    context->root_node = context->vfs.vnodes[0].node;
    context->executable_node =
        (size_t)(vfs_test_fixture_find_path(&context->filesystem, "/app") & UINT64_C(0xffff));
    ELF_MODEL_EXPECT(context, context->executable_node < MICROS_RAMFS_NODE_CAPACITY);
    context->idle_vfs_spawn = context->vfs.spawn;
    context->idle_vfs_pending = context->vfs.pending;
    context->root_vnode = context->vfs.vnodes[0];
    context->initial_tty_request = context->vfs.next_tty_request_id;
    return true;
}

bool elf_model_initialize(struct elf_spawn_model_context *context)
{
    size_t slot;

    memset(context, 0, sizeof(*context));
    context->vm = vm_core_test_context_get();
    context->seed = UINT64_C(0x8d13bca057e2469f);
    context->random = context->seed;
    context->trace_digest = UINT64_C(14695981039346656037);
    context->kernel_slot = ELF_MODEL_NO_SLOT;
    context->supplied_slot = ELF_MODEL_NO_SLOT;
    ELF_MODEL_EXPECT(context,
        vm_core_test_initialize_boot_info(context->vm, TEST_MANAGED_FRAME_COUNT)
        && vm_core_test_initialize_state(context->vm) == MICROS_VM_CORE_OK
    );
    memcpy(context->initial_frames, context->vm->boot_info.frame_states, sizeof(context->initial_frames));
    context->executable_size = vm_core_test_build_elf(context->vm, 1, 1, 1);
    for (slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
        context->kernel[slot].phase = slot < ELF_MODEL_FIRST_PROCESS ? ELF_KERNEL_STATIC : ELF_KERNEL_FREE;
        context->kernel[slot].generation = 1;
    }
    for (slot = 0; slot < TEST_MANAGED_FRAME_COUNT; ++slot) {
        context->frame_owners[slot] = slot < TEST_FIRST_FREE_FRAME ? ELF_FRAME_BOOT : ELF_FRAME_FREE;
    }
    return initialize_services(context);
}
