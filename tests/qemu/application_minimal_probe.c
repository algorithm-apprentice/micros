#include "micros/application_runtime.h"

_Alignas(64) static volatile uint8_t aligned_bss;

int micros_application_main(int argc, char *argv[], char *envp[])
{
    (void)argc;
    (void)argv;
    (void)envp;
    return aligned_bss;
}
