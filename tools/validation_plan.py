#!/usr/bin/env python3

import argparse
import json
import os
import re
import shlex
import subprocess


UNIT_FAST = ("cmake", "--workflow", "--preset", "test-unit-fast")
UNIT_FULL = ("cmake", "--workflow", "--preset", "test-unit")
IPC_MODEL = ("cmake", "--workflow", "--preset", "test-ipc-model")
DOCS = ("python3", "tools/check_docs.py")
SMOKE = ("cmake", "--workflow", "--preset", "test-qemu-smoke")

QEMU_WORKFLOWS = (
    "test-qemu-smoke",
    "test-qemu-panic",
    "test-qemu-trap",
    "test-qemu-timer",
    "test-qemu-frame-allocator",
    "test-qemu-trap-panic",
    "test-qemu-mmu",
    "test-qemu-object-model",
    "test-qemu-endpoint",
    "test-qemu-ipc",
    "test-qemu-nested-trap",
    "test-qemu-frame-ownership",
    "test-qemu-user-address-space",
    "test-qemu-user-execution",
    "test-qemu-scheduler",
    "test-qemu-scheduler-invalid-outgoing",
    "test-qemu-scheduler-invalid-next",
)

PLANNER_INPUTS = (
    "tools/validation_plan.py",
    "tests/host/test_validation_plan.py",
    "docs/adr/0032-fail-closed-change-aware-validation.md",
)

SHARED_QEMU_PATHS = (
    "CMakeLists.txt",
    "CMakePresets.json",
    "cmake/",
    "arch/riscv64/kernel.ld",
    "kernel/main.c",
    "kernel/kernel_object_runtime.c",
    "include/micros/",
    "kernel/bootstrap_memory.c",
    "kernel/frame_ownership.c",
    "kernel/trap.c",
    "arch/riscv64/trap.S",
    "arch/riscv64/trap_context.h",
    "kernel/scheduler.c",
    "kernel/scheduler_core.c",
    "include/micros/scheduler_core",
    "kernel/timer.c",
    "kernel/user_execution.c",
    "arch/riscv64/boot.S",
    "tools/check_elf_sections.py",
    "tools/run_qemu_smoke.py",
    "tests/host/test_run_qemu_smoke.py",
)

GATE_INPUTS = {
    "test-qemu-smoke": (
        "kernel/fdt.c",
        "arch/riscv64/uart.c",
        "arch/riscv64/sbi.c",
    ),
    "test-qemu-panic": ("kernel/panic.c", "arch/riscv64/panic.S"),
    "test-qemu-trap": ("arch/riscv64/trap_test.S",),
    "test-qemu-timer": ("arch/riscv64/sbi.c",),
    "test-qemu-frame-allocator": (
        "kernel/frame_allocator.c",
        "kernel/frame_allocator_test.c",
    ),
    "test-qemu-trap-panic": (
        "kernel/panic.c",
        "arch/riscv64/trap_test.S",
    ),
    "test-qemu-mmu": (
        "kernel/sv39.c",
        "kernel/mmu_test.c",
        "arch/riscv64/mmu.S",
        "arch/riscv64/mmu_test.S",
    ),
    "test-qemu-object-model": (
        "kernel/object_model_test.c",
        "kernel/kernel_objects.c",
    ),
    "test-qemu-endpoint": (
        "kernel/endpoint.c",
        "kernel/kernel_objects.c",
        "kernel/endpoint_test.c",
    ),
    "test-qemu-ipc": (
        "kernel/ipc.c",
        "kernel/endpoint.c",
        "kernel/kernel_objects.c",
        "kernel/ipc_test.c",
    ),
    "test-qemu-nested-trap": ("arch/riscv64/nested_trap_test.S",),
    "test-qemu-frame-ownership": (
        "kernel/frame_ownership_test.c",
        "kernel/frame_allocator.c",
        "kernel/kernel_objects.c",
    ),
    "test-qemu-user-address-space": (
        "kernel/user_address_space.c",
        "kernel/address_space.c",
        "kernel/user_address_space_test.c",
        "arch/riscv64/user_address_space_test.S",
    ),
    "test-qemu-user-execution": (
        "kernel/user_execution_test.c",
        "arch/riscv64/user_execution_test.S",
    ),
    "test-qemu-scheduler": (
        "kernel/scheduler_test.c",
        "kernel/kernel_objects.c",
        "arch/riscv64/scheduler_test.S",
    ),
    "test-qemu-scheduler-invalid-outgoing": (
        "kernel/scheduler_invalid_test.c",
        "arch/riscv64/scheduler_test.S",
    ),
    "test-qemu-scheduler-invalid-next": (
        "kernel/scheduler_invalid_test.c",
        "arch/riscv64/scheduler_test.S",
    ),
}

SLOW_MODEL_INPUTS = {
    "test-ipc-model": (
        "CMakeLists.txt",
        "CMakePresets.json",
        "kernel/endpoint.c",
        "kernel/ipc.c",
        "kernel/kernel_objects.c",
        "kernel/scheduler_core.c",
        "include/micros/",
        "tests/host/endpoint_model_test.c",
        "tests/host/ipc_model_test.c",
        "tests/host/ipc_model_main.c",
        "tests/host/ipc_notify_test.c",
        "tests/host/ipc_close_test.c",
    ),
}

SLOW_MODEL_CTESTS = {
    "test-ipc-model": ("ipc-model", "ipc_model_test"),
}


def workflow(name):
    return ("cmake", "--workflow", "--preset", name)


def append_unique(commands, command):
    if command not in commands:
        commands.append(command)


def is_document(path):
    return path.endswith(".md") or path.startswith(".github/instructions/")


def normalize_path(path, root=None):
    if root is not None and os.path.isabs(path):
        path = os.path.relpath(path, root)
    while path.startswith("./"):
        path = path[2:]
    return path


def planner_root():
    return os.path.realpath(
        os.path.join(os.path.dirname(__file__), os.pardir)
    )


def normalize_explicit_path(path, root, caller_directory):
    candidate = (
        path
        if os.path.isabs(path)
        else os.path.join(caller_directory, path)
    )
    candidate = os.path.realpath(candidate)
    if os.path.commonpath((root, candidate)) != root:
        raise ValueError(f"path is outside planner repository: {path}")
    return os.path.relpath(candidate, root)


def path_matches(path, patterns):
    return any(
        path.startswith(pattern) if pattern.endswith("/") else path == pattern
        for pattern in patterns
    )


def target_path(path):
    return path.startswith(("kernel/", "arch/", "include/"))


def affected_workflows(paths):
    if any(path_matches(path, SHARED_QEMU_PATHS) for path in paths):
        return list(QEMU_WORKFLOWS)

    workflows = []
    unmatched_target = False
    for path in paths:
        matched = [
            name
            for name in QEMU_WORKFLOWS
            if path_matches(path, GATE_INPUTS.get(name, ()))
        ]
        if target_path(path) and not matched:
            unmatched_target = True
        for name in matched:
            if name not in workflows:
                workflows.append(name)
    if unmatched_target:
        return list(QEMU_WORKFLOWS)
    return workflows


def affected_slow_models(paths):
    return [
        name
        for name, inputs in SLOW_MODEL_INPUTS.items()
        if any(path_matches(path, inputs) for path in paths)
    ]


def requires_full_tier(paths):
    if any(path_matches(path, SHARED_QEMU_PATHS) for path in paths):
        return True
    for path in paths:
        if is_document(path):
            continue
        known = (
            any(
                path_matches(path, GATE_INPUTS.get(name, ()))
                for name in QEMU_WORKFLOWS
            )
            or bool(affected_slow_models([path]))
            or path_matches(path, PLANNER_INPUTS)
        )
        if not known:
            return True
    return False


def diff_commands(base):
    return (
        ("git", "diff", "--cached", "--check"),
        ("git", "diff", "--check"),
        ("git", "diff", "--check", f"{base}...HEAD"),
    )


def plan(paths, tier, base="origin/main"):
    paths = sorted({normalize_path(path) for path in paths})
    if tier == "full":
        commands = [UNIT_FULL]
        commands.extend(workflow(name) for name in QEMU_WORKFLOWS)
        commands.append(DOCS)
        commands.extend(diff_commands(base))
        return commands

    commands = []
    code_paths = [path for path in paths if not is_document(path)]
    if code_paths:
        append_unique(commands, UNIT_FAST)
    if any(is_document(path) for path in paths):
        append_unique(commands, DOCS)
    if not code_paths:
        commands.extend(diff_commands(base))
        return commands

    if requires_full_tier(code_paths):
        return plan(paths, "full", base)

    workflows = affected_workflows(code_paths)
    if tier == "fast":
        for name in affected_slow_models(code_paths):
            append_unique(commands, workflow(name))
        for name in workflows:
            append_unique(commands, workflow(name))
    else:
        commands[0] = UNIT_FULL
        append_unique(commands, SMOKE)
        for name in workflows:
            append_unique(commands, workflow(name))
    commands.extend(diff_commands(base))
    return commands


def changed_paths(base, root):
    commands = (
        ("git", "diff", "--no-renames", "--name-only", f"{base}...HEAD"),
        ("git", "diff", "--no-renames", "--name-only"),
        ("git", "diff", "--cached", "--no-renames", "--name-only"),
        ("git", "ls-files", "--others", "--exclude-standard"),
    )
    paths = set()
    for command in commands:
        completed = subprocess.run(
            command,
            check=True,
            stdout=subprocess.PIPE,
            text=True,
            cwd=root,
        )
        paths.update(line for line in completed.stdout.splitlines() if line)
    return sorted(paths)


def untracked_paths(root):
    completed = subprocess.run(
        ("git", "ls-files", "--others", "--exclude-standard"),
        check=True,
        stdout=subprocess.PIPE,
        text=True,
        cwd=root,
    )
    return sorted(
        line for line in completed.stdout.splitlines() if line
    )


def combine_paths(discovered, explicit):
    return sorted(set(discovered) | set(explicit))


def ensure_execution_ready(root):
    remaining = untracked_paths(root)
    if remaining:
        raise ValueError(
            "stage untracked files before execution: "
            + ", ".join(remaining)
        )


def preset_workflows(root):
    with open(
        os.path.join(root, "CMakePresets.json"),
        encoding="utf-8",
    ) as source:
        presets = json.load(source)
    return {
        preset["name"]
        for preset in presets["workflowPresets"]
    }


def documented_qemu_workflows(root):
    with open(
        os.path.join(root, "docs/testing-strategy.md"),
        encoding="utf-8",
    ) as source:
        content = source.read()
    return set(
        re.findall(
            r"cmake --workflow --preset (test-qemu-[a-z0-9-]+)",
            content,
        )
    )


def documented_slow_workflows(root):
    with open(
        os.path.join(root, "docs/testing-strategy.md"),
        encoding="utf-8",
    ) as source:
        content = source.read()
    workflows = set(
        re.findall(
            r"cmake --workflow --preset (test-[a-z0-9-]*model)",
            content,
        )
    )
    return {
        name for name in workflows if not name.startswith("test-qemu-")
    }


def custom_target_block(cmake, target):
    marker = f"add_custom_target(\n        {target}\n"
    start = cmake.find(marker)
    if start < 0:
        return ""
    next_target = cmake.find("add_custom_target(", start + len(marker))
    return cmake[start:] if next_target < 0 else cmake[start:next_target]


def validate_inventory(root):
    workflows = preset_workflows(root)
    qemu_inventory = {
        name for name in workflows if name.startswith("test-qemu-")
    }
    if qemu_inventory != set(QEMU_WORKFLOWS):
        raise ValueError("QEMU workflow inventory is out of sync")
    if qemu_inventory != documented_qemu_workflows(root):
        raise ValueError("documented QEMU matrix is out of sync")
    slow_inventory = {
        name
        for name in workflows
        if name.startswith("test-")
        and name.endswith("-model")
        and not name.startswith("test-qemu-")
    }
    if slow_inventory != set(SLOW_MODEL_INPUTS):
        raise ValueError("slow-model workflow inventory is out of sync")
    if slow_inventory != documented_slow_workflows(root):
        raise ValueError("documented slow-model matrix is out of sync")
    if slow_inventory != set(SLOW_MODEL_CTESTS):
        raise ValueError("slow-model CTest map is out of sync")
    if set(GATE_INPUTS) != qemu_inventory:
        raise ValueError("QEMU ownership map is out of sync")

    with open(
        os.path.join(root, "CMakeLists.txt"),
        encoding="utf-8",
    ) as source:
        cmake = source.read()
    fast_block = custom_target_block(cmake, "test-unit-fast")
    full_block = custom_target_block(cmake, "test-unit")
    for workflow_name, (ctest_name, executable) in SLOW_MODEL_CTESTS.items():
        required = (
            f"add_test(NAME {ctest_name} COMMAND {executable})",
            f"set_tests_properties({ctest_name} PROPERTIES LABELS slow)",
        )
        if any(fragment not in cmake for fragment in required):
            raise ValueError("slow-model CTest wiring is out of sync")
        model_block = custom_target_block(cmake, workflow_name)
        if (
            executable in fast_block
            or executable not in model_block
            or executable not in full_block
        ):
            raise ValueError("slow-model tier membership is out of sync")


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--tier", choices=("fast", "pr", "full"), default="fast")
    parser.add_argument("--base", default="origin/main")
    parser.add_argument("--path", action="append", default=[])
    parser.add_argument("--execute", action="store_true")
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    root = planner_root()
    try:
        validate_inventory(root)
        discovered = changed_paths(arguments.base, root)
        explicit = [
            normalize_explicit_path(path, root, os.getcwd())
            for path in arguments.path
        ]
        paths = combine_paths(discovered, explicit)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    commands = plan(paths, arguments.tier, arguments.base)

    if arguments.execute:
        try:
            ensure_execution_ready(root)
        except ValueError as error:
            raise SystemExit(str(error)) from error
    for command in commands:
        print(shlex.join(command), flush=True)
        if arguments.execute:
            subprocess.run(command, check=True, cwd=root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
