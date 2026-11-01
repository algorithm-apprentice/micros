"""Closed current QEMU/image build variants for stack-evidence artifacts."""

from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import subprocess

from tools.stack_bound_types import StackEvidenceError


IMAGE_WORKFLOWS = (
    "build-tty-service-image", "build-ramfs-service-image",
    "build-vfs-service-image", "build-application-images",
)
BASE_WORKFLOW = "build-riscv64-debug"
METADATA_MODULE = "tests/stack/StackMetadata.cmake"
OBSERVED_MODULE = "tests/stack/StackObserved.cmake"
OBSERVED_RECIPE = (
    'if(NOT CMAKE_CROSSCOMPILING OR NOT MICROS_STACK_BUILD_MODE STREQUAL "observed")\n'
    '    message(FATAL_ERROR "Observer support requires the explicit RV64 observed mode")\n'
    'endif()\n'
    'if(NOT MICROS_BUILD_SCHEDULER_TEST AND NOT MICROS_BUILD_IPC_SYSCALL_TEST AND NOT MICROS_BUILD_GRANT_SYSCALL_TEST AND NOT MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST AND NOT MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST AND NOT MICROS_BUILD_PM_SERVICE_TEST AND NOT MICROS_BUILD_TTY_SERVICE_TEST AND NOT MICROS_BUILD_RAMFS_SERVICE_TEST AND NOT MICROS_BUILD_VFS_SERVICE_TEST AND NOT MICROS_BUILD_ELF_SPAWN_TEST)\n'
    '    message(FATAL_ERROR "The current observer slice admits only its declared components")\n'
    'endif()\n'
    'add_compile_options("$<$<COMPILE_LANGUAGE:C>:-fstack-usage;-fstack-size-section;-finstrument-functions-after-inlining>")\n'
    'function(micros_stack_bind_observer)\n'
    '    if(NOT TARGET micros)\n'
    '        message(FATAL_ERROR "Observer support lacks its exact kernel target")\n'
    '    endif()\n'
    '    target_sources(micros PRIVATE tests/stack/observer.S tests/stack/observer.c)\n'
    '    target_compile_definitions(micros PRIVATE MICROS_STACK_OBSERVER_KERNEL=1)\n'
    '    set(stack_users)\n'
    '    if(MICROS_BUILD_BOOTSTRAP_LAUNCHER_TEST)\n'
    '        set(stack_users micros_bootstrap_launcher micros_bootstrap_probe)\n'
    '    elseif(MICROS_BUILD_VM_SCRATCH_PREEMPTION_TEST)\n'
    '        set(stack_users micros_bootstrap_launcher micros_vm_server micros_pm_service micros_vm_scratch_preemption_driver)\n'
    '    elseif(MICROS_BUILD_PM_SERVICE_TEST)\n'
    '        set(stack_users micros_bootstrap_launcher micros_vm_server micros_pm_service micros_pm_service_probe micros_application_runtime_probe)\n'
    '    elseif(MICROS_BUILD_TTY_SERVICE_TEST)\n'
    '        set(stack_users micros_bootstrap_launcher micros_vm_server micros_pm_service micros_tty_service micros_tty_service_vfs)\n'
    '    elseif(MICROS_BUILD_RAMFS_SERVICE_TEST)\n'
    '        set(stack_users micros_bootstrap_launcher micros_vm_server micros_pm_service micros_tty_service micros_ramfs_service micros_ramfs_service_vfs)\n'
    '    elseif(MICROS_BUILD_VFS_SERVICE_TEST)\n'
    '        set(stack_users micros_bootstrap_launcher micros_vm_server micros_pm_service micros_tty_service micros_ramfs_service micros_vfs_service micros_vfs_service_probe)\n'
    '    elseif(MICROS_BUILD_ELF_SPAWN_TEST)\n'
    '        set(stack_users micros_bootstrap_launcher micros_vm_server micros_pm_service micros_tty_service micros_ramfs_service micros_vfs_service micros_elf_spawn_init micros_elf_spawn_child micros_application_runtime_probe)\n'
    '    endif()\n'
    '    if(stack_users)\n'
    '        foreach(user_target IN LISTS stack_users)\n'
    '            target_sources("${user_target}" PRIVATE tests/stack/observer.S)\n'
    '            target_link_options("${user_target}" PRIVATE "-Wl,-Map,$<TARGET_FILE_DIR:${user_target}>/$<TARGET_FILE_BASE_NAME:${user_target}>.map")\n'
    '        endforeach()\n'
    '        set(stack_catalog "${CMAKE_BINARY_DIR}/generated/stack_observer_images.h")\n'
    '        add_custom_command(OUTPUT "${stack_catalog}"\n'
    '            COMMAND "${MICROS_PYTHON}" ${MICROS_PYTHON_ARGS}\n'
    '                "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_stack_observer_catalog.py"\n'
    '                --build "${CMAKE_BINARY_DIR}"\n'
    '            DEPENDS ${stack_users}\n'
    '                tools/generate_stack_observer_catalog.py tools/stack_bound_user_images.py\n'
    '            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" VERBATIM)\n'
    '        target_sources(micros PRIVATE tests/stack/observer_user.c "${stack_catalog}")\n'
    '        target_include_directories(micros PRIVATE "${CMAKE_BINARY_DIR}/generated")\n'
    '        target_compile_definitions(micros PRIVATE MICROS_STACK_OBSERVER_USERS=1)\n'
    '    endif()\n'
    '    if(MICROS_BUILD_PM_SERVICE_TEST OR MICROS_BUILD_ELF_SPAWN_TEST)\n'
    '        target_compile_definitions(micros PRIVATE MICROS_STACK_OBSERVER_APPLICATIONS=1)\n'
    '    endif()\n'
    '    if(MICROS_BUILD_TTY_SERVICE_TEST OR MICROS_BUILD_RAMFS_SERVICE_TEST OR MICROS_BUILD_VFS_SERVICE_TEST OR MICROS_BUILD_ELF_SPAWN_TEST)\n'
    '        target_compile_definitions(micros PRIVATE MICROS_STACK_OBSERVER_POLLED_REPORT=1)\n'
    '    endif()\n'
    'endfunction()\n'
    'cmake_language(DEFER CALL micros_stack_bind_observer)\n'
)
METADATA_RECIPE = (
    'if(NOT CMAKE_CROSSCOMPILING)\n'
    '    message(FATAL_ERROR "Stack metadata requires the RV64 target toolchain")\n'
    'endif()\n'
    'if(NOT MICROS_STACK_BUILD_MODE MATCHES "^(ordinary|metadata)$")\n'
    '    message(FATAL_ERROR "Stack artifacts require an explicit ordinary or metadata mode")\n'
    'endif()\n'
    'if(MICROS_STACK_BUILD_MODE STREQUAL "metadata")\n'
    '    add_compile_options("$<$<COMPILE_LANGUAGE:C>:-fstack-usage;-fstack-size-section>")\n'
    '    if(NOT MICROS_STACK_PAIR_RECORD OR NOT EXISTS "${MICROS_STACK_PAIR_RECORD}")\n'
    '        message(FATAL_ERROR "Stack metadata requires its sealed ordinary artifact pair")\n'
    '    endif()\n'
    'endif()\n'
    'function(micros_stack_bind_leaf_boundaries)\n'
    '    foreach(stack_leaf IN ITEMS micros_elf_spawn_init micros_elf_spawn_child micros_application_runtime_probe micros_application_minimal_probe)\n'
    '        if(TARGET "${stack_leaf}")\n'
    '            target_link_options("${stack_leaf}" PRIVATE\n'
    '                "-Wl,-Map,$<TARGET_FILE_DIR:${stack_leaf}>/$<TARGET_FILE_BASE_NAME:${stack_leaf}>.map")\n'
    '            if(MICROS_STACK_BUILD_MODE STREQUAL "metadata"\n'
    '               AND NOT stack_leaf STREQUAL "micros_application_minimal_probe")\n'
    '                add_custom_command(TARGET "${stack_leaf}" POST_BUILD\n'
    '                    COMMAND "${MICROS_PYTHON}" ${MICROS_PYTHON_ARGS}\n'
    '                        "${CMAKE_CURRENT_SOURCE_DIR}/tools/canonicalize_stack_metadata_elf.py"\n'
    '                        --pair "${MICROS_STACK_PAIR_RECORD}" --leaf "${stack_leaf}"\n'
    '                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" VERBATIM)\n'
    '            endif()\n'
    '        endif()\n'
    '    endforeach()\n'
    'endfunction()\n'
    'cmake_language(DEFER CALL micros_stack_bind_leaf_boundaries)\n'
)


@dataclass(frozen=True)
class Profile:
    configure: str
    workflow: str
    target: str
    variables: tuple[tuple[str, str], ...]


def selected(items, name):
    matches = [item for item in items if item.get("name") == name]
    if len(matches) != 1:
        raise StackEvidenceError("missing or duplicate stack preset")
    return matches[0]


def static_profiles(root):
    from tools.validation_plan import canonical_workflows

    root = Path(root)
    if (root / "CMakeUserPresets.json").exists():
        raise StackEvidenceError("user presets cannot supply stack authority")
    presets = json.loads((root / "CMakePresets.json").read_text())
    if "include" in presets:
        raise StackEvidenceError("external stack preset includes are unsupported")
    workflows = tuple(
        name for name in canonical_workflows()
        if name.startswith("test-qemu-") or name in IMAGE_WORKFLOWS
    )
    observed = {
        item["name"] for item in presets["workflowPresets"]
        if item["name"].startswith("test-qemu-") or item["name"].startswith("build-")
    }
    if observed != set(workflows) | {BASE_WORKFLOW} or not set(IMAGE_WORKFLOWS) <= observed:
        raise StackEvidenceError("current QEMU/image stack scope is incomplete")
    base_workflow = selected(presets["workflowPresets"], BASE_WORKFLOW)
    base_build = selected(presets["buildPresets"], "riscv64-debug")
    if (
        base_workflow.get("steps") != [
            {"type": "configure", "name": "riscv64-debug"},
            {"type": "build", "name": "riscv64-debug"},
        ]
        or set(base_build) != {"name", "configurePreset"}
        or base_build["configurePreset"] != "riscv64-debug"
    ):
        raise StackEvidenceError("option-free base build recipe is unreviewed")
    result = []
    configurations = {}
    for name in workflows:
        workflow = selected(presets["workflowPresets"], name)
        build = selected(presets["buildPresets"], name)
        configure = build.get("configurePreset")
        if (
            set(workflow) - {"name", "displayName", "description", "steps"}
            or set(build) - {"name", "displayName", "description", "configurePreset", "targets"}
            or type(configure) is not str
            or re.fullmatch(r"riscv64-[a-z0-9-]+", configure) is None
            or workflow.get("steps") != [
                {"type": "configure", "name": configure},
                {"type": "build", "name": name},
            ]
            or type(build.get("targets")) is not list
            or len(build["targets"]) != 1
        ):
            raise StackEvidenceError("stack workflow is outside its fixed recipe")
        config = selected(presets["configurePresets"], configure)
        parent = config.get("inherits")
        if parent is not None:
            if parent != "riscv64-debug":
                raise StackEvidenceError("unreviewed stack preset inheritance")
            base = selected(presets["configurePresets"], parent)
            config = {
                **base, **config,
                "cacheVariables": {
                    **base["cacheVariables"], **config.get("cacheVariables", {}),
                },
            }
        variables = config.get("cacheVariables", {})
        if (
            set(config) - {
                "name", "displayName", "description", "inherits", "generator",
                "binaryDir", "cacheVariables", "toolchainFile",
            }
            or config.get("generator") != "Ninja"
            or config.get("toolchainFile") != "${sourceDir}/cmake/toolchains/riscv64-clang.cmake"
            or config.get("binaryDir") != "${sourceDir}/build/" + configure
            or variables.get("CMAKE_BUILD_TYPE") != "Debug"
            or any(
                key != "CMAKE_BUILD_TYPE" and not (
                    key.startswith("MICROS_BUILD_") and value in ("ON", "OFF")
                    or key == "MICROS_SCHEDULER_INVALID_TEST_SCENARIO"
                    and value in ("outgoing", "selected")
                )
                for key, value in variables.items()
            )
        ):
            raise StackEvidenceError("stack configure contains an unreviewed build input")
        declared = tuple(sorted(variables.items()))
        if configure in configurations:
            if configurations[configure] != declared:
                raise StackEvidenceError("conflicting stack configure identity")
            continue
        configurations[configure] = declared
        result.append(Profile(configure, name, build["targets"][0], declared))
    return tuple(result)


def validate_metadata_recipe(root):
    path = Path(root) / METADATA_MODULE
    if path.is_symlink() or not path.is_file() or path.read_text() != METADATA_RECIPE:
        raise StackEvidenceError("stack metadata recipe differs from its declaration")


def validate_observed_recipe(root):
    path = Path(root) / OBSERVED_MODULE
    if path.is_symlink() or not path.is_file() or path.read_text() != OBSERVED_RECIPE:
        raise StackEvidenceError("observed stack recipe differs from its declaration")


def build_observed_profile(root, profile, python):
    from tools.validation_context import capture_candidate
    from tools.stack_bound_manifest import seal_build

    root = Path(root).resolve()
    if profile not in static_profiles(root) or profile.workflow not in (
        "test-qemu-scheduler", "test-qemu-ipc-syscall", "test-qemu-grant-syscall",
        "test-qemu-bootstrap-launcher", "test-qemu-vm-scratch-preemption",
        "test-qemu-pm-service", "test-qemu-tty", "test-qemu-ramfs", "test-qemu-vfs",
        "test-qemu-elf-spawn",
    ):
        raise StackEvidenceError("current observer slice has no implemented scope for this workflow")
    validate_observed_recipe(root)
    candidate = capture_candidate(root)
    if not candidate.clean:
        raise StackEvidenceError("observer build lacks its exact clean staged candidate")
    directory = root / "build/stack-evidence" / profile.configure / "observed"
    if any(path.is_symlink() for path in (directory, *directory.parents) if path != root.parent):
        raise StackEvidenceError("observer build has a linked artifact directory")
    subprocess.run((
        "cmake", "--fresh", "-S", str(root), "-B", str(directory), "-G", "Ninja",
        "-DCMAKE_TOOLCHAIN_FILE=" + str(root / "cmake/toolchains/riscv64-clang.cmake"),
        "-DMICROS_PYTHON=" + str(python), "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCMAKE_PROJECT_INCLUDE=" + str(root / OBSERVED_MODULE),
        "-DMICROS_STACK_BUILD_MODE=observed",
        *("-D" + key + "=" + value for key, value in profile.variables),
    ), cwd=root, check=True)
    subprocess.run(("cmake", "--build", str(directory), "--clean-first"), cwd=root, check=True)
    if capture_candidate(root) != candidate:
        raise StackEvidenceError("observer build source changed during execution")
    seal_build(root, directory, profile.configure, "observed", candidate)
    return directory


def build_profile(root, profile, mode, python):
    from tools.stack_artifact_build import pair_record_path, seal_ordinary_pair, tool_identities
    from tools.stack_artifact_files import read_regular
    from tools.validation_context import capture_candidate
    from tools.stack_bound_manifest import seal_build

    root = Path(root).resolve()
    if profile not in static_profiles(root) or mode not in ("ordinary", "metadata"):
        raise StackEvidenceError("unreviewed stack build profile or mode")
    validate_metadata_recipe(root)
    candidate = capture_candidate(root)
    if not candidate.clean:
        raise StackEvidenceError("stack build source is not the exact staged candidate")
    pair = pair_record_path(root, profile)
    if mode == "metadata":
        data, identity = read_regular(pair)
        record = json.loads(data)
        if (
            identity["mode"] != 0o600 or type(record) is not dict
            or record.get("mode") != "metadata" or record.get("profile") != profile.configure
            or record.get("candidate") != candidate.source
            or record.get("tools") != tool_identities(dict(os.environ))
        ):
            raise StackEvidenceError("metadata build lacks its exact sealed ordinary pair")
    directory = root / "build/stack-evidence" / profile.configure / mode
    if any(path.is_symlink() for path in (directory, *directory.parents) if path != root.parent):
        raise StackEvidenceError("linked stack build directories are unsupported")
    arguments = (
        "cmake", "--fresh", "-S", str(root), "-B", str(directory), "-G", "Ninja",
        "-DCMAKE_TOOLCHAIN_FILE=" + str(root / "cmake/toolchains/riscv64-clang.cmake"),
        "-DMICROS_PYTHON=" + str(python),
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCMAKE_PROJECT_INCLUDE=" + str(root / METADATA_MODULE),
        "-DMICROS_STACK_BUILD_MODE=" + mode,
        *(("-DMICROS_STACK_PAIR_RECORD=" + str(pair),) if mode == "metadata" else ()),
        *("-D" + key + "=" + value for key, value in profile.variables),
    )
    subprocess.run(arguments, cwd=root, check=True)
    subprocess.run(("cmake", "--build", str(directory), "--clean-first"), cwd=root, check=True)
    if profile.workflow in IMAGE_WORKFLOWS:
        subprocess.run(
            ("cmake", "--build", str(directory), "--target", profile.target),
            cwd=root, check=True,
        )
    if capture_candidate(root) != candidate:
        raise StackEvidenceError("stack build source changed during execution")
    if mode == "ordinary":
        seal_ordinary_pair(root, profile, candidate)
    seal_build(root, directory, profile.configure, mode, candidate)
    return directory
