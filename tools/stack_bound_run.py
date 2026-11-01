"""One exact observed scenario after its complete static and artifact proof."""

from dataclasses import dataclass, replace
from pathlib import Path

from tools.qemu_smoke_arguments import parse_arguments
from tools.qemu_smoke_cli import _matches_observed_run
from tools.qemu_smoke_command import build_qemu_command
from tools.qemu_smoke_execution import run_qemu
from tools.qemu_smoke_outcome import classify_smoke
from tools.qemu_smoke_records import SmokeOutcome
from tools.stack_artifact_files import read_regular
from tools.stack_bound_observer import certify_kernel_observation, qemu_recipe_arguments
from tools.stack_bound_reports import legacy_transcript, validate_observation_records
from tools.stack_bound_scope import SCENARIOS, declared_producers, require_image_outputs
from tools.stack_bound_types import StackEvidenceError
from tools.stack_bound_certify import verify_firmware_proofs
from tools.stack_bound_manifest import SEAL, verify_build_seal
from tools.validation_context import capture_candidate


@dataclass(frozen=True)
class ObservedScope:
    configure: str
    gap: str
    proofs: tuple
    observations: tuple
    command: tuple
    timeout: float
    transcript: str
    images: tuple
    image_proofs: tuple
    seal: dict


def run_observed_scope(root, directory):
    root, directory = Path(root), Path(directory)
    candidate = capture_candidate(root)
    if not candidate.clean:
        raise StackEvidenceError("observation execution requires the clean staged source")
    image_proofs, seal = certify_kernel_observation(root, directory)
    proofs = tuple(proof for image in image_proofs for proof in image.roots)
    configure = directory.parent.name
    scenario = SCENARIOS[configure]
    names = require_image_outputs(directory, configure)
    images = tuple((name, read_regular(directory / name)[1]) for name in names)
    producers = declared_producers(root, directory, proofs)
    arguments = parse_arguments(list(qemu_recipe_arguments(root, directory, scenario.workflow)))
    cache = read_regular(directory / "CMakeCache.txt")[0].decode().splitlines()
    if (
        arguments.kernel != directory / "kernel/micros.elf"
        or arguments.memory != ["128M"] or arguments.expect != "pass"
        or arguments.trap_context_sepc_symbol or arguments.require_vm_self_fault
        or [line for line in cache if line.startswith("MICROS_QEMU:")] != [
            "MICROS_QEMU:FILEPATH=" + arguments.qemu,
        ]
    ):
        raise StackEvidenceError("observed guest recipe is outside its exact current configuration")
    command = tuple(build_qemu_command(qemu=arguments.qemu, kernel=str(arguments.kernel), memory="128M"))
    verify_firmware_proofs(image_proofs)
    if capture_candidate(root) != candidate:
        raise StackEvidenceError("source drifted before observation launch")
    result = run_qemu(
        command, arguments.timeout,
        serial_input_trigger=arguments.serial_input_trigger,
        serial_input=arguments.serial_input,
    )
    outcome = classify_smoke(
        output=result.output, return_code=result.return_code, timed_out=result.timed_out,
        markers=arguments.marker, require_fdt_events=arguments.require_fdt_events,
        require_fdt_reservations=arguments.require_fdt_reservations,
    )
    if outcome is not SmokeOutcome.PASS:
        raise StackEvidenceError(f"observed scenario failed its legacy outcome: {outcome.value}\n{result.output}")
    legacy = replace(result, output=legacy_transcript(result.output))
    if not _matches_observed_run(
        arguments, legacy, outcome, SmokeOutcome.PASS, None, None,
    ) or arguments.serial_input_trigger is not None and not result.serial_input_sent:
        raise StackEvidenceError("observations did not follow complete legacy acceptance\n" + result.output)
    observations = validate_observation_records(result.output, producers)
    if (
        capture_candidate(root) != candidate or read_regular(directory / SEAL)[1] != seal
        or images != tuple((name, read_regular(directory / name)[1]) for name in names)
    ):
        raise StackEvidenceError("observation source or executable changed during execution")
    verify_build_seal(root, directory, configure, "observed", candidate)
    verify_firmware_proofs(image_proofs)
    return ObservedScope(
        configure, scenario.gap, proofs, observations, command, arguments.timeout,
        result.output, images, image_proofs, seal,
    )
