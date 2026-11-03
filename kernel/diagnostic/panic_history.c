#include "kernel/diagnostic/diagnostic_history.h"

#include "arch/riscv64/platform.h"

static void write_history_byte(uint8_t value)
{
    static const char digits[] = "0123456789abcdef";
    char pair[3];

    pair[0] = digits[value >> 4];
    pair[1] = digits[value & 15];
    pair[2] = '\0';
    uart_write(pair);
}

void micros_diagnostic_panic_report(void)
{
    int32_t size = micros_diagnostic_panic_size();
    uint32_t offset = 0;

    if (size < 0) {
        uart_write("MICROS_DIAGNOSTIC_HISTORY state=unavailable bytes=0x0000000000000000\n");
        return;
    }
    uart_write("MICROS_DIAGNOSTIC_HISTORY state=retained bytes=");
    uart_write_hex64((uint64_t)(uint32_t)size);
    uart_write("\n");
    while (offset < (uint32_t)size) {
        uint32_t chunk = 0;

        uart_write("MICROS_DIAGNOSTIC_HISTORY_DATA offset=");
        uart_write_hex64(offset);
        uart_write(" data=");
        while (chunk < 32 && offset < (uint32_t)size) {
            uint8_t byte = micros_diagnostic_panic_byte_prevalidated(offset);

            write_history_byte(byte);
            ++offset;
            ++chunk;
        }
        uart_write("\n");
    }
}
