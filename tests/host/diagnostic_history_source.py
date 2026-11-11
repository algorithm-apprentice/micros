"""Finite current observation, callback and fatal source/machine contracts."""

from pathlib import Path
import re
from tests.host.evidence_records import regular_source


SOURCE_PROJECTION = {'new_kernel_members': ('kernel/diagnostic/diagnostic_history_core.h',
                        'kernel/diagnostic/diagnostic_history_core.c',
                        'kernel/diagnostic/diagnostic_history.h',
                        'kernel/diagnostic/diagnostic_history.c',
                        'kernel/diagnostic/diagnostic_control_core.h',
                        'kernel/diagnostic/diagnostic_control_core.c',
                        'kernel/diagnostic/diagnostic_control.c',
                        'kernel/diagnostic/panic_history.c')}


def _project_exact_changes(source, changes, label):
    counts = tuple(source.count(added) for added, _ in changes)
    if not any(counts):
        return source
    if any(count != 1 for count in counts):
        raise AssertionError(f"PLAT01 {label} has an incomplete or repeated extension")
    for added, original in changes:
        source = source.replace(added, original, 1)
    return source


FATAL_SOURCE_CONTRACTS = {
    ("tests/qemu/diagnostic_history_kernel.c", "micros_diagnostic_test_observe_thr"): """
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
void micros_diagnostic_test_observe_thr(void)
{
    if (diagnostic_thr_writes != UINT64_MAX) {
        ++diagnostic_thr_writes;
    }
}
#endif
""",
    ("arch/riscv64/uart.c", "uart_panic_mode"): """
static bool uart_panic_mode(void)
{
    return uart_console_state.owner == MICROS_UART_CONSOLE_OWNER_PANIC;
}
""",
    ("arch/riscv64/uart.c", "uart_write"): """
void uart_write(const char *text)
{
    if (uart_panic_mode()) {
        uart_write_direct(text);
        return;
    }
    uart_write_record(text);
}
""",
    ("arch/riscv64/uart.c", "uart_write_hex64"): """
void uart_write_hex64(uint64_t value)
{
    if (uart_panic_mode()) {
        uart_write_hex64_direct(value);
        return;
    }
    uart_write_hex64_record(value);
}
""",
    ("kernel/diagnostic/diagnostic_history_core.c", "micros_diagnostic_history_valid"): """
bool micros_diagnostic_history_valid(const struct micros_diagnostic_history *history)
{
    return (
        history != NULL
        && history->next < MICROS_DIAGNOSTIC_RING_CAPACITY
        && history->size <= MICROS_DIAGNOSTIC_RING_CAPACITY
        && history->tail_position <= MICROS_DIAGNOSTIC_TAIL_PAYLOAD
        && (history->size == MICROS_DIAGNOSTIC_RING_CAPACITY || history->next == history->size)
        && history->tail_position == (
            history->size < MICROS_DIAGNOSTIC_TAIL_PAYLOAD
                ? history->size : MICROS_DIAGNOSTIC_TAIL_PAYLOAD
        )
        && history->tail[history->tail_position] == 0
        && history->tail[MICROS_DIAGNOSTIC_TAIL_CAPACITY - 1] == 0
    );
}
""",
    ("kernel/diagnostic/diagnostic_history.c", "micros_diagnostic_panic_size"): """
int32_t micros_diagnostic_panic_size(void)
{
    return micros_diagnostic_history_valid(&diagnostic_history)
        ? (int32_t)diagnostic_history.tail_position : -1;
}
""",
    ("kernel/diagnostic/diagnostic_history.c", "micros_diagnostic_panic_byte_prevalidated"): """
uint8_t micros_diagnostic_panic_byte_prevalidated(uint32_t index)
{
    return diagnostic_history.tail[index];
}
""",
    ("kernel/diagnostic/panic_history.c", "write_history_byte"): r"""
static void write_history_byte(uint8_t value)
{
    static const char digits[] = "0123456789abcdef";
    char pair[3];

    pair[0] = digits[value >> 4];
    pair[1] = digits[value & 15];
    pair[2] = '\0';
    uart_write(pair);
}
""",
    ("kernel/diagnostic/panic_history.c", "micros_diagnostic_panic_report"): r"""
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
""",
}


HISTORY_LAYOUT = """
struct micros_diagnostic_history {
    uint8_t ring[MICROS_DIAGNOSTIC_RING_CAPACITY];
    uint8_t tail[MICROS_DIAGNOSTIC_TAIL_CAPACITY];
    uint32_t next;
    uint32_t size;
    uint32_t tail_position;
};
"""


DIAGNOSTIC_CONSTANTS = (
    ("MICROS_DIAGNOSTIC_RING_CAPACITY", "10000"),
    ("MICROS_DIAGNOSTIC_TAIL_CAPACITY", "2000"),
    ("MICROS_DIAGNOSTIC_TAIL_PAYLOAD", "1998"),
    ("MICROS_DIAGNOSTIC_HEADER_SIZE", "16"),
    ("MICROS_DIAGNOSTIC_SNAPSHOT_SIZE", "10016"),
    ("MICROS_DIAGNOSTIC_VERSION", "1"),
)


THR_OBSERVATION = (
    "#ifdef MICROS_BUILD_TTY_SERVICE_TEST\n"
    "    micros_diagnostic_test_observe_thr();\n"
    "#endif\n"
)


THR_OBSERVATION_HEADER = (
    "#ifdef MICROS_BUILD_TTY_SERVICE_TEST\n"
    '#include "tests/qemu/diagnostic_history_kernel.h"\n'
    "#endif\n"
)


def checked_fatal_function(unit, name, source):
    from tests.host.c_source_records import function_records, tokens

    template = FATAL_SOURCE_CONTRACTS.get((unit, name))
    if template is None:
        raise AssertionError("PLAT01 has no new source/effect contract for this function")
    expected = function_records(template)[name]
    actual = function_records(source).get(name)
    if (
        actual is None or actual.guards != expected.guards
        or tokens(actual.signature) != tokens(expected.signature)
        or tokens(actual.body) != tokens(expected.body)
    ):
        raise AssertionError("PLAT01 fatal helper differs from its fixed operation contract")
    return actual


def diagnostic_header_source():
    return (
        "#ifndef MICROS_DIAGNOSTIC_H\n#define MICROS_DIAGNOSTIC_H\n\n"
        "#include <stdint.h>\n\n"
        + "".join("#define " + name + " UINT32_C(" + value + ")\n" for name, value in DIAGNOSTIC_CONSTANTS)
        + "#define MICROS_KERNEL_EVENT_DIAGNOSTIC UINT64_C(0x10)\n\n#endif\n"
    )


def effective_thr_body(body, enabled):
    store = "    uart0[UART_THR] = (uint8_t)character;\n"
    if type(enabled) is not bool or body.count(store + THR_OBSERVATION) != 1:
        raise AssertionError("PLAT01 THR observation is not immediately after its actual store")
    return body.replace(
        THR_OBSERVATION, "    micros_diagnostic_test_observe_thr();\n" if enabled else "", 1,
    )


def _remove_c_function(source, name):
    from tests.host.c_source_records import function_records

    record = function_records(source).get(name)
    if record is None or record.guards:
        raise AssertionError("PLAT01 source extension lacks an ordinary uniquely owned definition")
    fragment = record.signature + "{" + record.body + "}"
    if source.count(fragment) != 1:
        raise AssertionError("PLAT01 source function has an ambiguous complete extent")
    return source.replace(fragment, "", 1)


def project_fatal_uart(source):
    from tests.host.c_source_records import function_records

    records = function_records(source)
    for name in ("uart_panic_mode", "uart_write", "uart_write_hex64"):
        checked_fatal_function("arch/riscv64/uart.c", name, source)
    character = records["uart_write_character"]
    effective_thr_body(character.body, False)
    source = source.replace(THR_OBSERVATION, "", 1)
    for name in (
        "uart_panic_mode", "uart_write", "uart_write_hex64",
        "uart_write_record", "uart_write_hex64_record",
    ):
        source = _remove_c_function(source, name)
    if "uart_write_logical_byte" in records:
        source = _remove_c_function(source, "uart_write_logical_byte")
    for name in ("uart_write_direct", "uart_write_hex64_direct"):
        original = name.removesuffix("_direct")
        declaration = "static void " + name + "("
        if source.count(declaration) != 1:
            raise AssertionError("PLAT01 direct writer lacks its exact private-name transport")
        source = source.replace(declaration, "void " + original + "(", 1)
    include = '#include "kernel/diagnostic/diagnostic_history.h"\n'
    if source.count(include) != 1 or source.count(THR_OBSERVATION_HEADER) != 1:
        raise AssertionError("PLAT01 UART source includes do not match their fixed boundaries")
    source = source.replace(include, "", 1).replace(THR_OBSERVATION_HEADER, "", 1)
    quiesce = function_records(source)["uart_console_begin_quiesce_prevalidated"]
    handoff = "    micros_diagnostic_handoff();\n"
    if quiesce.body.count(handoff) != 1 or not quiesce.body.rstrip().endswith(handoff.strip()):
        raise AssertionError("PLAT01 handoff cursor is not captured after complete quiescence")
    return source.replace(handoff, "", 1)


def project_fatal_report(source):
    from tests.host.c_source_records import function_records

    include = '#include "kernel/diagnostic/diagnostic_history.h"\n'
    call = "    micros_diagnostic_panic_report();\n"
    record = function_records(source).get("micros_panic_report")
    if (
        record is None or source.count(include) != 1 or source.count(call) != 1
        or record.body.count(call + "    uart_flush();\n") != 1
        or record.body.index(call) > record.body.index("    uart_flush();\n")
    ):
        raise AssertionError("PLAT01 history is not after the core/trap records and before first flush")
    return source.replace(include, "", 1).replace(call, "", 1)


TTY_INITIALIZATION = """        || !micros_tty_diagnostic_initialize(
            &tty_diagnostic, micros_bootstrap_service_config.self_endpoint
        )
"""


TTY_STARTUP_PROBE = """#ifdef MICROS_BUILD_TTY_SERVICE_TEST
    uint64_t initial_events;

    tty_service_clear_bytes(&message, sizeof(message));
    if (
        micros_runtime_receive(MICROS_ENDPOINT_ANY, &message) != MICROS_SYSCALL_ABI_OK
        || !micros_tty_diagnostic_notification(&message, &initial_events)
        || initial_events != (MICROS_KERNEL_EVENT_TTY_IRQ | MICROS_KERNEL_EVENT_DIAGNOSTIC)
        || !handle_irq(&bus, &io)
    ) {
        __builtin_trap();
    }
    while (true) {
        enum micros_diagnostic_test_transfer_result transfer = micros_diagnostic_test_tty_transfer(&tty_diagnostic);

        if (transfer == MICROS_DIAGNOSTIC_TEST_TRANSFER_COMPLETE) {
            break;
        }
        tty_service_clear_bytes(&message, sizeof(message));
        if (
            transfer != MICROS_DIAGNOSTIC_TEST_TRANSFER_DEFERRED
            || micros_runtime_receive(MICROS_ENDPOINT_ANY, &message) != MICROS_SYSCALL_ABI_OK
            || !micros_tty_diagnostic_notification(&message, &initial_events)
            || initial_events != MICROS_KERNEL_EVENT_TTY_IRQ
            || !handle_irq(&bus, &io)
        ) {
            __builtin_trap();
        }
    }
#endif
"""


TTY_DIAGNOSTIC_DISPATCH = """                !micros_tty_diagnostic_notification(&message, &diagnostic_events)
                || (
                    (diagnostic_events & MICROS_KERNEL_EVENT_TTY_IRQ) != 0
                    && !handle_irq(&bus, &io)
                )
"""


TTY_DIAGNOSTIC_PROGRESS = """        if (
            !micros_tty_diagnostic_progress(
                &tty_diagnostic, &tty_service.terminal,
                (diagnostic_events & MICROS_KERNEL_EVENT_DIAGNOSTIC) != 0, &diagnostic_effects
            )
            || micros_tty_uart_apply_effects(
                &tty_uart, &bus, &tty_service.terminal, &diagnostic_effects
            ) != MICROS_TTY_UART_OK
        ) {
            __builtin_trap();
        }
"""


TTY_OWNERSHIP_ANCHOR = (
    "        || micros_tty_service_commit_ownership(&tty_service)\n"
    "            != MICROS_TTY_SERVICE_OK\n"
)


TTY_CALL_ANCHOR = """        } else if (!handle_call(&bus, &io, &message)) {
            __builtin_trap();
        }
"""


TTY_CONSTRUCTOR_CHANGES = (
    (
        "    struct micros_ipc_message message;\n"
        "    struct micros_tty_effects diagnostic_effects;\n",
        "    struct micros_ipc_message message;\n",
    ),
    (TTY_OWNERSHIP_ANCHOR + TTY_INITIALIZATION, TTY_OWNERSHIP_ANCHOR),
    ("    }\n\n" + TTY_STARTUP_PROBE + "\n    for (;;) {\n", "    }\n\n    for (;;) {\n"),
    ("    for (;;) {\n        uint64_t diagnostic_events = 0;\n", "    for (;;) {\n"),
    (
        TTY_DIAGNOSTIC_DISPATCH,
        "                !notification_is_tty_irq(&message)\n"
        "                || !handle_irq(&bus, &io)\n",
    ),
    (TTY_CALL_ANCHOR + TTY_DIAGNOSTIC_PROGRESS, TTY_CALL_ANCHOR),
)


def project_diagnostic_callback_source(unit, source):
    from tests.host.c_source_records import function_records

    if unit != "servers/tty/tty_service.c":
        return source
    record = function_records(source).get("micros_service_main")
    if record is None:
        raise AssertionError("TTY diagnostic integration has no actual service constructor")
    body = _project_exact_changes(record.body, TTY_CONSTRUCTOR_CHANGES, "TTY constructor")
    original = record.signature + "{" + record.body + "}"
    if source.count(original) != 1:
        raise AssertionError("TTY diagnostic constructor has an ambiguous extent")
    return source.replace(original, record.signature + "{" + body + "}", 1)


def diagnostic_kernel_members(root):
    from tests.host.c_source_records import function_records

    members = SOURCE_PROJECTION["new_kernel_members"]
    portable_headers = {
        "kernel/diagnostic/diagnostic_history_core.h",
        "kernel/diagnostic/diagnostic_control_core.h",
    }
    errors = []
    for relative in members:
        try:
            path = regular_source(Path(root), relative)
            if path.stat().st_mode & 0o777 != 0o644:
                raise ValueError("diagnostic source mode is not ordinary 0644")
            source = path.read_text(encoding="utf-8")
            if relative.endswith(".h") and function_records(source):
                raise ValueError("diagnostic private header contains an implementation")
            for name in re.findall(r'(?m)^\s*#\s*include\s+"([^"]+)"', source):
                if Path(name).is_absolute() or ".." in Path(name).parts or name.endswith((".c", ".S")):
                    raise ValueError("diagnostic source has an escaping or textual implementation include")
                selected = name if "/" in name else str(Path(relative).parent / name)
                if selected.startswith(("servers/", "user/", "tests/host/")):
                    raise ValueError("diagnostic kernel source imports another policy/evidence owner")
                if "_core." in relative and selected.startswith("kernel/") and selected not in portable_headers:
                    raise ValueError("portable diagnostic core gains privileged adapter authority")
        except (ValueError, AssertionError, OSError) as error:
            errors.append(f"{relative}: {error}")
    return members, tuple(errors)


RETRIGGER_READ_BOUNDARY = """    result = micros_diagnostic_prepare_read(
        &diagnostic_reader, identity, history, request->diagnostic_cursor, &span
    );
    if (result != MICROS_DIAGNOSTIC_CONTROL_OK) {
        return control_result(hart, frame, result);
    }
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
    if (!micros_diagnostic_test_before_read(request->diagnostic_cursor)) {
        return (uint64_t)(int64_t)MICROS_SYSCALL_ABI_STATE;
    }
    /* The admitted fixture appends only after the original request preflight. */
    if (micros_diagnostic_prepare_read(
        &diagnostic_reader, identity, history, request->diagnostic_cursor, &span
    ) != MICROS_DIAGNOSTIC_CONTROL_OK) {
        panic_control(hart, frame);
    }
#endif
    micros_diagnostic_copy_prevalidated(&diagnostic_reader, history, &span);
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
    micros_diagnostic_test_after_read();
#endif
"""


RETRIGGER_QUEUED_IRQ = """    if (state->handoff.route_phase == MICROS_TTY_ROUTE_IN_SERVICE) {
        const struct micros_endpoint_registry *registry = micros_ipc_runtime_registry();
        const struct micros_endpoint_record *endpoint;

        if (registry == NULL || state->process.slot != MICROS_TTY_PROCESS_SLOT) {
            MICROS_PANIC(0, "diagnostic-test-deferred-irq");
        }
        endpoint = &registry->endpoints[MICROS_TTY_PROCESS_SLOT];
        if (
            state->handoff.claimed_source != 10
            || endpoint->state != MICROS_ENDPOINT_STATE_ACTIVE || endpoint->value != state->endpoint
            || endpoint->owner.slot != state->process.slot
            || endpoint->owner.generation != state->process.generation
            || endpoint->pending_kernel_events != UINT64_C(0x8)
        ) {
            MICROS_PANIC(0, "diagnostic-test-deferred-irq");
        }
        return false;
    }
"""


RETRIGGER_FIRST_PROBE = """    if (
        diagnostic_fixture.phase != 1 || cursor != diagnostic_fixture.cursor
        || state->handoff.deadline_armed || state->handoff.deadline != 0
    ) {
        MICROS_PANIC(0, "diagnostic-test-reader");
    }
"""


RETRIGGER_IDLE = """    if (state->handoff.route_phase != MICROS_TTY_ROUTE_IDLE || state->handoff.claimed_source != 0) {
        MICROS_PANIC(0, "diagnostic-test-reader");
    }
    diagnostic_fixture.phase = 2;
"""


def validate_retrigger_read_adapter(source):
    from tests.host.c_source_records import function_records

    record = function_records(source).get("read_history")
    if record is None or record.guards or record.body.count(RETRIGGER_READ_BOUNDARY) != 1:
        raise AssertionError("PLAT01 deferral must follow full preflight and precede every test copy/after-copy")
    for name in (
        "micros_diagnostic_test_before_read", "micros_diagnostic_copy_prevalidated",
        "micros_diagnostic_test_after_read",
    ):
        if len(re.findall(r"\b" + name + r"\s*\(", record.body)) != 1:
            raise AssertionError("PLAT01 read adapter repeats an admission or copy site")


def validate_retrigger_kernel_fixture(source):
    from tests.host.c_source_records import function_records, tokens

    record = function_records(source).get("micros_diagnostic_test_before_read")
    if (
        record is None or record.guards != ("ifdef MICROS_BUILD_TTY_SERVICE_TEST",)
        or tokens(record.signature) != tokens("bool micros_diagnostic_test_before_read(uint32_t cursor)")
        or record.body.count(RETRIGGER_FIRST_PROBE + RETRIGGER_QUEUED_IRQ + RETRIGGER_IDLE) != 1
        or record.body.count("return false;") != 1
        or record.body.count("diagnostic_fixture.phase = 2;") != 1
    ):
        raise AssertionError("PLAT01 first-probe admission lost its exact readiness, cursor, queued IRQ or idle guard")
