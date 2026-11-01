"""Closed balanced CI ownership; local full validation remains unchanged."""

from tools import validation_plan
from tools.stack_bound_build import static_profiles
from tools.stack_bound_scope import KERNEL_ONLY, OTHER_IMAGES, SCENARIOS, expected_images


TARGET_PARTS = tuple(f"target-{index}" for index in range(1, 6))
PARTS = ("native", *TARGET_PARTS)


def target_partition(root):
    profiles = static_profiles(root)
    workflows = set(validation_plan.QEMU_WORKFLOWS) | set(validation_plan.IMAGE_WORKFLOWS)
    configurations = set(KERNEL_ONLY) | set(OTHER_IMAGES) | set(SCENARIOS)
    if (
        len(profiles) != len(workflows) or len(profiles) != len(configurations)
        or {row.workflow for row in profiles} != workflows
        or {row.configure for row in profiles} != configurations
    ):
        raise ValueError("CI target partition has missing, duplicate or unknown ownership")
    parts = {name: [] for name in TARGET_PARTS}
    totals = {name: 0 for name in TARGET_PARTS}

    def weight(profile):
        return len(expected_images(profile.configure)) * (2 if profile.configure in SCENARIOS else 1)

    for profile in sorted(profiles, key=lambda row: (-weight(row), row.configure)):
        selected = min(TARGET_PARTS, key=lambda name: (totals[name], name))
        parts[selected].append(profile)
        totals[selected] += weight(profile)
    if any(not rows for rows in parts.values()):
        raise ValueError("CI target partition has an empty producer")
    return {name: tuple(sorted(rows, key=lambda row: row.configure)) for name, rows in parts.items()}


def part_profiles(root, part):
    if part not in PARTS:
        raise ValueError("unknown CI part")
    return () if part == "native" else target_partition(root)[part]


def part_commands(root, part, base):
    if part not in PARTS:
        raise ValueError("unknown CI part")
    partition = target_partition(root)
    target_commands = {
        validation_plan.workflow(profile.workflow) for rows in partition.values() for profile in rows
    }
    full = validation_plan.plan([], "full", base)
    if not target_commands <= set(full) or full.count(validation_plan.STACK_EVIDENCE) != 1:
        raise ValueError("CI ownership differs from the canonical complete plan")
    if part == "native":
        return tuple(command for command in full
                     if command not in target_commands and command != validation_plan.STACK_EVIDENCE)
    return tuple(validation_plan.workflow(row.workflow) for row in partition[part])
