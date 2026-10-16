#include "tty_handoff_test_internal.h"

bool tty_handoff_test_device_leaf_classification(void)
{
    const struct micros_process_handle tty_process = {3, 7};
    const struct micros_process_handle foreign_process = {2, 5};
    const uint64_t tty_root = UINT64_C(0x80200000);
    const uint32_t exact_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE
        | MICROS_SV39_PERMISSION_USER;
    struct micros_tty_device_authority authority = {
        .process = tty_process,
        .root_physical_address = tty_root,
        .console_phase = MICROS_TTY_CONSOLE_MAPPED,
    };
    enum micros_tty_device_leaf_class classification;

    EXPECT_TRUE(
        micros_tty_device_leaf_required(
            &authority,
            tty_process,
            tty_root
        )
        && !micros_tty_device_leaf_required(
            &authority,
            foreign_process,
            tty_root
        )
        && micros_tty_device_leaf_classify(
            &authority,
            tty_process,
            tty_root,
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_PHYSICAL_BASE,
            exact_permissions,
            &classification
        ) == MICROS_TTY_HANDOFF_OK
        && classification == MICROS_TTY_DEVICE_LEAF_EXACT
        && micros_tty_device_range_intersects(
            &authority,
            tty_process,
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_MAPPED_LENGTH
        )
        && micros_tty_device_range_intersects(
            &authority,
            tty_process,
            MICROS_TTY_UART_VIRTUAL_BASE - 1,
            2
        )
        && !micros_tty_device_range_intersects(
            &authority,
            tty_process,
            MICROS_TTY_UART_VIRTUAL_BASE
                - MICROS_TTY_UART_MAPPED_LENGTH,
            MICROS_TTY_UART_MAPPED_LENGTH
        )
        && !micros_tty_device_range_intersects(
            &authority,
            tty_process,
            MICROS_TTY_UART_VIRTUAL_BASE
                + MICROS_TTY_UART_MAPPED_LENGTH,
            1
        )
        && !micros_tty_device_range_intersects(
            &authority,
            foreign_process,
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_MAPPED_LENGTH
        )
    );
    EXPECT_TRUE(
        micros_tty_device_leaf_classify(
            &authority,
            tty_process,
            tty_root,
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_PHYSICAL_BASE,
            exact_permissions | MICROS_SV39_PERMISSION_EXECUTE,
            &classification
        ) == MICROS_TTY_HANDOFF_OK
        && classification == MICROS_TTY_DEVICE_LEAF_INVALID
        && micros_tty_device_leaf_classify(
            &authority,
            tty_process,
            tty_root,
            MICROS_TTY_UART_VIRTUAL_BASE
                + MICROS_TTY_UART_MAPPED_LENGTH,
            MICROS_TTY_UART_PHYSICAL_BASE,
            exact_permissions,
            &classification
        ) == MICROS_TTY_HANDOFF_OK
        && classification == MICROS_TTY_DEVICE_LEAF_INVALID
        && micros_tty_device_leaf_classify(
            &authority,
            foreign_process,
            UINT64_C(0x80300000),
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_PHYSICAL_BASE,
            exact_permissions,
            &classification
        ) == MICROS_TTY_HANDOFF_OK
        && classification == MICROS_TTY_DEVICE_LEAF_INVALID
    );
    EXPECT_TRUE(
        micros_tty_device_leaf_classify(
            &authority,
            tty_process,
            tty_root,
            MICROS_TTY_UART_VIRTUAL_BASE,
            UINT64_C(0x80400000),
            exact_permissions,
            &classification
        ) == MICROS_TTY_HANDOFF_OK
        && classification == MICROS_TTY_DEVICE_LEAF_INVALID
        && micros_tty_device_leaf_classify(
            &authority,
            foreign_process,
            UINT64_C(0x80300000),
            MICROS_TTY_UART_VIRTUAL_BASE,
            UINT64_C(0x80400000),
            exact_permissions,
            &classification
        ) == MICROS_TTY_HANDOFF_OK
        && classification == MICROS_TTY_DEVICE_LEAF_MANAGED
    );
    authority.console_phase = MICROS_TTY_CONSOLE_MAP_REQUESTED;
    classification = MICROS_TTY_DEVICE_LEAF_INVALID;
    EXPECT_TRUE(
        micros_tty_device_leaf_classify(
            &authority,
            tty_process,
            tty_root,
            MICROS_TTY_UART_VIRTUAL_BASE,
            MICROS_TTY_UART_PHYSICAL_BASE,
            exact_permissions,
            &classification
        ) == MICROS_TTY_HANDOFF_ERROR_STATE
        && classification == MICROS_TTY_DEVICE_LEAF_INVALID
    );
    return true;
}
