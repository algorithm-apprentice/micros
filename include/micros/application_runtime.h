#ifndef MICROS_APPLICATION_RUNTIME_H
#define MICROS_APPLICATION_RUNTIME_H

#include <stdint.h>

#include "micros/endpoint.h"

extern micros_endpoint_t micros_application_pm_endpoint;
extern micros_endpoint_t micros_application_vfs_endpoint;
extern uint64_t micros_application_pid;
extern uint64_t micros_application_page_size;

int micros_application_main(int argc, char *argv[], char *envp[]);
_Noreturn void micros_application_exit(int32_t code);

#endif
