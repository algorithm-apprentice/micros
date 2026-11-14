"""Current frame-ownership source, dependency and fixture-state contracts."""

from pathlib import Path
import re
import unittest

from tests.host.build_source_records import cmake_command, cmake_if_blocks
from tests.host.c_source_records import (
    c_function_definitions, c_lexical_text, c_signature_start, file_scope_objects, tokens,
)
from tests.host.source_snapshot import source_bytes
from tools import validation_plan


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SOURCE_SETS = {
    "MICROS_FRAME_OWNERSHIP_SOURCES": tuple(
        "kernel/memory/frame_ownership" + suffix + ".c"
        for suffix in ("", "_validation", "_bootstrap", "_handoff", "_vm_claim", "_vm_freeze", "_vm_release")
    ),
    "MICROS_FRAME_ALLOCATOR_HOST_TEST_SOURCES": (
        "tests/host/frame_allocator_test.c", "tests/host/frame_allocator_test_cases.c",
        "tests/host/frame_allocator_model_test.c",
    ),
    "MICROS_FRAME_OWNERSHIP_HOST_TEST_SOURCES": tuple(
        "tests/host/frame_ownership_test" + suffix + ".c"
        for suffix in ("", "_fixture", "_bootstrap", "_validation", "_handoff", "_model", "_vm")
    ),
    "MICROS_FRAME_OWNERSHIP_QEMU_TEST_SOURCES": tuple(
        "kernel/tests/memory/frame_ownership_test" + suffix + ".c"
        for suffix in ("", "_fixture", "_lifecycle", "_handoff")
    ),
}
PRIVATE_HEADERS = (
    "kernel/memory/frame_ownership_internal.h",
    "kernel/tests/memory/frame_ownership_test_internal.h",
    "tests/host/frame_allocator_test_internal.h", "tests/host/frame_ownership_test_internal.h",
)
PUBLIC_SYMBOLS = {
    "kernel/memory/frame_ownership.c": (
        "micros_frame_owner_make_kernel", "micros_frame_owner_make_process", "micros_frame_ownership_initialize",
    ),
    "kernel/memory/frame_ownership_validation.c": (
        "micros_frame_ownership_lookup", "micros_frame_ownership_count_process", "micros_frame_ownership_validate",
        "micros_frame_ownership_lookup_prevalidated",
    ),
    "kernel/memory/frame_ownership_bootstrap.c": (
        "micros_frame_ownership_allocate", "micros_frame_ownership_release", "micros_frame_ownership_release_process_set",
    ),
    "kernel/memory/frame_ownership_handoff.c": (
        "micros_frame_ownership_prepare_handoff", "micros_frame_ownership_prepare_wired_process_user_set",
        "micros_frame_ownership_complete_handoff",
    ),
    "kernel/memory/frame_ownership_vm_claim.c": (
        "micros_frame_ownership_vm_claim_preflight", "micros_frame_ownership_vm_claim_commit_prevalidated",
    ),
    "kernel/memory/frame_ownership_vm_freeze.c": (
        "micros_frame_ownership_vm_freeze_preflight", "micros_frame_ownership_vm_freeze_commit_prevalidated",
    ),
    "kernel/memory/frame_ownership_vm_release.c": (
        "micros_frame_ownership_vm_release_transferable_preflight",
        "micros_frame_ownership_vm_release_transferable_commit_prevalidated",
        "micros_frame_ownership_vm_release_process_preflight",
        "micros_frame_ownership_vm_release_process_commit_prevalidated",
    ),
    "kernel/tests/memory/frame_ownership_test.c": ("micros_frame_ownership_runtime_run_self_test",),
}


def current_text(path):
    return source_bytes(REPOSITORY_ROOT, REPOSITORY_ROOT / path).decode()


def static_definition_paths(sources, name):
    pattern = re.compile(rf"(?ms)^static\b[^;{{}}]*?\b{re.escape(name)}\b[^;{{}}]*;")
    return [
        path for path in sources
        if (REPOSITORY_ROOT / path).is_file() and pattern.search(current_text(path))
    ]


class FrameOwnershipSourceLayoutTest(unittest.TestCase):
    def test_current_source_sets_and_declaration_only_private_headers(self):
        cmake = current_text("CMakeLists.txt")
        for name, sources in SOURCE_SETS.items():
            with self.subTest(source_set=name):
                block = cmake_command(cmake, "set", name)
                self.assertEqual(sources, tuple(re.findall(r"(?:kernel|tests/host)/[A-Za-z0-9_/]+\.c", block)))
                self.assertEqual(len(sources), len(set(sources)))
                self.assertTrue(all((REPOSITORY_ROOT / path).is_file() for path in sources))
        for header in PRIVATE_HEADERS:
            with self.subTest(header=header):
                self.assertEqual({}, c_function_definitions(current_text(header)))

    def test_consumers_expand_the_complete_source_sets_once(self):
        cmake = current_text("CMakeLists.txt")
        expected = {
            "frame_allocator_test": ("MICROS_FRAME_ALLOCATOR_HOST_TEST_SOURCES",),
            "frame_ownership_test": ("MICROS_FRAME_OWNERSHIP_SOURCES", "MICROS_FRAME_OWNERSHIP_HOST_TEST_SOURCES"),
            **{name: ("MICROS_FRAME_OWNERSHIP_SOURCES",) for name in (
                "vm_bootstrap_test", "pm_test", "vm_core_test", "micros",
            )},
        }
        for target, selected in expected.items():
            block = cmake_command(cmake, "add_executable", target)
            for name in SOURCE_SETS:
                self.assertEqual(int(name in selected), block.count("${" + name + "}"), (target, name))
        block = "\n".join(cmake_if_blocks(cmake, "MICROS_BUILD_FRAME_OWNERSHIP_TEST"))
        for name in SOURCE_SETS:
            self.assertEqual(int(name == "MICROS_FRAME_OWNERSHIP_QEMU_TEST_SOURCES"), block.count("${" + name + "}"))

    def test_public_entrypoints_keep_their_responsible_owner(self):
        definitions = {}
        for path in {path for sources in SOURCE_SETS.values() for path in sources}:
            for name in c_function_definitions(current_text(path)):
                definitions.setdefault(name, []).append(path)
        for path, names in PUBLIC_SYMBOLS.items():
            for name in names:
                with self.subTest(symbol=name):
                    self.assertEqual([path], definitions.get(name))

    def test_source_includes_preserve_the_production_test_boundary(self):
        sources = {path for paths in SOURCE_SETS.values() for path in paths} | set(PRIVATE_HEADERS)
        test_headers = {Path(path).name for path in PRIVATE_HEADERS[1:]}
        for path in sources:
            includes = re.findall(r'(?m)^\s*#\s*include\s*[<"]([^>"]+)[>"]', current_text(path))
            self.assertFalse(any(name.endswith(".c") for name in includes), path)
            if path.startswith("kernel/memory/"):
                self.assertFalse(any(Path(name).name in test_headers for name in includes), path)

    def test_split_target_and_host_fixtures_have_one_context(self):
        for source_set, kind in (
            ("MICROS_FRAME_OWNERSHIP_HOST_TEST_SOURCES", "frame_ownership_test_context"),
            ("MICROS_FRAME_OWNERSHIP_QEMU_TEST_SOURCES", "micros_frame_ownership_test_context"),
        ):
            for path in SOURCE_SETS[source_set]:
                expected = (f"static struct {kind} test_context;",) if path.endswith("_fixture.c") else ()
                with self.subTest(path=path):
                    self.assertEqual(expected, file_scope_objects(current_text(path)))

    def test_current_paths_keep_broad_validation_classification(self):
        for source_set, paths in SOURCE_SETS.items():
            for path in paths[1:]:
                for tier in ("fast", "pr"):
                    with self.subTest(source_set=source_set, path=path, tier=tier):
                        self.assertEqual(
                            validation_plan.plan([paths[0]], tier, "main"),
                            validation_plan.plan([path], tier, "main"),
                        )


if __name__ == "__main__":
    unittest.main()
