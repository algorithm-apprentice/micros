#!/usr/bin/env -S python3.15 -E -S -B

"""Build and certify every current stack scope without accepting external evidence."""

import argparse
import fcntl
import os
from pathlib import Path
import stat
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.python_host_runtime import require_current_runtime
from tools.stack_artifact_build import link_argv
from tools.stack_artifact_files import read_regular
from tools.stack_bound_build import build_observed_profile, build_profile, static_profiles
from tools.stack_bound_certify import certify_image, verify_firmware_proofs
from tools.stack_bound_images import require_passive_images
from tools.stack_bound_manifest import SEAL, require_paired_compile_inputs, verify_build_seal
from tools.stack_bound_publish import StaticConfiguration, publish_complete_result
from tools.stack_bound_run import run_observed_scope
from tools.stack_bound_scope import KERNEL_ONLY, OTHER_IMAGES, SCENARIOS, require_image_outputs
from tools.stack_bound_types import StackEvidenceError
from tools.user_elf_parse import load_elf
from tools.validation_context import capture_candidate
from tools.validation_inputs import UnsupportedContext


ROOT = Path(__file__).resolve().parent.parent


def static_configuration(root, profile, candidate):
    ordinary = build_profile(root, profile, "ordinary", os.environ["MICROS_PYTHON"])
    metadata = build_profile(root, profile, "metadata", os.environ["MICROS_PYTHON"])
    first = verify_build_seal(root, ordinary, profile.configure, "ordinary", candidate)
    second = verify_build_seal(root, metadata, profile.configure, "metadata", candidate)
    original_seals = read_regular(ordinary / SEAL)[1], read_regular(metadata / SEAL)[1]
    require_paired_compile_inputs(ordinary, metadata, first, second)
    names = require_image_outputs(ordinary, profile.configure)
    require_image_outputs(metadata, profile.configure)
    for name in names:
        original = read_regular(ordinary / name)[0]
        measured = read_regular(metadata / name)[0]
        require_passive_images(original, measured)
        if name != "applications/micros-application-runtime-probe.runtime.elf" and (
            link_argv(ordinary, name, name) != link_argv(metadata, name, name)
        ):
            raise StackEvidenceError("ordinary/metadata linked image recipes differ")
        image = load_elf(ordinary / name)
        if any(symbol.name.startswith((
            "__cyg_profile_", "micros_stack_observer", "micros_stack_observations",
            "micros_stack_user_observation",
        )) for symbol in image.symbols):
            raise StackEvidenceError("ordinary executable contains observer state or hooks")
    binaries = {str(path.relative_to(ordinary)) for path in ordinary.rglob("*.bin")}
    if binaries != {str(path.relative_to(metadata)) for path in metadata.rglob("*.bin")}:
        raise StackEvidenceError("ordinary/metadata complete binary output sets differ")
    for name in binaries:
        if read_regular(ordinary / name)[0] != read_regular(metadata / name)[0]:
            raise StackEvidenceError("metadata changed a complete seed/binary output")
    images = tuple(certify_image(root, metadata, name, profile.configure, "metadata") for name in names)
    verify_firmware_proofs(images)
    if (
        verify_build_seal(root, ordinary, profile.configure, "ordinary", candidate) != first
        or verify_build_seal(root, metadata, profile.configure, "metadata", candidate) != second
        or original_seals != (read_regular(ordinary / SEAL)[1], read_regular(metadata / SEAL)[1])
    ):
        raise StackEvidenceError("paired build inputs changed during static proof")
    return StaticConfiguration(
        profile.configure, images, *original_seals,
    )


def execute(root):
    candidate = capture_candidate(root)
    if not candidate.clean:
        raise StackEvidenceError("stack evidence requires a complete clean staged candidate")
    profiles = static_profiles(root)
    if {profile.configure for profile in profiles} != set(KERNEL_ONLY) | set(OTHER_IMAGES) | set(SCENARIOS):
        raise StackEvidenceError("current stack configuration declaration is incomplete")
    static = []
    for index, profile in enumerate(profiles, 1):
        print(f"MICROS_STACK_STATIC {index}/{len(profiles)} {profile.configure}", flush=True)
        static.append(static_configuration(root, profile, candidate))
    observed = []
    by_configure = {profile.configure: profile for profile in profiles}
    for index, (configure, scenario) in enumerate(SCENARIOS.items(), 1):
        print(f"MICROS_STACK_TARGET {index}/{len(SCENARIOS)} {scenario.gap} {configure}", flush=True)
        directory = build_observed_profile(root, by_configure[configure], os.environ["MICROS_PYTHON"])
        result = run_observed_scope(root, directory)
        observed.append(result)
        print(f"MICROS_STACK_SCOPE_PASS {scenario.gap} producers={len(result.observations)}", flush=True)
    path = publish_complete_result(root, candidate, tuple(static), tuple(observed))
    print(f"MICROS_STACK_EVIDENCE_PASS current=10 future=B13-E007:BLOCKED output={path}", flush=True)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)
    require_current_runtime()
    for path in (ROOT / "build", ROOT / "build/stack-evidence"):
        if path.is_symlink():
            raise StackEvidenceError("stack evidence build namespace is linked")
        path.mkdir(exist_ok=True)
    lock = ROOT / "build/stack-evidence/.run.lock"
    descriptor = os.open(lock, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        status = os.fstat(descriptor)
        if not stat.S_ISREG(status.st_mode) or status.st_nlink != 1:
            raise StackEvidenceError("stack evidence lock is not a private regular file")
        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return execute(ROOT)
    finally:
        os.close(descriptor)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (StackEvidenceError, UnsupportedContext, OSError, subprocess.CalledProcessError) as error:
        print(f"MICROS_STACK_EVIDENCE_FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
