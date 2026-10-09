#ifndef MICROS_ARCH_RISCV64_MMU_H
#define MICROS_ARCH_RISCV64_MMU_H

#include <stdint.h>

#define MICROS_RISCV_SATP_MODE_SV39 (UINT64_C(8) << 60)

uint64_t micros_riscv_activate_sv39(uint64_t root_physical_address);
void micros_riscv_publish_page_table(void);

#endif
