"""Transported stack data retains its image ownership and checked arithmetic."""

import copy
from pathlib import Path
import tempfile
import unittest

from tests.host.test_ci_records import context


def user_image():
    first = {"variant": "fixture", "image": "bootstrap/probe.elf",
             "translation_unit": "lib/runtime/start.S", "symbol": "_start"}
    second = {**first, "translation_unit": "tests/qemu/probe.c", "symbol": "micros_service_main"}
    return {
        "variant": "fixture", "image": "bootstrap/probe.elf", "mode": "metadata",
        "frames": [
            {"function": first, "bytes_used": 0, "classification": "static"},
            {"function": second, "bytes_used": 32, "classification": "static"},
        ],
        "calls": [{"caller": 0, "offset": 0, "kind": "call", "targets": [1]}],
        "roots": [
            {"root": {"identifier": name, "function": function, "capacity": 4096, "initial_bytes": 0},
             "bytes_used": 32, "bytes_remaining": 4064,
             "witness": [{"function": index, "qualifier": "UNQUALIFIED"} for index in indexes]}
            for name, function, indexes in (
                ("user-startup", first, (0, 1)), ("service-main", second, (1,)),
            )
        ],
        "epochs": [], "payloads": [], "faults": [], "continuations": [], "snapshots": [],
        "fatal_context": None,
    }


def kernel_image(firmware):
    declarations = (
        ("boot", "_start", 16384, 0),
        ("supervisor", "micros_trap_entry", 16384, 0),
        ("emergency", "micros_trap_nested_panic", 4096, 0),
        ("primary-idle", "micros_scheduler_idle_wait", 16384, 0),
        ("kernel-main", "kernel_main", 16384, 0),
        ("trap-dispatch", "micros_trap_dispatch", 16384, 288),
        ("panic-report", "micros_panic_report", 4096, 48),
        ("idle-select", "micros_scheduler_idle_select", 16384, 0),
    )
    functions = [
        {"variant": "riscv64-debug", "image": "kernel/micros.elf",
         "translation_unit": "kernel/fixture.c", "symbol": symbol}
        for _, symbol, _, _ in declarations
    ]
    functions.extend((
        {**functions[0], "translation_unit": "arch/riscv64/uart.c", "symbol": "uart_write"},
        {**functions[0], "translation_unit": "arch/riscv64/panic.S", "symbol": "micros_panic_entry"},
    ))
    from tools.stack_bound_publish import json_value

    return {
        "variant": "riscv64-debug", "image": "kernel/micros.elf", "mode": "metadata",
        "frames": [{"function": value, "bytes_used": 16, "classification": "static"} for value in functions],
        "calls": [{"caller": 8, "offset": 0, "kind": "call", "targets": [9]}],
        "roots": [
            {"root": {"identifier": name, "function": functions[index], "capacity": capacity, "initial_bytes": initial},
             "bytes_used": initial + 16, "bytes_remaining": capacity - initial - 16,
             "witness": [{"function": index, "qualifier": "UNQUALIFIED"}]}
            for index, (name, _, capacity, initial) in enumerate(declarations)
        ],
        "epochs": [], "payloads": [], "faults": [], "continuations": [], "snapshots": [],
        "fatal_context": {
            "firmware": json_value(firmware),
            "bindings": {"establishing": [], "preservers": [functions[8]], "infeasible": [functions[8], 0]},
        },
    }


class CiStackRecordsTest(unittest.TestCase):
    def test_valid_proof_can_be_replayed_without_rebuilding_or_changing_its_identity(self):
        from tools.ci_stack_records import validate_image

        image = user_image()
        validate_image(image, "fixture", "bootstrap/probe.elf", "metadata", None)
        self.assertEqual(user_image(), image)

    def test_borrowed_identity_bad_frame_call_root_and_witness_fail(self):
        from tools.ci_stack_records import validate_image

        for mutation in ("image", "frame", "call", "root", "bound", "witness"):
            image = copy.deepcopy(user_image())
            if mutation == "image":
                image["frames"][0]["function"]["image"] = "bootstrap/foreign.elf"
            elif mutation == "frame":
                image["frames"][1]["bytes_used"] = True
            elif mutation == "call":
                image["calls"][0]["targets"] = [2]
            elif mutation == "root":
                image["roots"][0]["root"]["capacity"] = 8192
            elif mutation == "bound":
                image["roots"][0]["bytes_used"] = 31
            else:
                image["roots"][0]["witness"] = image["roots"][0]["witness"][:1]
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                validate_image(image, "fixture", "bootstrap/probe.elf", "metadata", None)

    def test_profile_publication_preserves_live_and_serialized_firmware_identity(self):
        from tools.ci_records import read_json
        from tools.ci_stack_records import validate_profile
        from tools.stack_artifact_files import read_regular
        from tools.stack_bound_manifest import encoded, publish_json

        before = context()
        before["firmware"] = {
            "data_roots": ("/selected/qemu/share/qemu",),
            "firmware": {"sha256": "f" * 64},
        }
        profile = "riscv64-debug"
        with tempfile.TemporaryDirectory(prefix="micros-ci-profile-publication-") as temporary:
            directory = Path(temporary)
            seals = directory / "seals" / profile
            seals.mkdir(parents=True)
            identities = {}
            for mode in ("ordinary", "metadata"):
                path = seals / f"{mode}.json"
                path.write_bytes(encoded({
                    "version": 1, "profile": profile, "mode": mode,
                    "candidate": before["candidate"], "tools": before["tools"],
                }))
                identities[mode] = read_regular(path)[1]
            record = {
                "version": 1, "status": "PROFILE_COMPLETE", "part": before["part"], "profile": profile,
                "before": before, "after": before,
                "static": {
                    "profile": profile, "ordinary_seal": identities["ordinary"],
                    "metadata_seal": identities["metadata"], "images": [kernel_image(before["firmware"])],
                },
                "observed": None,
            }
            path = directory / "profile.json"
            publish_json(path, record, validate=lambda: validate_profile(record, profile, before, directory))
            serialized = read_json(path)
            validate_profile(serialized, profile, before, directory)
            for field in ("data_roots", "firmware"):
                changed = copy.deepcopy(record)
                changed["static"]["images"][0]["fatal_context"]["firmware"][field] = (
                    ["/foreign/qemu"] if field == "data_roots" else {"sha256": "0" * 64}
                )
                with self.subTest(field=field), self.assertRaises(ValueError):
                    publish_json(path, changed, validate=lambda: validate_profile(changed, profile, before, directory))
                self.assertEqual(serialized, read_json(path))


if __name__ == "__main__":
    unittest.main()
