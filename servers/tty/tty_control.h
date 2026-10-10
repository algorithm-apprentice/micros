#ifndef MICROS_SERVERS_TTY_CONTROL_H
#define MICROS_SERVERS_TTY_CONTROL_H

#include "micros/runtime.h"

micros_runtime_result_t micros_tty_control_commit(
    micros_endpoint_t self_endpoint
);

micros_runtime_result_t micros_tty_control_irq_complete(void);

#endif
