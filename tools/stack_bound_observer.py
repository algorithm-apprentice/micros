"""Independently declared observer artifact and its zero-stack machine ABI."""

from dataclasses import dataclass
from functools import lru_cache
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

from tools.stack_artifact_files import read_regular
from tools.stack_artifact_build import ninja_path
from tools.stack_bound_bindings import reference_source
from tools.stack_bound_assembly import allocated_signature
from tools.stack_bound_flow import function_flow, reachable_instructions
from tools.stack_bound_machine import CALLER_SAVED, is_return, require_c_frame_movement
from tools.stack_bound_objects import function_symbols, instructions, load_object, section_bytes
from tools.stack_bound_types import Frame, FunctionKey, StackEvidenceError
from tools.stack_bound_values import register_inputs
from tools.user_elf_instructions import _instruction_destination
from tools.validation_context import target_compiler
from tools.validation_inputs import _file_identity, _run


OBSERVER_SOURCE = """\
.option push
.option norelax
.option norvc
.section .text.__cyg_profile_func_enter, "ax", @progbits
.balign 4
.globl __cyg_profile_func_enter
.type __cyg_profile_func_enter, @function
__cyg_profile_func_enter:
    andi t0, sp, 15
    bnez t0, .Lbad
#ifdef MICROS_STACK_OBSERVER_KERNEL
    la t0, __boot_stack_bottom
    bltu sp, t0, .Lprimary
    la t1, __boot_stack_top
    bgeu sp, t1, .Lprimary
    li t2, 0
    li t4, 0
    j .Lsample
.Lprimary:
    la t0, __trap_stack_bottom
    bltu sp, t0, .Lemergency
    la t1, __trap_stack_top
    bgeu sp, t1, .Lemergency
    li t2, 1
    li t4, 0
    j .Lsample
.Lemergency:
    la t0, __trap_emergency_stack_bottom
    bltu sp, t0, .Lthread
    la t1, __trap_emergency_stack_top
    bgeu sp, t1, .Lthread
    li t2, 2
    li t4, 0
    j .Lsample
.Lthread:
    la t0, __thread_kernel_stacks_start
    bltu sp, t0, .Lbad
    la t1, __thread_kernel_stacks_end
    bgeu sp, t1, .Lbad
    sub t2, sp, t0
    srli t2, t2, 14
    slli t1, t2, 14
    add t0, t0, t1
    bltu sp, t0, .Lbad
    li t1, 16384
    add t1, t0, t1
    bltu t1, sp, .Lbad
    addi t2, t2, 3
    li t4, 1
.Lsample:
    sub t5, t1, sp
    slli t2, t2, 6
    la t3, micros_stack_observations
    add t3, t3, t2
    ld t0, 0(t3)
    beqz t4, .Lstatic_owner
    beqz t0, .Lbad
    j .Lowner_ready
.Lstatic_owner:
    bnez t0, .Lbad
.Lowner_ready:
    ld t0, 32(t3)
    bnez t0, .Lbad
#else
    li t0, 0x7ffff000
    bltu sp, t0, .Lbad
    li t1, 0x80000000
    bltu t1, sp, .Lbad
    sub t5, t1, sp
    la t3, micros_stack_user_observation
#endif
    addi t0, t3, 8
    amomaxu.d.aqrl zero, t5, (t0)
    addi t0, t3, 16
    li t1, 1
    amoadd.d.aqrl t2, t1, (t0)
    li t1, -1
    bne t2, t1, .Lreturn
    addi t0, t3, 24
    li t1, 1
    amoor.d.aqrl zero, t1, (t0)
.Lreturn:
    ret
.Lbad:
#ifdef MICROS_STACK_OBSERVER_KERNEL
    la t0, micros_stack_observer_failure
#else
    la t0, micros_stack_user_observation
    addi t0, t0, 24
#endif
    li t1, 1
    amoor.d.aqrl zero, t1, (t0)
    ret
.size __cyg_profile_func_enter, . - __cyg_profile_func_enter
.section .text.__cyg_profile_func_exit, "ax", @progbits
.balign 4
.globl __cyg_profile_func_exit
.type __cyg_profile_func_exit, @function
__cyg_profile_func_exit:
    ret
.size __cyg_profile_func_exit, . - __cyg_profile_func_exit
.section .bss.stack_observation, "aw", @nobits
.balign 8
#ifdef MICROS_STACK_OBSERVER_KERNEL
.globl micros_stack_observations
.type micros_stack_observations, @object
micros_stack_observations:
    .skip 131 * 64
.size micros_stack_observations, . - micros_stack_observations
.globl micros_stack_observer_failure
.type micros_stack_observer_failure, @object
micros_stack_observer_failure:
    .skip 8
.size micros_stack_observer_failure, . - micros_stack_observer_failure
#else
.globl micros_stack_user_observation
.type micros_stack_user_observation, @object
micros_stack_user_observation:
    .skip 64
.size micros_stack_user_observation, . - micros_stack_user_observation
#endif
.option pop
.section .note.GNU-stack, "", @progbits
"""
HOOK_SYMBOLS = ("__cyg_profile_func_enter", "__cyg_profile_func_exit")


@dataclass(frozen=True)
class ObserverEvidence:
    frames: tuple
    calls: tuple
    records: int


@lru_cache(maxsize=8)
def reference_observer(compiler, compiler_sha, kernel):
    with tempfile.TemporaryDirectory(prefix="micros-observer-artifact-") as temporary:
        root = Path(temporary)
        source, output = root / "observer.S", root / "observer.obj"
        source.write_text(OBSERVER_SOURCE)
        result = subprocess.run([
            compiler, "--target=riscv64-unknown-elf", "-march=rv64imac_zicsr_zifencei",
            "-mabi=lp64", "-mcmodel=medany",
            *(("-DMICROS_STACK_OBSERVER_KERNEL=1",) if kernel else ()),
            "-c", str(source), "-o", str(output),
        ], capture_output=True, text=True, timeout=60)
        if result.returncode or _file_identity(Path(compiler))["sha256"] != compiler_sha:
            raise StackEvidenceError("observer artifact compiler failed or changed")
        return load_object(output)


def observer_frame_evidence(root, obj, variant, image, kernel):
    if type(kernel) is not bool:
        raise StackEvidenceError("observer has an undeclared ownership mode")
    if read_regular(Path(root) / "tests/stack/observer.S")[0].decode("utf-8") != OBSERVER_SOURCE:
        raise StackEvidenceError("observer assembly differs from its independent artifact declaration")
    compiler = str(target_compiler(dict(os.environ)).resolve(strict=True))
    expected = reference_observer(compiler, _file_identity(Path(compiler))["sha256"], kernel)
    if allocated_signature(obj) != allocated_signature(expected):
        raise StackEvidenceError("observer object changed its declared ranges/atomic primitives/ABI")
    symbols = function_symbols(obj)
    if set(symbols) != {"__cyg_profile_func_enter", "__cyg_profile_func_exit"}:
        raise StackEvidenceError("observer leaf symbol inventory is incomplete")
    frames = []
    for name, symbol in symbols.items():
        require_c_frame_movement(obj, symbol, 0)
        section = obj.sections[symbol.section_index]
        for _, width, word in instructions(
            section_bytes(obj, section)[symbol.value:symbol.value + symbol.size], symbol.value,
        ):
            destination = word >> 7 & 31 if width == 4 and word & 0x7f == 0x2f else (
                _instruction_destination(word, width)
            )
            if destination not in (None, 0, *CALLER_SAVED) or width == 4 and word & 0x7f == 0x73:
                raise StackEvidenceError("observer changes stack, callee custody or interrupt state")
        frames.append(Frame(FunctionKey(variant, image, "tests/stack/observer.S", name), 0))
    return ObserverEvidence(tuple(frames), (), 131 if kernel else 1)


def compiler_hook_inventory(obj, symbol):
    flow = function_flow(obj, symbol)
    hooks = tuple(call for call in flow.calls if call.target_symbol in (
        "__cyg_profile_func_enter", "__cyg_profile_func_exit",
    ))
    enters = tuple(call for call in hooks if call.target_symbol == "__cyg_profile_func_enter")
    exits = tuple(call for call in hooks if call.target_symbol == "__cyg_profile_func_exit")
    returns = {
        point for point, (width, word) in flow.instructions.items() if is_return(width, word)
    }
    if len(enters) != 1 or len(exits) != (1 if returns else 0):
        raise StackEvidenceError("observed function lacks its complete compiler entry/exit inventory")
    inputs = register_inputs(obj, symbol)
    for hook in hooks:
        if (
            hook.offset not in flow.continuations
            or inputs[hook.offset][10] != ("symbol", symbol.name, symbol.section_index, symbol.value)
            or inputs[hook.offset][11] != ("entry-return-address",)
            or inputs[hook.offset][2] is None or inputs[hook.offset][2][0] != "stack"
            or inputs[hook.offset][2][1] >= 0
        ):
            raise StackEvidenceError("compiler hook borrowed another function or incoming return address")
    enter, = enters
    bypass = reachable_instructions(flow, excluded=flow.continuations[enter.offset])
    if returns & bypass or any(
        call.offset != enter.offset and call.offset in bypass for call in flow.calls
    ):
        raise StackEvidenceError("observed call/return bypasses its exact entry hook")
    if exits and returns & reachable_instructions(flow, excluded=flow.continuations[exits[0].offset]):
        raise StackEvidenceError("observed normal return bypasses its exact exit hook")
    return hooks


def runtime_call_inventory(obj, symbol):
    calls = function_flow(obj, symbol).calls
    hooks = compiler_hook_inventory(obj, symbol) if any(
        call.target_symbol in HOOK_SYMBOLS for call in calls
    ) else ()
    return tuple(call for call in calls if call not in hooks), hooks


def qemu_recipe_arguments(root, directory, target):
    from tests.host.stack_recipe import project_root_recipe, validate_stack_recipe

    root, directory = Path(root), Path(directory)
    try:
        validate_stack_recipe(root)
        source = project_root_recipe(read_regular(root / "CMakeLists.txt")[0].decode())
    except (AssertionError, OSError) as error:
        raise StackEvidenceError(f"unbound stack recipe extension: {error}") from error
    if source != reference_source(str(root), "CMakeLists.txt"):
        raise StackEvidenceError("legacy QEMU recipe changed from its fixed source stage")
    content = _run((ninja_path(directory), "-C", directory, "-t", "commands", target)).decode()
    runner = str(root / "tools/run_qemu_smoke.py")
    found = []
    for line in content.splitlines():
        words = shlex.split(line)
        if runner not in words:
            continue
        if words.count(runner) != 1:
            raise StackEvidenceError("legacy QEMU recipe has a duplicated runner")
        position = words.index(runner)
        if position < 4 or words[position - 4:position] != [
            os.environ["MICROS_PYTHON"], "-E", "-S", "-B",
        ] or "&&" in words[position + 1:]:
            raise StackEvidenceError("legacy QEMU recipe has an unbound interpreter or command")
        found.append(tuple(words[position + 1:]))
    if len(found) != 1:
        raise StackEvidenceError("legacy QEMU recipe lacks one exact runner invocation")
    return found[0]


def certify_kernel_observation(root, directory):
    from tools.stack_bound_build import validate_observed_recipe
    from tools.stack_bound_certify import certify_image
    from tools.stack_bound_scope import SCENARIOS, require_image_outputs
    from tools.stack_bound_manifest import SEAL, verify_build_seal
    from tools.stack_bound_user_images import (
        STATIC_ONLY_USER_IMAGES, USER_IMAGE_SCOPES, user_observer_descriptor,
    )
    from tools.user_elf_parse import load_elf
    from tools.validation_context import capture_candidate

    root, directory = Path(root).resolve(), Path(directory).resolve()
    variant = directory.parent.name
    if variant not in SCENARIOS or directory != root / "build/stack-evidence" / variant / "observed":
        raise StackEvidenceError("kernel observation has an undeclared artifact scope")
    scenario = SCENARIOS[variant]
    flag = scenario.flag
    image_names = require_image_outputs(directory, variant)
    validate_observed_recipe(root)
    cache = read_regular(directory / "CMakeCache.txt")[0].decode("utf-8").splitlines()
    if [line for line in cache if line.startswith(flag + ":")] != [
        flag + ":BOOL=ON",
    ] or [line.partition("=")[2] for line in cache if line.startswith("MICROS_STACK_BUILD_MODE:")] != ["observed"]:
        raise StackEvidenceError("scheduler observation lacks its explicit compiled mode")
    candidate = capture_candidate(root)
    if not candidate.clean:
        raise StackEvidenceError("scheduler observation lacks its exact current staged source")
    sealed = verify_build_seal(root, directory, variant, "observed", candidate)
    seal_identity = read_regular(directory / SEAL)[1]
    static_users = USER_IMAGE_SCOPES.get(variant, ()) + STATIC_ONLY_USER_IMAGES.get(variant, ())
    for relative in dict.fromkeys(path for _, path in static_users):
        user_observer_descriptor(load_elf(directory / relative), relative, next(
            image_id for image_id, path in static_users if path == relative
        ))
    proofs = tuple(certify_image(root, directory, name, variant, "observed") for name in image_names)
    if (
        capture_candidate(root) != candidate
        or verify_build_seal(root, directory, variant, "observed", candidate) != sealed
        or read_regular(directory / SEAL)[1] != seal_identity
    ):
        raise StackEvidenceError("observation source or artifacts changed during static proof")
    return proofs, seal_identity
