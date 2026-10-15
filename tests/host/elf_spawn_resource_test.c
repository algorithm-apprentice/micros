#include "tests/host/elf_spawn_fixture_test_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tests/qemu/elf_spawn_resources.h"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); \
            return false; \
        } \
    } while (0)

static struct {
    struct micros_elf_spawn_resources baseline;
    struct micros_elf_spawn_resources current;
} resource_context;

static void reset_resources(void)
{
    struct micros_elf_spawn_resources *baseline = &resource_context.baseline;
    struct micros_kernel_objects *objects = &baseline->kernel.objects;
    struct micros_pm_process_record *init = &baseline->services.pm.records[0];

    memset(&resource_context, 0, sizeof(resource_context));
    objects->live_process_count = 1;
    objects->live_thread_count = 1;
    objects->processes[6].slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE;
    objects->processes[6].generation = 1;
    objects->processes[6].primary_endpoint = UINT32_C(0x1006);
    objects->processes[6].privilege_profile = MICROS_PRIVILEGE_PROFILE_APPLICATION;
    objects->processes[6].address_space_root = UINT64_C(0x90000000);
    objects->threads[0].slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE;
    objects->threads[0].generation = 1;
    objects->threads[0].owner = (struct micros_process_handle){6, 1};
    objects->threads[0].context_attached = true;
    objects->threads[0].scheduler_assigned = true;
    objects->threads[0].ready_linked = true;
    baseline->kernel.ownership.managed_frame_count = 64;
    baseline->kernel.ownership.owners[1].kind = MICROS_FRAME_OWNER_KERNEL_RETAINED;
    baseline->kernel.allocator.allocated_bitmap[0] = 2;
    baseline->kernel.pm.last_transaction = 1;
    baseline->kernel.pm.last_prepared_token = 1;
    baseline->kernel.vm.last_load_token = 1;
    baseline->kernel.vm.last_mapping_generation = 1;
    baseline->kernel.table_count = 1;
    baseline->kernel.tables[0].physical_address = UINT64_C(0x90000000);
    baseline->kernel.tables[0].entries[0] = UINT64_C(0x20000001);
    init->state = MICROS_PM_PROCESS_RUNNING;
    init->generation = 1;
    init->pid = 1;
    init->reaper = true;
    init->endpoint = UINT32_C(0x1006);
    baseline->services.pm.last_pid = 1;
    baseline->services.coordinator.last_spawn_token = 1;
    baseline->services.vm.phase = MICROS_VM_LOADER_IDLE;
    baseline->services.vfs.processes[0].state = MICROS_VFS_PROCESS_ACTIVE;
    baseline->services.vfs.processes[0].endpoint = init->endpoint;
    baseline->services.vfs.open_files[0].state = MICROS_VFS_OPEN_FILE_ACTIVE;
    baseline->services.vfs.open_files[0].reference_count = 3;
    baseline->services.vfs.vnodes[0].state = MICROS_VFS_VNODE_ACTIVE;
    baseline->services.vfs.vnodes[0].local_reference_count = 2;
    baseline->services.ramfs.nodes[0].live = true;
    baseline->services.ramfs.nodes[0].reference_count = 1;
    baseline->services.tty.owned = true;
    resource_context.current = *baseline;
}

bool test_elf_spawn_kernel_resource_oracle(void)
{
    struct micros_elf_spawn_kernel_resources *current = &resource_context.current.kernel;
    const struct micros_elf_spawn_kernel_resources *baseline = &resource_context.baseline.kernel;

    reset_resources();
    CHECK(micros_elf_spawn_same_kernel_resources(baseline, current));
    current->objects.processes[6].generation++;
    CHECK(!micros_elf_spawn_same_kernel_resources(baseline, current));
    reset_resources();
    current->objects.processes[9].slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE;
    CHECK(!micros_elf_spawn_same_kernel_resources(baseline, current));
    reset_resources();
    current->objects.threads[0].ipc_reply_token = 17;
    CHECK(!micros_elf_spawn_same_kernel_resources(baseline, current));
    reset_resources();
    current->endpoints.endpoints[6].pending_kernel_events = 1;
    CHECK(!micros_elf_spawn_same_kernel_resources(baseline, current));
    reset_resources();
    current->endpoints.profiles[1].operations = MICROS_PRIVILEGE_OPERATION_SEND;
    CHECK(!micros_elf_spawn_same_kernel_resources(baseline, current));
    reset_resources();
    current->grants.grants[0].state = MICROS_GRANT_SLOT_ACTIVE;
    CHECK(!micros_elf_spawn_same_kernel_resources(baseline, current));
    reset_resources();
    current->ownership.owners[1].slot = 7;
    CHECK(!micros_elf_spawn_same_kernel_resources(baseline, current));
    reset_resources();
    current->allocator.allocated_bitmap[0] ^= 4;
    CHECK(!micros_elf_spawn_same_kernel_resources(baseline, current));
    reset_resources();
    current->pm.active_transaction = 8;
    CHECK(!micros_elf_spawn_same_kernel_resources(baseline, current));
    reset_resources();
    current->tables[0].entries[5] ^= 1;
    CHECK(!micros_elf_spawn_same_kernel_resources(baseline, current));
    return true;
}

bool test_elf_spawn_service_resource_oracle(void)
{
    struct micros_elf_spawn_service_resources *current = &resource_context.current.services;
    const struct micros_elf_spawn_service_resources *baseline = &resource_context.baseline.services;

    reset_resources();
    CHECK(micros_elf_spawn_same_service_resources(baseline, current));
    current->pm.records[1].state = MICROS_PM_PROCESS_ZOMBIE;
    CHECK(!micros_elf_spawn_same_service_resources(baseline, current));
    reset_resources();
    current->coordinator.spawn_pending = true;
    CHECK(!micros_elf_spawn_same_service_resources(baseline, current));
    reset_resources();
    current->vm.mapping_records[7].phase = MICROS_VM_MAPPING_RECORD_ACTIVE;
    CHECK(!micros_elf_spawn_same_service_resources(baseline, current));
    reset_resources();
    current->vfs.processes[0].descriptors[1] = 2;
    CHECK(!micros_elf_spawn_same_service_resources(baseline, current));
    reset_resources();
    current->vfs.open_files[0].reference_count++;
    CHECK(!micros_elf_spawn_same_service_resources(baseline, current));
    reset_resources();
    current->vfs.vnodes[0].backend_reference_count++;
    CHECK(!micros_elf_spawn_same_service_resources(baseline, current));
    reset_resources();
    current->vfs.backend_page_owner = MICROS_VFS_PAGE_SYNCHRONOUS;
    CHECK(!micros_elf_spawn_same_service_resources(baseline, current));
    reset_resources();
    current->ramfs.nodes[0].reference_count++;
    CHECK(!micros_elf_spawn_same_service_resources(baseline, current));
    reset_resources();
    current->tty.terminal.write_resident = true;
    CHECK(!micros_elf_spawn_same_service_resources(baseline, current));
    reset_resources();
    current->tty.pending_read.active = true;
    CHECK(!micros_elf_spawn_same_service_resources(baseline, current));
    return true;
}

bool test_elf_spawn_resource_counter_and_inactive_byte_policy(void)
{
    struct micros_elf_spawn_resources *current = &resource_context.current;
    const struct micros_elf_spawn_resources *baseline = &resource_context.baseline;

    reset_resources();
    current->kernel.objects.processes[9].generation++;
    current->kernel.objects.threads[9].generation++;
    current->kernel.grants.grants[0].generation++;
    current->kernel.endpoints.last_reply_token++;
    current->kernel.pm.last_transaction++;
    current->kernel.pm.last_prepared_token++;
    current->kernel.pm.last_exit_token++;
    current->kernel.vm.last_load_token++;
    current->kernel.vm.last_mapping_generation++;
    current->kernel.objects.threads[0].remaining_counter_ticks++;
    current->kernel.objects.threads[0].user_context.sepc = UINT64_C(0x40000080);
    current->services.pm.last_pid = 3;
    current->services.pm.records[1].generation++;
    current->services.coordinator.last_spawn_token = 3;
    current->services.coordinator.request_bytes[0] = 5;
    current->services.vfs.next_tty_request_id++;
    current->services.vfs.last_image_token++;
    current->services.vfs.last_descriptor_token++;
    current->services.vfs.client_page[0] = 9;
    current->services.vfs.backend_page[0] = 10;
    current->services.ramfs.scratch[0] = 11;
    current->services.tty.last_accepted_request_id++;
    current->services.tty.terminal.write[0] = 12;
    CHECK(micros_elf_spawn_same_kernel_resources(&baseline->kernel, &current->kernel));
    CHECK(micros_elf_spawn_same_service_resources(&baseline->services, &current->services));
    return true;
}
