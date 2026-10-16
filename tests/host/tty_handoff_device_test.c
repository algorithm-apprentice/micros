#include "tty_handoff_test_internal.h"

struct tty_handoff_device_work {
    struct micros_tty_device_authority authority;
    enum micros_tty_device_leaf_class classification;
};

static bool tty_handoff_test_check_device_exclusions(
    struct tty_handoff_device_work *work,
    const struct micros_process_handle tty_process,
    const struct micros_process_handle foreign_process,
    const uint64_t tty_root,
    const uint32_t exact_permissions
)
{
    EXPECT_TRUE(
        micros_tty_device_leaf_classify(
            &work->authority,
            tty_process,
            tty_root,
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_PHYSICAL_BASE,
            exact_permissions | MICROS_SV39_PERMISSION_EXECUTE,
            &work->classification
        ) == MICROS_TTY_HANDOFF_OK
        && work->classification == MICROS_TTY_DEVICE_LEAF_INVALID
        && micros_tty_device_leaf_classify(
            &work->authority,
            tty_process,
            tty_root,
            MICROS_TTY_UART_VIRTUAL_BASE
                + MICROS_TTY_UART_MAPPED_LENGTH,
            MICROS_TTY_UART_PHYSICAL_BASE,
            exact_permissions,
            &work->classification
        ) == MICROS_TTY_HANDOFF_OK
        && work->classification == MICROS_TTY_DEVICE_LEAF_INVALID
        && micros_tty_device_leaf_classify(
            &work->authority,
            foreign_process,
            UINT64_C(0x80300000),
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_PHYSICAL_BASE,
            exact_permissions,
            &work->classification
        ) == MICROS_TTY_HANDOFF_OK
        && work->classification == MICROS_TTY_DEVICE_LEAF_INVALID
    );
    EXPECT_TRUE(
        micros_tty_device_leaf_classify(
            &work->authority,
            tty_process,
            tty_root,
            MICROS_TTY_UART_VIRTUAL_BASE,
            UINT64_C(0x80400000),
            exact_permissions,
            &work->classification
        ) == MICROS_TTY_HANDOFF_OK
        && work->classification == MICROS_TTY_DEVICE_LEAF_INVALID
        && micros_tty_device_leaf_classify(
            &work->authority,
            foreign_process,
            UINT64_C(0x80300000),
            MICROS_TTY_UART_VIRTUAL_BASE,
            UINT64_C(0x80400000),
            exact_permissions,
            &work->classification
        ) == MICROS_TTY_HANDOFF_OK
        && work->classification == MICROS_TTY_DEVICE_LEAF_MANAGED
    );
    work->authority.console_phase = MICROS_TTY_CONSOLE_MAP_REQUESTED;
    work->classification = MICROS_TTY_DEVICE_LEAF_INVALID;
    EXPECT_TRUE(
        micros_tty_device_leaf_classify(
            &work->authority,
            tty_process,
            tty_root,
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_PHYSICAL_BASE,
            exact_permissions,
            &work->classification
        ) == MICROS_TTY_HANDOFF_ERROR_STATE
        && work->classification == MICROS_TTY_DEVICE_LEAF_INVALID
    );
    return true;
}

bool tty_handoff_test_device_leaf_classification(void)
{
    const struct micros_process_handle tty_process = {3, 7};
    const struct micros_process_handle foreign_process = {2, 5};
    const uint64_t tty_root = UINT64_C(0x80200000);
    const uint32_t exact_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE
        | MICROS_SV39_PERMISSION_USER;
    struct tty_handoff_device_work work;

    work.authority = (struct micros_tty_device_authority) {
        .process = tty_process,
        .root_physical_address = tty_root,
        .console_phase = MICROS_TTY_CONSOLE_MAPPED,
    };

    EXPECT_TRUE(
        micros_tty_device_leaf_required(
            &work.authority,
            tty_process,
            tty_root
        )
        && !micros_tty_device_leaf_required(
            &work.authority,
            foreign_process,
            tty_root
        )
        && micros_tty_device_leaf_classify(
            &work.authority,
            tty_process,
            tty_root,
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_PHYSICAL_BASE,
            exact_permissions,
            &work.classification
        ) == MICROS_TTY_HANDOFF_OK
        && work.classification == MICROS_TTY_DEVICE_LEAF_EXACT
        && micros_tty_device_range_intersects(
            &work.authority,
            tty_process,
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_MAPPED_LENGTH
        )
        && micros_tty_device_range_intersects(
            &work.authority,
            tty_process,
            MICROS_TTY_UART_VIRTUAL_BASE - 1,
            2
        )
        && !micros_tty_device_range_intersects(
            &work.authority,
            tty_process,
            MICROS_TTY_UART_VIRTUAL_BASE
                - MICROS_TTY_UART_MAPPED_LENGTH,
            MICROS_TTY_UART_MAPPED_LENGTH
        )
        && !micros_tty_device_range_intersects(
            &work.authority,
            tty_process,
            MICROS_TTY_UART_VIRTUAL_BASE
                + MICROS_TTY_UART_MAPPED_LENGTH,
            1
        )
        && !micros_tty_device_range_intersects(
            &work.authority,
            foreign_process,
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_MAPPED_LENGTH
        )
    );
    return tty_handoff_test_check_device_exclusions(
        &work,
        tty_process,
        foreign_process,
        tty_root,
        exact_permissions
    );
}
