"""Strict complete producer reports bound to externally derived stack limits."""

from dataclasses import dataclass
import re

from tools.qemu_smoke_records import _split_output_records
from tools.stack_bound_types import StackEvidenceError, require_path, require_uint64


PREFIX = "MICROS_STACK_OBSERVATION "
PATTERN = re.compile(
    r"MICROS_STACK_OBSERVATION image=(kernel|(?:bootstrap|applications)/[a-z0-9.-]+\.elf) "
    r"owner=(boot|primary|emergency|thread|user) slot=(0|[1-9][0-9]*) "
    r"generation=0x([0-9a-f]{16}) samples=0x([0-9a-f]{16}) use=0x([0-9a-f]{16}) "
    r"bottom=0x([0-9a-f]{16}) top=0x([0-9a-f]{16}) failure=0x([0-9a-f]{16})"
)


@dataclass(frozen=True)
class Producer:
    image: str
    owner: str
    slot: int
    generation: int
    bottom: int
    top: int
    bound: int
    required: bool = True

    @property
    def identity(self):
        return self.image, self.owner, self.slot, self.generation


@dataclass(frozen=True)
class Observation:
    producer: Producer
    samples: int
    use: int


def producer_inventory(producers):
    if type(producers) is not tuple or not producers:
        raise StackEvidenceError("missing complete declared producer inventory")
    result = {}
    for producer in producers:
        if type(producer) is not Producer or type(producer.required) is not bool:
            raise StackEvidenceError("invalid declared stack producer")
        for name in ("slot", "generation", "bottom", "top", "bound"):
            require_uint64(getattr(producer, name), "producer " + name)
        kernel = producer.image == "kernel"
        if not kernel:
            require_path(producer.image)
        if (
            kernel and producer.owner not in ("boot", "primary", "emergency", "thread")
            or not kernel and (producer.owner != "user" or not producer.image.endswith(".elf"))
            or producer.slot >= (128 if kernel else 64)
            or producer.bottom % 16 or producer.top % 16
            or producer.top <= producer.bottom
            or producer.top - producer.bottom != (
                4096 if producer.owner in ("user", "emergency") else 16384
            )
            or producer.bound > producer.top - producer.bottom
            or producer.owner in ("thread", "user") and producer.generation == 0
            or producer.owner not in ("thread", "user") and (producer.slot != 0 or producer.generation != 0)
            or producer.identity in result
        ):
            raise StackEvidenceError("producer declaration has duplicate or invalid identity/stack geometry")
        result[producer.identity] = producer
    if not any(producer.required for producer in result.values()):
        raise StackEvidenceError("producer inventory contains no required live owner")
    return result


def validate_observation_records(output, producers):
    if type(output) is not str:
        raise StackEvidenceError("stack reports must be the actual decoded guest transcript")
    expected = producer_inventory(producers)
    records, seen = [], set()
    lines, terminated = _split_output_records(output)
    for line, complete in zip(lines, terminated):
        if not line.startswith("MICROS_STACK_"):
            continue
        match = PATTERN.fullmatch(line)
        if not complete or match is None:
            raise StackEvidenceError("malformed or unterminated stack observation record")
        image, owner, slot = match[1], match[2], int(match[3])
        generation, samples, used, bottom, top, failure = (int(match[index], 16) for index in range(4, 10))
        identity = image, owner, slot, generation
        producer = expected.get(identity)
        if producer is None or identity in seen:
            raise StackEvidenceError("extra, duplicated or foreign-generation stack producer")
        if (
            samples == 0 or failure != 0 or bottom != producer.bottom or top != producer.top
            or used > top - bottom or used > producer.bound
        ):
            raise StackEvidenceError("stack observation has no samples, sticky failure or exceeds its bound")
        seen.add(identity)
        records.append(Observation(producer, samples, used))
    if any(producer.required and identity not in seen for identity, producer in expected.items()):
        raise StackEvidenceError("missing required live producer observations")
    return tuple(records)


def legacy_transcript(output):
    lines = output.splitlines(keepends=True)
    for index, line in enumerate(lines):
        if line.startswith("MICROS_STACK_"):
            if any(
                not later.startswith("MICROS_STACK_") and later.strip()
                for later in lines[index:]
            ):
                raise StackEvidenceError("observation records split the legacy serial contract")
            return "".join(lines[:index])
    raise StackEvidenceError("guest transcript contains no stack observations")
