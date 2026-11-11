"""Current portable return qualification and passive target observer boundaries."""

from pathlib import Path
import re

from tests.host.object_hart_validation_evidence import NATIVE_HEADERS, NATIVE_SOURCES
from tests.host.c_source_records import function_records
from tests.host.work_count_lexical import function_spans


RECIPE_ADMISSION = {
    "native_targets": (
        "common_scheduler_return_current_ordinary", "common_scheduler_return_test",
    ),
    "generator": "tools/generate_common_scheduler_return_fixture.py",
    "driver": "tools/check_common_scheduler_return.py",
    "native_root": "build/host-debug/common-scheduler-return",
    "target_report_option": "--report-transcript",
}
OBSERVATION_PREFIX = '#include "tests/host/common_scheduler_return_test.h"\n'
PLAN_PATH = "kernel/scheduler/scheduler_core_return_plan.c"
TARGET_PATHS = (
    "kernel/scheduler/scheduler.c",
    "kernel/scheduler/scheduler_context.c",
    "kernel/scheduler/scheduler_return.c",
    "kernel/scheduler/scheduler_idle.c",
)
TARGET_INCLUDE = '#include "tests/qemu/common_scheduler_return_work.h"\n'


def _replace_once(text, before, after):
    if text.count(before) != 1:
        raise ValueError("OBJ02 transformation lost one exact boundary: " + before[:100])
    return text.replace(before, after, 1)


def _map_function(text, name, transform):
    records = function_spans(text)
    if name not in records:
        raise ValueError("OBJ02 transformation lost its function: " + name)
    span = records[name]
    return text[:span.body_start] + transform(text[span.body_start:span.body_end]) + text[span.body_end:]


def extend_target_observations(path, text):
    if path not in TARGET_PATHS or TARGET_INCLUDE in text or "MICROS_OBJ02_" in text:
        raise ValueError("OBJ02 target observer has an unknown or already annotated source")
    text = TARGET_INCLUDE + text
    if path.endswith(("scheduler_return.c", "scheduler_context.c")):
        text = _replace_once(text, "micros_user_execution_validate_context(", "MICROS_OBJ02_VALIDATE_CONTEXT(")
    if path.endswith("/scheduler.c"):
        def annotate_start(body):
            body = _replace_once(
                body, "    error = micros_hart_plan_user_return(",
                "    MICROS_OBJ02_RETURN_BEGIN(MICROS_OBJ02_RETURN_START, hart->current_thread);\n"
                "    error = micros_hart_plan_user_return(",
            )
            body = _replace_once(
                body, "        riscv_irq_restore(saved_status);\n        return error == MICROS_KERNEL_OBJECT_OK\n",
                "        MICROS_OBJ02_RETURN_CANCEL();\n"
                "        riscv_irq_restore(saved_status);\n        return error == MICROS_KERNEL_OBJECT_OK\n",
            )
            anchor = "    scheduler_error = scheduler_runtime_preflight_kernel_interval(hart, &accounting);"
            if body.count(anchor) != 1:
                raise ValueError("OBJ02 start observer lost its first accounting sample")
            before, after = body.split(anchor)
            guard = "        riscv_irq_restore(saved_status);\n        return scheduler_error;"
            if before.count(guard) != 2:
                raise ValueError("OBJ02 start observer lost a recoverable context failure")
            before = before.replace(guard, "        MICROS_OBJ02_RETURN_CANCEL();\n" + guard)
            return before + "    MICROS_OBJ02_RETURN_END(plan.selected);\n" + anchor + after
        text = _map_function(text, "micros_scheduler_start", annotate_start)
    elif path.endswith("/scheduler_return.c"):
        def annotate_return(body):
            body = _replace_once(
                body, "    scheduler_error = capture_outgoing_custody(",
                "    MICROS_OBJ02_RETURN_BEGIN(\n"
                "        store_outgoing ? MICROS_OBJ02_RETURN_USER : MICROS_OBJ02_RETURN_CAPTURED,\n"
                "        hart->current_thread\n    );\n    scheduler_error = capture_outgoing_custody(",
            )
            body = _replace_once(
                body, "        return scheduler_error;\n    }\n    micros_diagnostic_notify_pending",
                "        MICROS_OBJ02_RETURN_CANCEL();\n        return scheduler_error;\n"
                "    }\n    micros_diagnostic_notify_pending",
            )
            body = _replace_once(
                body, "        return scheduler_runtime_map_object_error(error);",
                "        MICROS_OBJ02_RETURN_CANCEL();\n        return scheduler_runtime_map_object_error(error);",
            )
            for indent in ("        ", "    "):
                anchor = "\n" + indent + "scheduler_error = prepare_timer_accounting(runtime, hart, &accounting);"
                body = _replace_once(body, anchor, "\n" + indent + "MICROS_OBJ02_RETURN_END(plan.selected);" + anchor)
            return body
        text = _map_function(text, "select_user_return", annotate_return)
    elif path.endswith("/scheduler_idle.c"):
        def annotate_idle(body):
            body = _replace_once(
                body, "    micros_diagnostic_notify_pending(hart, NULL);",
                "    MICROS_OBJ02_RETURN_BEGIN(MICROS_OBJ02_RETURN_IDLE, hart->current_thread);\n"
                "    micros_diagnostic_notify_pending(hart, NULL);",
            )
            for indent in ("        ", "    "):
                anchor = "\n" + indent + "prepare_idle_timer_accounting(runtime, hart, &accounting);"
                body = _replace_once(body, anchor, "\n" + indent + "MICROS_OBJ02_RETURN_END(plan.selected);" + anchor)
            return body
        text = _map_function(text, "micros_scheduler_idle_select", annotate_idle)
    return text


def project_target_observations(path, text):
    if path not in TARGET_PATHS or text.count(TARGET_INCLUDE) != 1 or not text.startswith(TARGET_INCLUDE):
        raise ValueError("OBJ02 target source lacks its exact passive include")
    original = text.removeprefix(TARGET_INCLUDE)
    original = original.replace("MICROS_OBJ02_VALIDATE_CONTEXT(", "micros_user_execution_validate_context(")
    original = re.sub(r"(?m)^[ \t]*MICROS_OBJ02_RETURN_(?:BEGIN|END|CANCEL)\([^;]*\);\n", "", original)
    if "MICROS_OBJ02_" in original or extend_target_observations(path, original) != text:
        raise ValueError("OBJ02 target source moved, omitted or changed a passive hook")
    return original


def validate_current_native_source(path, text, *, root=None):
    from tests.host.c_source_records import c_lexical_text, file_scope_objects
    from tests.host.native_observations import observe_source as observe_current

    if path not in (*NATIVE_SOURCES, *NATIVE_HEADERS) or type(text) is not str:
        raise ValueError("current return observation has an unknown source input")
    if file_scope_objects(text):
        raise ValueError("current portable return input introduced file-scope state")
    records = function_records(text)
    if path in NATIVE_HEADERS:
        if records:
            raise ValueError("current return header introduced unobserved function bodies")
        return
    for record in records.values():
        if re.search(r"\b(?:static|_Thread_local)\b", c_lexical_text(record.body)):
            raise ValueError("current portable return function introduced hidden persistent state")
    observe_current(
        Path(__file__).resolve().parents[2] if root is None else Path(root),
        "OBJ02", path, text, "ordinary",
    )


def workspace_accessor(stage):
    if stage != "current":
        raise ValueError("scheduler workspace observation requires current source")
    return "\nsize_t micros_obj02_return_workspace_size(void)\n{\n    return 0;\n}\n"


def native_products():
    return (("current", "ordinary"), ("current", "work"))
