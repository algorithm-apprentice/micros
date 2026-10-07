#ifndef MICROS_GRANT_RUNTIME_H
#define MICROS_GRANT_RUNTIME_H

#include "micros/grant.h"

enum micros_grant_error micros_grant_runtime_initialize(void);

const struct micros_grant_registry *micros_grant_runtime_registry(void);

enum micros_grant_error micros_grant_runtime_validate(void);

#endif
