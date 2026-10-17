#include "kernel/boot/bootstrap_manifest_internal.h"

struct role_profile_ids {
    uint8_t vm_profile_id;
    uint8_t pm_profile_id;
    uint8_t console_profile_id;
};

const struct micros_privilege_profile *bootstrap_manifest_find_profile(
    const struct micros_privilege_profile *profiles,
    size_t count,
    uint8_t profile_id
)
{
    size_t index;

    for (index = 0; index < count; ++index) {
        if (profiles[index].id == profile_id) {
            return &profiles[index];
        }
    }
    return NULL;
}

static bool profile_matches(
    const struct micros_privilege_profile *profile,
    uint8_t id,
    const char *name,
    uint32_t operations,
    uint32_t call_targets,
    uint32_t notify_targets,
    uint64_t kernel_operations
)
{
    return profile != NULL
        && profile->id == id
        && bootstrap_manifest_fixed_name_matches_literal(profile->name, name)
        && profile->operations == operations
        && profile->call_targets == call_targets
        && profile->send_targets == 0
        && profile->notify_targets == notify_targets
        && profile->kernel_operations == kernel_operations;
}

static bool core_service_profiles_match(
    const struct micros_privilege_profile *profiles,
    size_t profile_count
)
{
    const uint32_t launcher_target =
        UINT32_C(1)
        << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER;
    const uint32_t bootstrap_service_operations =
        MICROS_PRIVILEGE_OPERATION_RECEIVE
        | MICROS_PRIVILEGE_OPERATION_CALL;

    return profile_matches(
        bootstrap_manifest_find_profile(
            profiles,
            profile_count,
            MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
        ),
        MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER,
        "BOOTSTRAP_LAUNCHER",
        MICROS_PRIVILEGE_OPERATION_RECEIVE
            | MICROS_PRIVILEGE_OPERATION_REPLY,
        0,
        0,
        MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL
    )
        && profile_matches(
            bootstrap_manifest_find_profile(
                profiles,
                profile_count,
                MICROS_PRIVILEGE_PROFILE_VM
            ),
            MICROS_PRIVILEGE_PROFILE_VM,
            "VM",
            bootstrap_service_operations
                | MICROS_PRIVILEGE_OPERATION_REPLY,
            launcher_target,
            0,
            MICROS_KERNEL_OPERATION_VM_HANDOFF
        )
        && profile_matches(
            bootstrap_manifest_find_profile(
                profiles,
                profile_count,
                MICROS_PRIVILEGE_PROFILE_PM
            ),
            MICROS_PRIVILEGE_PROFILE_PM,
            "PM",
            bootstrap_service_operations
                | MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
            launcher_target
                | (
                    UINT32_C(1)
                    << MICROS_PRIVILEGE_PROFILE_VM
                ) | (
                    UINT32_C(1)
                    << MICROS_PRIVILEGE_PROFILE_VFS
                ),
            0,
            MICROS_KERNEL_OPERATION_PM_CONTROL
        );
}

static bool io_service_profiles_match(
    const struct micros_privilege_profile *profiles,
    size_t profile_count
)
{
    const uint32_t launcher_target =
        UINT32_C(1)
        << MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER;
    const uint32_t bootstrap_service_operations =
        MICROS_PRIVILEGE_OPERATION_RECEIVE
        | MICROS_PRIVILEGE_OPERATION_CALL;

    return profile_matches(
        bootstrap_manifest_find_profile(
            profiles,
            profile_count,
            MICROS_PRIVILEGE_PROFILE_TTY
        ),
        MICROS_PRIVILEGE_PROFILE_TTY,
        "TTY",
        bootstrap_service_operations
            | MICROS_PRIVILEGE_OPERATION_REPLY
            | MICROS_PRIVILEGE_OPERATION_NOTIFY,
        launcher_target,
        UINT32_C(1) << MICROS_PRIVILEGE_PROFILE_VFS,
        MICROS_KERNEL_OPERATION_TTY_CONTROL
    )
        && profile_matches(
            bootstrap_manifest_find_profile(
                profiles,
                profile_count,
                MICROS_PRIVILEGE_PROFILE_RAMFS
            ),
            MICROS_PRIVILEGE_PROFILE_RAMFS,
            "RAMFS",
            bootstrap_service_operations
                | MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
            launcher_target,
            0,
            0
        )
        && profile_matches(
            bootstrap_manifest_find_profile(
                profiles,
                profile_count,
                MICROS_PRIVILEGE_PROFILE_VFS
            ),
            MICROS_PRIVILEGE_PROFILE_VFS,
            "VFS",
            bootstrap_service_operations
                | MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
            launcher_target
                | (
                    UINT32_C(1)
                    << MICROS_PRIVILEGE_PROFILE_TTY
                ) | (
                    UINT32_C(1)
                    << MICROS_PRIVILEGE_PROFILE_RAMFS
                ),
            0,
            0
        );
}

static bool application_profile_matches(
    const struct micros_privilege_profile *profiles,
    size_t profile_count
)
{
    return profile_matches(
        bootstrap_manifest_find_profile(
            profiles,
            profile_count,
            MICROS_PRIVILEGE_PROFILE_APPLICATION
        ),
        MICROS_PRIVILEGE_PROFILE_APPLICATION,
        "APPLICATION",
        MICROS_PRIVILEGE_OPERATION_CALL,
        (
            UINT32_C(1)
            << MICROS_PRIVILEGE_PROFILE_PM
        ) | (
            UINT32_C(1)
            << MICROS_PRIVILEGE_PROFILE_VFS
        ),
        0,
        0
    );
}

static bool stable_service_profiles_match(
    const struct micros_privilege_profile *profiles,
    size_t profile_count
)
{
    return core_service_profiles_match(profiles, profile_count)
        && io_service_profiles_match(profiles, profile_count)
        && application_profile_matches(profiles, profile_count);
}

enum micros_bootstrap_error bootstrap_manifest_validate_profile_table(
    const struct micros_privilege_profile *profiles,
    size_t profile_count
)
{
    size_t index;

    if (
        profile_count == 0
        || profile_count > MICROS_PRIVILEGE_PROFILE_CAPACITY
    ) {
        return MICROS_BOOTSTRAP_ERROR_PROFILE;
    }
    for (index = 0; index < profile_count; ++index) {
        const struct micros_privilege_profile *profile =
            &profiles[index];
        size_t character_index;
        size_t other;

        if (
            profile->id == 0
            || profile->id >= MICROS_PRIVILEGE_PROFILE_CAPACITY
            || !bootstrap_manifest_fixed_name_is_canonical(profile->name)
            || profile->operations == 0
            || (
                profile->operations
                & ~MICROS_PRIVILEGE_OPERATION_DEFINED_MASK
            ) != 0
            || (
                profile->call_targets != 0
                && (
                    profile->operations
                    & MICROS_PRIVILEGE_OPERATION_CALL
                ) == 0
            )
            || (
                profile->send_targets != 0
                && (
                    profile->operations
                    & MICROS_PRIVILEGE_OPERATION_SEND
                ) == 0
            )
            || (
                profile->notify_targets != 0
                && (
                    profile->operations
                    & MICROS_PRIVILEGE_OPERATION_NOTIFY
                ) == 0
            )
        ) {
            return MICROS_BOOTSTRAP_ERROR_PROFILE;
        }
        for (
            character_index = 0;
            character_index < MICROS_PRIVILEGE_PROFILE_NAME_SIZE
                && profile->name[character_index] != '\0';
            ++character_index
        ) {
            unsigned char character =
                (unsigned char)profile->name[character_index];

            if (character < 0x20 || character > 0x7e) {
                return MICROS_BOOTSTRAP_ERROR_PROFILE;
            }
        }
        for (other = 0; other < index; ++other) {
            if (
                profiles[other].id == profile->id
                || bootstrap_manifest_fixed_names_equal(
                    profiles[other].name,
                    profile->name
                )
            ) {
                return MICROS_BOOTSTRAP_ERROR_PROFILE;
            }
        }
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_controller_profile(
    const struct micros_privilege_profile *profiles,
    size_t profile_count,
    uint8_t controller_profile_id
)
{
    const struct micros_privilege_profile *controller =
        bootstrap_manifest_find_profile(
            profiles,
            profile_count,
            controller_profile_id
        );

    if (controller == NULL) {
        return MICROS_BOOTSTRAP_ERROR_PROFILE;
    }
    if (
        controller->operations
            != (
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_REPLY
            )
        || controller->call_targets != 0
        || controller->send_targets != 0
        || controller->notify_targets != 0
        || controller->kernel_operations
            != MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL
    ) {
        return MICROS_BOOTSTRAP_ERROR_PROFILE;
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_profile_target_closure(
    const struct micros_privilege_profile *profiles,
    size_t profile_count,
    uint32_t *installed_profile_targets
)
{
    uint32_t targets = 0;
    size_t index;

    for (index = 0; index < profile_count; ++index) {
        targets |= UINT32_C(1) << profiles[index].id;
    }
    for (index = 0; index < profile_count; ++index) {
        if (
            (
                profiles[index].call_targets
                | profiles[index].send_targets
                | profiles[index].notify_targets
            ) & ~targets
        ) {
            return MICROS_BOOTSTRAP_ERROR_PROFILE;
        }
    }
    *installed_profile_targets = targets;
    return MICROS_BOOTSTRAP_OK;
}

static struct role_profile_ids find_role_profile_ids(
    const struct micros_bootstrap_manifest *manifest
)
{
    struct role_profile_ids roles = {0};
    size_t index;

    for (index = 0; index < manifest->header.entry_count; ++index) {
        if (
            (
                manifest->entries[index].role_flags
                & MICROS_BOOTSTRAP_ROLE_VM
            ) != 0
        ) {
            roles.vm_profile_id = manifest->entries[index].profile_id;
        }
        if (
            (
                manifest->entries[index].role_flags
                & MICROS_BOOTSTRAP_ROLE_PM
            ) != 0
        ) {
            roles.pm_profile_id = manifest->entries[index].profile_id;
        }
        if (
            (
                manifest->entries[index].role_flags
                & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
            ) != 0
        ) {
            roles.console_profile_id =
                manifest->entries[index].profile_id;
        }
    }
    return roles;
}

static enum micros_bootstrap_error validate_standard_profile_operations(
    const struct micros_privilege_profile *profiles,
    size_t profile_count,
    uint8_t controller_profile_id,
    const struct role_profile_ids *roles
)
{
    size_t index;

    if (
        roles->pm_profile_id != 0
        && (
            controller_profile_id
                != MICROS_PRIVILEGE_PROFILE_BOOTSTRAP_LAUNCHER
            || roles->vm_profile_id != MICROS_PRIVILEGE_PROFILE_VM
            || roles->pm_profile_id != MICROS_PRIVILEGE_PROFILE_PM
            || !stable_service_profiles_match(
                profiles,
                profile_count
            )
        )
    ) {
        return MICROS_BOOTSTRAP_ERROR_PROFILE;
    }
    for (index = 0; index < profile_count; ++index) {
        uint64_t expected_kernel_operations =
            profiles[index].id == controller_profile_id
            ? MICROS_KERNEL_OPERATION_BOOTSTRAP_CONTROL
            : (
                profiles[index].id == roles->vm_profile_id
                ? MICROS_KERNEL_OPERATION_VM_HANDOFF
                : (
                    profiles[index].id == roles->pm_profile_id
                    ? MICROS_KERNEL_OPERATION_PM_CONTROL
                    : (
                        roles->pm_profile_id != 0
                        && profiles[index].id
                            == MICROS_PRIVILEGE_PROFILE_TTY
                        ? MICROS_KERNEL_OPERATION_TTY_CONTROL
                        : 0
                    )
                )
            );

        if (
            profiles[index].kernel_operations
                != expected_kernel_operations
        ) {
            return MICROS_BOOTSTRAP_ERROR_PROFILE;
        }
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_console_profile(
    const struct micros_privilege_profile *profile,
    uint8_t console_profile_id,
    uint32_t controller_target
)
{
    if (
        console_profile_id != MICROS_PRIVILEGE_PROFILE_TTY
        || profile == NULL
        || profile->operations
            != (
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_CALL
                | MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_NOTIFY
            )
        || profile->call_targets != controller_target
        || profile->send_targets != 0
        || profile->notify_targets
            != (
                UINT32_C(1)
                << MICROS_PRIVILEGE_PROFILE_VFS
            )
        || profile->kernel_operations
            != MICROS_KERNEL_OPERATION_TTY_CONTROL
    ) {
        return MICROS_BOOTSTRAP_ERROR_PROFILE;
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_entry_role_profile_id(
    const struct micros_bootstrap_manifest_entry *entry
)
{
    if (
        entry->profile_id == MICROS_PRIVILEGE_PROFILE_APPLICATION
        || (
            (
                entry->role_flags
                & MICROS_BOOTSTRAP_ROLE_VM
            ) != 0
            && entry->profile_id != MICROS_PRIVILEGE_PROFILE_VM
        )
        || (
            (
                entry->role_flags
                & MICROS_BOOTSTRAP_ROLE_PM
            ) != 0
            && entry->profile_id != MICROS_PRIVILEGE_PROFILE_PM
        )
    ) {
        return MICROS_BOOTSTRAP_ERROR_PROFILE;
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_service_profile_operations(
    const struct micros_bootstrap_manifest_entry *entry,
    const struct micros_privilege_profile *profile,
    uint32_t controller_target,
    uint32_t installed_profile_targets
)
{
    if (
        profile == NULL
        || profile->operations
            != (
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_CALL
                | (
                    (
                        entry->role_flags
                        & MICROS_BOOTSTRAP_ROLE_VM
                    ) != 0
                    ? MICROS_PRIVILEGE_OPERATION_REPLY
                    : 0
                )
                | (
                    (
                        entry->role_flags
                        & MICROS_BOOTSTRAP_ROLE_PM
                    ) != 0
                    ? (
                        MICROS_PRIVILEGE_OPERATION_REPLY
                        | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE
                    )
                    : 0
                )
                | (
                    entry->profile_id
                        == MICROS_PRIVILEGE_PROFILE_RAMFS
                    ? (
                        MICROS_PRIVILEGE_OPERATION_REPLY
                        | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE
                    )
                    : 0
                )
                | (
                    entry->profile_id
                        == MICROS_PRIVILEGE_PROFILE_VFS
                    ? (
                        MICROS_PRIVILEGE_OPERATION_REPLY
                        | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE
                    )
                    : 0
                )
            )
        || (profile->call_targets & controller_target) == 0
        || (
            profile->call_targets & ~installed_profile_targets
        ) != 0
        || (
            profile->call_targets
            & (UINT32_C(1) << entry->profile_id)
        ) != 0
        || profile->send_targets != 0
        || profile->notify_targets != 0
    ) {
        return MICROS_BOOTSTRAP_ERROR_PROFILE;
    }
    return MICROS_BOOTSTRAP_OK;
}

static enum micros_bootstrap_error validate_entry_profile_relationships(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_privilege_profile *profiles,
    size_t profile_count,
    uint8_t controller_profile_id,
    uint32_t installed_profile_targets,
    uint8_t console_profile_id
)
{
    uint32_t controller_target = UINT32_C(1) << controller_profile_id;
    size_t index;

    for (index = 0; index < manifest->header.entry_count; ++index) {
        const struct micros_bootstrap_manifest_entry *entry =
            &manifest->entries[index];
        const struct micros_privilege_profile *profile;
        enum micros_bootstrap_error error;

        if (entry->profile_id == controller_profile_id) {
            if (
                (
                    entry->role_flags
                    & MICROS_BOOTSTRAP_ROLE_CONTROLLER
                ) == 0
            ) {
                return MICROS_BOOTSTRAP_ERROR_PROFILE;
            }
            continue;
        }
        if (
            (
                entry->role_flags
                & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
            ) != 0
        ) {
            profile = bootstrap_manifest_find_profile(
                profiles,
                profile_count,
                entry->profile_id
            );
            error = validate_console_profile(
                profile,
                console_profile_id,
                controller_target
            );
            if (error != MICROS_BOOTSTRAP_OK) {
                return error;
            }
            continue;
        }
        error = validate_entry_role_profile_id(entry);
        if (error != MICROS_BOOTSTRAP_OK) {
            return error;
        }
        profile = bootstrap_manifest_find_profile(
            profiles,
            profile_count,
            entry->profile_id
        );
        error = validate_service_profile_operations(
            entry,
            profile,
            controller_target,
            installed_profile_targets
        );
        if (error != MICROS_BOOTSTRAP_OK) {
            return error;
        }
    }
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error
bootstrap_manifest_validate_profile_relationships(
    const struct micros_bootstrap_manifest *manifest,
    const struct micros_privilege_profile *profiles,
    size_t profile_count,
    uint8_t controller_profile_id
)
{
    struct role_profile_ids roles;
    uint32_t installed_profile_targets;
    enum micros_bootstrap_error error;

    error = validate_controller_profile(
        profiles,
        profile_count,
        controller_profile_id
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    error = validate_profile_target_closure(
        profiles,
        profile_count,
        &installed_profile_targets
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    roles = find_role_profile_ids(manifest);
    error = validate_standard_profile_operations(
        profiles,
        profile_count,
        controller_profile_id,
        &roles
    );
    if (error != MICROS_BOOTSTRAP_OK) {
        return error;
    }
    return validate_entry_profile_relationships(
        manifest,
        profiles,
        profile_count,
        controller_profile_id,
        installed_profile_targets,
        roles.console_profile_id
    );
}
