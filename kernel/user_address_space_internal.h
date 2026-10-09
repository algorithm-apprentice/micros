#ifndef MICROS_KERNEL_USER_ADDRESS_SPACE_INTERNAL_H
#define MICROS_KERNEL_USER_ADDRESS_SPACE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "micros/user_address_space.h"

struct micros_user_address_space_tty_uart_plan {
    bool active;
    uint16_t leaf_index;
    uint8_t reserved[5];
    struct micros_process_handle process;
    uint64_t root_physical_address;
    uint64_t leaf_table_physical_address;
    uint64_t leaf_pte;
};

enum micros_user_address_space_error
micros_user_address_space_prepare_tty_uart_mapping(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address,
    struct micros_user_address_space_tty_uart_plan *plan
);

void micros_user_address_space_commit_tty_uart_mapping(
    struct micros_user_address_space_tty_uart_plan *plan
);

enum micros_user_address_space_error
micros_user_address_space_validate_tty_uart_mapping(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address
);

#endif
