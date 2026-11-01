"""Independent OpenSBI artifact expectation in Accepted ADR-0086."""

from pathlib import Path
import re

from tools.stack_artifact_files import read_regular
from tools.stack_bound_types import StackEvidenceError
from tools.validation_context import _cellar_root, qemu_configuration
from tools.validation_inputs import UnsupportedContext, _file_identity, _run


FIRMWARE_SIZE = 275928
FIRMWARE_SHA256 = "894e2aef99590fc07ec6c60ab00282b8bc5d5d5bb2a1d0c6ada0c52df24274c0"


def require_pinned_firmware(identity):
    if (
        type(identity) is not dict
        or set(identity) != {"kind", "mode", "size", "sha256"}
        or identity["kind"] != "file"
        or type(identity["mode"]) is not int
        or not 0 <= identity["mode"] <= 0o777
        or type(identity["size"]) is not int
        or identity["size"] != FIRMWARE_SIZE
        or identity["sha256"] != FIRMWARE_SHA256
    ):
        raise StackEvidenceError("default firmware is outside the independently pinned OpenSBI contract")


def resolve_default_firmware(executable):
    try:
        requested = Path(executable)
        if not requested.is_absolute():
            raise StackEvidenceError("default firmware requires an absolute configured QEMU")
        selected = requested.resolve(strict=True)
        installation = _cellar_root(selected)
        configuration = qemu_configuration(selected, installation)
        before = _file_identity(selected)
        version = _run((selected, "--version"))
        if re.search(rb"(?m)^QEMU emulator version 11\.", version) is None:
            raise StackEvidenceError("default firmware QEMU family is unsupported")
        roots = tuple(Path(line).resolve() for line in _run((selected, "-L", "help")).decode().splitlines())
        if not roots or any(not root.is_relative_to(installation) for root in roots):
            raise StackEvidenceError("default firmware data root leaves its configured QEMU installation")
        firmware = next((
            root / "opensbi-riscv64-generic-fw_dynamic.bin"
            for root in roots if (root / "opensbi-riscv64-generic-fw_dynamic.bin").is_file()
        ), None)
        if firmware is None or not firmware.resolve().is_relative_to(installation):
            raise StackEvidenceError("default firmware cannot be resolved inside its configured data root")
        _, identity = read_regular(firmware.resolve())
        require_pinned_firmware(identity)
        if _file_identity(selected) != before or qemu_configuration(selected, installation) != configuration:
            raise StackEvidenceError("default firmware QEMU/configuration changed during resolution")
        return {
            "qemu": {"path": str(requested), "resolved": str(selected), **before},
            "configuration": configuration,
            "data_roots": tuple(str(root) for root in roots),
            "firmware": {"path": str(firmware.resolve()), **identity},
        }
    except (OSError, UnicodeError, UnsupportedContext) as error:
        raise StackEvidenceError(f"default firmware/configuration closure failed: {error}") from error
