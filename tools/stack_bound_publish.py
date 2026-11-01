"""Complete current-scope certificates and explicit blocked B13 admission."""

from dataclasses import dataclass, fields, is_dataclass
from enum import Enum
import hashlib
from pathlib import Path

from tools.stack_artifact_files import read_regular
from tools.stack_bound_certify import verify_firmware_proofs
from tools.stack_bound_manifest import SEAL, publish_json, verify_build_seal
from tools.stack_bound_scope import KERNEL_ONLY, OTHER_IMAGES, SCENARIOS, expected_images
from tools.stack_bound_types import StackEvidenceError
from tools.validation_context import capture_candidate


@dataclass(frozen=True)
class StaticConfiguration:
    profile: str
    images: tuple
    ordinary_seal: dict
    metadata_seal: dict


def json_value(value):
    if isinstance(value, Enum):
        return value.name
    if is_dataclass(value):
        return {field.name: json_value(getattr(value, field.name)) for field in fields(value)}
    if isinstance(value, (tuple, list)):
        return [json_value(item) for item in value]
    if isinstance(value, dict):
        return {key: json_value(item) for key, item in value.items()}
    return value


def image_record(proof):
    frame_rows = sorted(proof.frames, key=lambda row: row.function)
    indexes = {row.function: index for index, row in enumerate(frame_rows)}
    roots = []
    for certificate in proof.roots:
        witness = []
        for item in certificate.witness:
            key = item.function if hasattr(item, "qualifier") else item
            witness.append({
                "function": indexes[key],
                "qualifier": item.qualifier.name if hasattr(item, "qualifier") else "UNQUALIFIED",
            })
        roots.append({
            "root": json_value(certificate.root), "bytes_used": certificate.bytes_used,
            "bytes_remaining": certificate.bytes_remaining, "witness": witness,
        })
    return {
        "variant": proof.variant, "image": proof.image, "mode": proof.mode,
        "frames": json_value(tuple(frame_rows)),
        "calls": [
            {"caller": indexes[edge.caller], "offset": edge.offset, "kind": edge.kind.value,
             "targets": [indexes[target] for target in edge.targets]}
            for edge in proof.edges
        ],
        "roots": roots, "epochs": json_value(proof.epochs), "payloads": json_value(proof.payloads),
        "faults": json_value(proof.faults), "continuations": json_value(proof.continuations),
        "snapshots": json_value(proof.snapshots),
        "fatal_context": json_value(proof.fatal),
    }


def validate_complete_scopes(static, observed):
    expected = set(KERNEL_ONLY) | set(OTHER_IMAGES) | set(SCENARIOS)
    if (
        type(static) is not tuple or type(observed) is not tuple
        or len(static) != len(expected) or len(observed) != len(SCENARIOS)
    ):
        raise StackEvidenceError("certificate publication requires every current static and target scope")
    if (
        {row.profile for row in static} != expected
        or {row.configure for row in observed} != set(SCENARIOS)
    ):
        raise StackEvidenceError("certificate scope inventory is duplicated, missing or unregistered")
    for row in static:
        if tuple(sorted(image.image for image in row.images)) != expected_images(row.profile):
            raise StackEvidenceError("static certificate omitted an executable output")
        if any(image.variant != row.profile or image.mode != "metadata" or not image.roots
               for image in row.images):
            raise StackEvidenceError("static certificate has a borrowed or incomplete image proof")
    for row in observed:
        if row.gap != SCENARIOS[row.configure].gap or not row.observations:
            raise StackEvidenceError("target certificate omitted its required gap or live producers")
        if tuple(sorted(image.image for image in row.image_proofs)) != expected_images(row.configure):
            raise StackEvidenceError("target certificate omitted an observed executable")


def publish_complete_result(root, candidate, static, observed):
    validate_complete_scopes(static, observed)
    root = Path(root)
    if candidate is None or not candidate.clean or capture_candidate(root) != candidate:
        raise StackEvidenceError("certificate source is not the exact validated candidate")
    payload = {
        "version": 1, "outcome": "PASS", "scope": "current-b01-b12",
        "candidate": candidate.source,
        "static": [
            {"profile": row.profile, "ordinary_seal": row.ordinary_seal,
             "metadata_seal": row.metadata_seal, "images": [image_record(image) for image in row.images]}
            for row in static
        ],
        "certificates": [
            {
                "gap": row.gap, "outcome": "PASS", "profile": row.configure,
                "observed_seal": row.seal,
                "images": [image_record(image) for image in row.image_proofs],
                "observations": json_value(row.observations),
                "qemu": {"argv": list(row.command), "timeout": row.timeout},
                "transcript_sha256": hashlib.sha256(row.transcript.encode("utf-8")).hexdigest(),
                "transcript": row.transcript,
            }
            for row in observed
        ],
        "future_admission": {
            "gap": "B13-E007", "outcome": "BLOCKED", "reason": "production-node-q-absent",
            "completion_barrier": "STACK13",
        },
    }
    def validate():
        if capture_candidate(root) != candidate:
            raise StackEvidenceError("source changed during certificate publication")
        for row in static:
            for mode, identity in (("ordinary", row.ordinary_seal), ("metadata", row.metadata_seal)):
                directory = root / "build/stack-evidence" / row.profile / mode
                verify_build_seal(root, directory, row.profile, mode, candidate)
                if read_regular(directory / SEAL)[1] != identity:
                    raise StackEvidenceError("static build changed after its image proofs")
        for row in observed:
            directory = root / "build/stack-evidence" / row.configure / "observed"
            verify_build_seal(root, directory, row.configure, "observed", candidate)
            if read_regular(directory / SEAL)[1] != row.seal:
                raise StackEvidenceError("observed build changed after its target evidence")
        verify_firmware_proofs(
            tuple(image for row in static for image in row.images)
            + tuple(image for row in observed for image in row.image_proofs)
        )
        if capture_candidate(root) != candidate:
            raise StackEvidenceError("source changed during certificate publication")

    path = root / "build/stack-evidence/current.json"
    publish_json(path, payload, validate=validate)
    return path
