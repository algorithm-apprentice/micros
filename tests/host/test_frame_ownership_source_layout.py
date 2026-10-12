from __future__ import annotations

from pathlib import Path
import re
import unittest

from tools import validation_plan


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CMAKE_PATH = REPOSITORY_ROOT / "CMakeLists.txt"
FUNCTION_REVIEW_LINE_LIMIT = 120

SOURCE_SETS = {
    "MICROS_FRAME_OWNERSHIP_SOURCES": (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_validation.c",
        "kernel/frame_ownership_bootstrap.c",
        "kernel/frame_ownership_handoff.c",
        "kernel/frame_ownership_vm_claim.c",
        "kernel/frame_ownership_vm_freeze.c",
        "kernel/frame_ownership_vm_release.c",
    ),
    "MICROS_FRAME_ALLOCATOR_HOST_TEST_SOURCES": (
        "tests/host/frame_allocator_test.c",
        "tests/host/frame_allocator_test_cases.c",
        "tests/host/frame_allocator_model_test.c",
    ),
    "MICROS_FRAME_OWNERSHIP_HOST_TEST_SOURCES": (
        "tests/host/frame_ownership_test.c",
        "tests/host/frame_ownership_test_fixture.c",
        "tests/host/frame_ownership_test_bootstrap.c",
        "tests/host/frame_ownership_test_validation.c",
        "tests/host/frame_ownership_test_handoff.c",
        "tests/host/frame_ownership_test_model.c",
        "tests/host/frame_ownership_test_vm.c",
    ),
    "MICROS_FRAME_OWNERSHIP_QEMU_TEST_SOURCES": (
        "kernel/frame_ownership_test.c",
        "kernel/frame_ownership_test_fixture.c",
        "kernel/frame_ownership_test_lifecycle.c",
        "kernel/frame_ownership_test_handoff.c",
    ),
}

PRIVATE_HEADERS = (
    "kernel/frame_ownership_internal.h",
    "kernel/frame_ownership_test_internal.h",
    "tests/host/frame_allocator_test_internal.h",
    "tests/host/frame_ownership_test_internal.h",
)

TEST_PRIVATE_HEADERS = (
    "kernel/frame_ownership_test_internal.h",
    "tests/host/frame_allocator_test_internal.h",
    "tests/host/frame_ownership_test_internal.h",
)

FRAME_PATH_CLASSIFICATION_EQUIVALENTS = (
    (
        "kernel/frame_ownership.c",
        (
            "kernel/frame_ownership_validation.c",
            "kernel/frame_ownership_bootstrap.c",
            "kernel/frame_ownership_handoff.c",
            "kernel/frame_ownership_vm_claim.c",
            "kernel/frame_ownership_vm_freeze.c",
            "kernel/frame_ownership_vm_release.c",
            "kernel/frame_ownership_internal.h",
        ),
    ),
    (
        "kernel/frame_ownership_test.c",
        (
            "kernel/frame_ownership_test_fixture.c",
            "kernel/frame_ownership_test_lifecycle.c",
            "kernel/frame_ownership_test_handoff.c",
            "kernel/frame_ownership_test_internal.h",
        ),
    ),
    (
        "tests/host/frame_ownership_test.c",
        (
            "tests/host/frame_ownership_test_fixture.c",
            "tests/host/frame_ownership_test_bootstrap.c",
            "tests/host/frame_ownership_test_validation.c",
            "tests/host/frame_ownership_test_handoff.c",
            "tests/host/frame_ownership_test_model.c",
            "tests/host/frame_ownership_test_vm.c",
            "tests/host/frame_ownership_test_internal.h",
        ),
    ),
    (
        "tests/host/frame_allocator_test.c",
        (
            "tests/host/frame_allocator_test_cases.c",
            "tests/host/frame_allocator_model_test.c",
            "tests/host/frame_allocator_test_internal.h",
        ),
    ),
)

FINAL_SYMBOL_OWNERS = {
    "micros_frame_owner_make_kernel":
        "kernel/frame_ownership.c",
    "micros_frame_owner_make_process":
        "kernel/frame_ownership.c",
    "micros_frame_ownership_initialize":
        "kernel/frame_ownership.c",
    "micros_frame_ownership_lookup":
        "kernel/frame_ownership_validation.c",
    "micros_frame_ownership_count_process":
        "kernel/frame_ownership_validation.c",
    "micros_frame_ownership_validate":
        "kernel/frame_ownership_validation.c",
    "micros_frame_ownership_allocate":
        "kernel/frame_ownership_bootstrap.c",
    "micros_frame_ownership_release":
        "kernel/frame_ownership_bootstrap.c",
    "micros_frame_ownership_release_process_set":
        "kernel/frame_ownership_bootstrap.c",
    "micros_frame_ownership_prepare_handoff":
        "kernel/frame_ownership_handoff.c",
    "micros_frame_ownership_prepare_wired_process_user_set":
        "kernel/frame_ownership_handoff.c",
    "micros_frame_ownership_complete_handoff":
        "kernel/frame_ownership_handoff.c",
    "micros_frame_ownership_vm_claim_preflight":
        "kernel/frame_ownership_vm_claim.c",
    "micros_frame_ownership_vm_claim_commit_prevalidated":
        "kernel/frame_ownership_vm_claim.c",
    "micros_frame_ownership_vm_freeze_preflight":
        "kernel/frame_ownership_vm_freeze.c",
    "micros_frame_ownership_vm_freeze_commit_prevalidated":
        "kernel/frame_ownership_vm_freeze.c",
    "micros_frame_ownership_vm_release_transferable_preflight":
        "kernel/frame_ownership_vm_release.c",
    "micros_frame_ownership_vm_release_transferable_commit_prevalidated":
        "kernel/frame_ownership_vm_release.c",
    "micros_frame_ownership_vm_release_process_preflight":
        "kernel/frame_ownership_vm_release.c",
    "micros_frame_ownership_vm_release_process_commit_prevalidated":
        "kernel/frame_ownership_vm_release.c",
    "micros_frame_ownership_runtime_run_self_test":
        "kernel/frame_ownership_test.c",
}

INITIAL_SYMBOL_OWNER_MIGRATION = (
    (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_validation.c",
        "micros_frame_ownership_lookup",
    ),
    (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_validation.c",
        "micros_frame_ownership_count_process",
    ),
    (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_validation.c",
        "micros_frame_ownership_validate",
    ),
    (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_bootstrap.c",
        "micros_frame_ownership_allocate",
    ),
    (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_bootstrap.c",
        "micros_frame_ownership_release",
    ),
    (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_bootstrap.c",
        "micros_frame_ownership_release_process_set",
    ),
    (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_handoff.c",
        "micros_frame_ownership_prepare_handoff",
    ),
    (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_handoff.c",
        "micros_frame_ownership_prepare_wired_process_user_set",
    ),
    (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_handoff.c",
        "micros_frame_ownership_complete_handoff",
    ),
    (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_vm_claim.c",
        "micros_frame_ownership_vm_claim_preflight",
    ),
    (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_vm_claim.c",
        "micros_frame_ownership_vm_claim_commit_prevalidated",
    ),
    (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_vm_freeze.c",
        "micros_frame_ownership_vm_freeze_preflight",
    ),
    (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_vm_freeze.c",
        "micros_frame_ownership_vm_freeze_commit_prevalidated",
    ),
    (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_vm_release.c",
        "micros_frame_ownership_vm_release_transferable_preflight",
    ),
    (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_vm_release.c",
        "micros_frame_ownership_vm_release_transferable_commit_prevalidated",
    ),
    (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_vm_release.c",
        "micros_frame_ownership_vm_release_process_preflight",
    ),
    (
        "kernel/frame_ownership.c",
        "kernel/frame_ownership_vm_release.c",
        "micros_frame_ownership_vm_release_process_commit_prevalidated",
    ),
)

SYMBOL_OWNER_MIGRATION = tuple(
    migration
    for migration in INITIAL_SYMBOL_OWNER_MIGRATION
    if migration[1] not in {
        "kernel/frame_ownership_validation.c",
        "kernel/frame_ownership_bootstrap.c",
    }
)

INITIAL_LONG_FUNCTION_MIGRATION = (
    (
        "kernel/frame_ownership.c",
        "micros_frame_ownership_release_process_set",
    ),
    ("kernel/frame_ownership.c", "vm_process_set_preflight"),
    (
        "kernel/frame_ownership_test.c",
        "micros_frame_ownership_runtime_run_self_test",
    ),
    (
        "tests/host/frame_ownership_test.c",
        "test_constructs_exact_owner_records",
    ),
    (
        "tests/host/frame_ownership_test.c",
        "test_initializes_once_on_pristine_storage",
    ),
    (
        "tests/host/frame_ownership_test.c",
        "test_allocates_looks_up_and_releases_exact_owners",
    ),
    (
        "tests/host/frame_ownership_test.c",
        "test_atomically_releases_process_owned_sets",
    ),
    (
        "tests/host/frame_ownership_test.c",
        "test_stages_and_atomically_commits_handoff",
    ),
    (
        "tests/host/frame_ownership_test.c",
        "test_atomically_stages_complete_wired_set",
    ),
    (
        "tests/host/frame_ownership_test.c",
        "transition_model_to_handed_off",
    ),
    (
        "tests/host/frame_ownership_test.c",
        "test_matches_seeded_reference_model",
    ),
    (
        "tests/host/frame_ownership_test.c",
        "test_commits_post_handoff_vm_ownership_lifecycle",
    ),
    (
        "tests/host/frame_ownership_test.c",
        "test_post_handoff_vm_ownership_preflight_is_atomic",
    ),
    (
        "tests/host/frame_allocator_test.c",
        "test_rejects_invalid_inputs_without_changing_state",
    ),
    (
        "tests/host/frame_allocator_test.c",
        "test_matches_seeded_reference_model",
    ),
)

LONG_FUNCTION_MIGRATION = tuple(
    migration
    for migration in INITIAL_LONG_FUNCTION_MIGRATION
    if (
        migration[0] != "kernel/frame_ownership.c"
        and migration[0] != "tests/host/frame_ownership_test.c"
        and not migration[0].startswith("tests/host/frame_allocator_")
    )
)

STRUCTURAL_TEST_PATH = "tests/host/test_frame_ownership_source_layout.py"

OWNERSHIP_CASE_ORDER = (
    "constructs exact owner records",
    "initializes once on pristine storage",
    "rejects invalid raw owner records",
    "allocates, looks up, and releases exact owners",
    "rejects geometry changes before mutation",
    "rolls back defensive post-allocation failure",
    "atomically releases process-owned sets",
    "stages and atomically commits handoff",
    "atomically stages complete wired set",
    "rejects independent corruption",
    "operates at maximum managed capacity",
    "matches seeded reference model",
    "commits post-handoff VM ownership lifecycle",
    "keeps post-handoff VM preflight atomic",
)

ALLOCATOR_CASE_ORDER = (
    "normalizes and subtracts ranges",
    "allocates, releases, and reuses lowest frame",
    "merges reservations and handles full removal",
    "rejects invalid inputs without state changes",
    "accepts capacity boundaries",
    "matches seeded reference model",
)


def cmake_command(text: str, command: str, first_argument: str) -> str:
    match = re.search(
        rf"\b{re.escape(command)}\s*\(\s*{re.escape(first_argument)}\b",
        text,
    )
    if match is None:
        raise AssertionError(
            f"missing {command} command for {first_argument}"
        )

    open_index = text.find("(", match.start())
    depth = 0
    for index in range(open_index, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                return text[match.start() : index + 1]

    raise AssertionError(
        f"unterminated {command} command for {first_argument}"
    )


def cmake_if_blocks(text: str, condition: str) -> tuple[str, ...]:
    pattern = re.compile(
        rf"\bif\s*\(\s*{re.escape(condition)}\s*\)"
    )
    blocks = []
    offset = 0

    while True:
        match = pattern.search(text, offset)
        if match is None:
            break
        depth = 1
        for token in re.finditer(
            r"\b(?P<command>if|endif)\s*\(",
            text[match.end() :],
        ):
            if token.group("command") == "if":
                depth += 1
            else:
                depth -= 1
                if depth == 0:
                    end = match.end() + token.end()
                    blocks.append(text[match.start() : end])
                    offset = end
                    break
        else:
            raise AssertionError(
                f"unterminated if block for {condition}"
            )

    if not blocks:
        raise AssertionError(f"missing if block for {condition}")
    return tuple(blocks)


def c_lexical_text(text: str) -> str:
    output = list(text)
    index = 0

    while index < len(text):
        if text.startswith("//", index):
            end = text.find("\n", index + 2)
            end = len(text) if end < 0 else end
            for selected in range(index, end):
                output[selected] = " "
            index = end
            continue
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            if end < 0:
                raise AssertionError("unterminated C comment")
            for selected in range(index, end + 2):
                if output[selected] != "\n":
                    output[selected] = " "
            index = end + 2
            continue
        if text[index] in ('"', "'"):
            quote = text[index]
            output[index] = " "
            index += 1
            while index < len(text):
                if text[index] == "\\":
                    output[index] = " "
                    if index + 1 < len(text):
                        if output[index + 1] != "\n":
                            output[index + 1] = " "
                    index += 2
                    continue
                if text[index] == quote:
                    output[index] = " "
                    index += 1
                    break
                if output[index] != "\n":
                    output[index] = " "
                index += 1
            else:
                raise AssertionError("unterminated C literal")
            continue
        index += 1

    return "".join(output)


def matching_open_parenthesis(text: str, close_index: int) -> int:
    depth = 0

    for index in range(close_index, -1, -1):
        if text[index] == ")":
            depth += 1
        elif text[index] == "(":
            depth -= 1
            if depth == 0:
                return index

    raise AssertionError("unterminated C parameter list")


def c_signature_start(text: str, segment_start: int, name_index: int) -> int:
    prefix = text[segment_start:name_index]
    lines = prefix.splitlines(keepends=True)
    offset = segment_start
    candidates = []

    for line in lines:
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            candidates.append(offset)
        elif stripped.startswith("#") or not stripped:
            candidates = []
        offset += len(line)

    return candidates[0] if candidates else name_index


def c_function_definitions(text: str) -> dict[str, int]:
    lexical = c_lexical_text(text)
    definitions = {}
    depth = 0
    segment_start = 0
    index = 0

    while index < len(lexical):
        selected = lexical[index]
        if selected == "{" and depth == 0:
            declaration = lexical[segment_start:index].rstrip()
            close_parenthesis = len(declaration) - 1
            if close_parenthesis >= 0 and declaration.endswith(")"):
                open_parenthesis = matching_open_parenthesis(
                    declaration,
                    close_parenthesis,
                )
                before = declaration[:open_parenthesis].rstrip()
                match = re.search(
                    r"([A-Za-z_][A-Za-z0-9_]*)$",
                    before,
                )
                if match is not None and "=" not in declaration:
                    name = match.group(1)
                    if name not in {"if", "for", "while", "switch"}:
                        body_depth = 1
                        body_end = index + 1
                        while body_end < len(lexical) and body_depth != 0:
                            if lexical[body_end] == "{":
                                body_depth += 1
                            elif lexical[body_end] == "}":
                                body_depth -= 1
                            body_end += 1
                        if body_depth != 0:
                            raise AssertionError(
                                f"unterminated body for {name}"
                            )
                        name_index = (
                            segment_start
                            + open_parenthesis
                            - len(match.group(0))
                        )
                        start = c_signature_start(
                            lexical,
                            segment_start,
                            name_index,
                        )
                        definitions[name] = (
                            text.count("\n", start, body_end - 1) + 1
                        )
                        index = body_end
                        segment_start = body_end
                        continue
            depth = 1
        elif selected == "{":
            depth += 1
        elif selected == "}":
            if depth == 0:
                raise AssertionError("unexpected C closing brace")
            depth -= 1
            if depth == 0:
                segment_start = index + 1
        elif selected == ";" and depth == 0:
            segment_start = index + 1
        index += 1

    return definitions


def all_source_set_paths() -> tuple[str, ...]:
    return tuple(
        dict.fromkeys(
            path
            for sources in SOURCE_SETS.values()
            for path in sources
        )
    )


def production_sources() -> tuple[str, ...]:
    return SOURCE_SETS["MICROS_FRAME_OWNERSHIP_SOURCES"]


def symbol_scan_sources() -> tuple[str, ...]:
    return (
        *production_sources(),
        *SOURCE_SETS["MICROS_FRAME_OWNERSHIP_QEMU_TEST_SOURCES"],
    )


def static_definition_paths(
    sources: tuple[str, ...],
    name: str,
) -> list[str]:
    pattern = re.compile(
        rf"(?ms)^static\b[^;{{}}]*?\b{re.escape(name)}\b"
        r"[^;{}]*;"
    )
    matches = []

    for relative_path in sources:
        path = REPOSITORY_ROOT / relative_path
        if (
            path.is_file()
            and pattern.search(path.read_text(encoding="utf-8"))
        ):
            matches.append(relative_path)

    return matches


class FrameOwnershipSourceLayoutTest(unittest.TestCase):
    def test_expected_sources_and_private_headers_exist(self) -> None:
        required = (*all_source_set_paths(), *PRIVATE_HEADERS)
        missing = [
            path
            for path in required
            if not (REPOSITORY_ROOT / path).is_file()
        ]

        self.assertEqual([], missing)

    def test_cmake_source_sets_have_exact_ordered_membership(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")

        for source_set_name, expected_sources in SOURCE_SETS.items():
            with self.subTest(source_set=source_set_name):
                source_set = cmake_command(
                    cmake,
                    "set",
                    source_set_name,
                )
                listed_sources = tuple(
                    re.findall(
                        r"(?:kernel|tests/host)/"
                        r"[A-Za-z0-9_]+\.c",
                        source_set,
                    )
                )
                self.assertEqual(expected_sources, listed_sources)

        for source in all_source_set_paths():
            self.assertEqual(1, cmake.count(source), source)

    def test_consumers_expand_exact_source_sets_once(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")
        consumers = {
            "frame-allocator": (
                cmake_command(cmake, "add_executable", "frame_allocator_test"),
                ("MICROS_FRAME_ALLOCATOR_HOST_TEST_SOURCES",),
            ),
            "frame-ownership": (
                cmake_command(cmake, "add_executable", "frame_ownership_test"),
                (
                    "MICROS_FRAME_OWNERSHIP_SOURCES",
                    "MICROS_FRAME_OWNERSHIP_HOST_TEST_SOURCES",
                ),
            ),
            "vm-bootstrap": (
                cmake_command(cmake, "add_executable", "vm_bootstrap_test"),
                ("MICROS_FRAME_OWNERSHIP_SOURCES",),
            ),
            "pm": (
                cmake_command(cmake, "add_executable", "pm_test"),
                ("MICROS_FRAME_OWNERSHIP_SOURCES",),
            ),
            "vm-core": (
                cmake_command(cmake, "add_executable", "vm_core_test"),
                ("MICROS_FRAME_OWNERSHIP_SOURCES",),
            ),
            "micros": (
                cmake_command(cmake, "add_executable", "micros"),
                ("MICROS_FRAME_OWNERSHIP_SOURCES",),
            ),
            "qemu-test": (
                "\n".join(
                    cmake_if_blocks(
                        cmake,
                        "MICROS_BUILD_FRAME_OWNERSHIP_TEST",
                    )
                ),
                ("MICROS_FRAME_OWNERSHIP_QEMU_TEST_SOURCES",),
            ),
        }

        for consumer, (block, selected_sets) in consumers.items():
            for source_set_name in SOURCE_SETS:
                with self.subTest(
                    consumer=consumer,
                    source_set=source_set_name,
                ):
                    self.assertEqual(
                        int(source_set_name in selected_sets),
                        block.count(f"${{{source_set_name}}}"),
                    )

    def test_private_headers_contain_declarations_only(self) -> None:
        definitions = {}

        for relative_path in PRIVATE_HEADERS:
            path = REPOSITORY_ROOT / relative_path
            if not path.is_file():
                continue
            selected = c_function_definitions(
                path.read_text(encoding="utf-8")
            )
            if selected:
                definitions[relative_path] = selected

        self.assertEqual({}, definitions)

    def test_production_sources_do_not_include_test_private_headers(
        self,
    ) -> None:
        violations = []

        for relative_path in production_sources():
            path = REPOSITORY_ROOT / relative_path
            if not path.is_file():
                continue
            text = path.read_text(encoding="utf-8")
            for private_path in TEST_PRIVATE_HEADERS:
                if (
                    private_path in text
                    or Path(private_path).name in text
                ):
                    violations.append((relative_path, private_path))

        self.assertEqual([], violations)

    def test_sources_do_not_textually_include_c_files(self) -> None:
        violations = []
        include = re.compile(
            r"(?m)^\s*#\s*include\s*[<\"][^>\"]+\.c[>\"]"
        )

        for relative_path in (
            *all_source_set_paths(),
            *PRIVATE_HEADERS,
        ):
            path = REPOSITORY_ROOT / relative_path
            if (
                path.is_file()
                and include.search(path.read_text(encoding="utf-8"))
            ):
                violations.append(relative_path)

        self.assertEqual([], violations)

    def test_symbol_owners_follow_exact_migration_map(self) -> None:
        initial = set(INITIAL_SYMBOL_OWNER_MIGRATION)
        migration = set(SYMBOL_OWNER_MIGRATION)
        by_symbol = {}
        definitions = {}

        self.assertEqual(
            len(SYMBOL_OWNER_MIGRATION),
            len(migration),
            "symbol-owner migration contains duplicates",
        )
        self.assertTrue(
            migration <= initial,
            "symbol-owner migration may only shrink",
        )

        for current, final, symbol in migration:
            self.assertEqual(FINAL_SYMBOL_OWNERS[symbol], final)
            self.assertNotEqual(current, final)
            self.assertNotIn(symbol, by_symbol)
            by_symbol[symbol] = current

        for relative_path in symbol_scan_sources():
            path = REPOSITORY_ROOT / relative_path
            if not path.is_file():
                continue
            for symbol in c_function_definitions(
                path.read_text(encoding="utf-8")
            ):
                definitions.setdefault(symbol, []).append(relative_path)

        for symbol, final_owner in FINAL_SYMBOL_OWNERS.items():
            with self.subTest(symbol=symbol):
                expected = by_symbol.get(symbol, final_owner)
                self.assertEqual(
                    [expected],
                    definitions.get(symbol, []),
                )

    def test_split_tests_do_not_duplicate_mutable_state(self) -> None:
        families = (
            (
                SOURCE_SETS["MICROS_FRAME_OWNERSHIP_HOST_TEST_SOURCES"],
                (
                    "allocator",
                    "alternate_allocator",
                    "ownership",
                    "ownership_snapshot",
                    "allocator_snapshot",
                    "release_bitmap",
                ),
            ),
            (
                SOURCE_SETS["MICROS_FRAME_OWNERSHIP_QEMU_TEST_SOURCES"],
                (
                    "ownership_state_snapshot",
                    "allocator_state_snapshot",
                    "object_state_snapshot",
                    "substitute_objects",
                    "substitute_state_snapshot",
                ),
            ),
        )

        for sources, names in families:
            for name in names:
                with self.subTest(source=sources[0], state=name):
                    self.assertLessEqual(
                        len(static_definition_paths(sources, name)),
                        1,
                    )

        contexts = (
            (
                "frame_ownership_test_context",
                "tests/host/frame_ownership_test_fixture.c",
                SOURCE_SETS["MICROS_FRAME_OWNERSHIP_HOST_TEST_SOURCES"],
            ),
            (
                "micros_frame_ownership_test_context",
                "kernel/frame_ownership_test_fixture.c",
                SOURCE_SETS["MICROS_FRAME_OWNERSHIP_QEMU_TEST_SOURCES"],
            ),
        )
        for context_type, owner, sources in contexts:
            pattern = re.compile(
                rf"(?m)^static\s+struct\s+{context_type}\s+"
                r"[A-Za-z_][A-Za-z0-9_]*\s*;"
            )
            observed = []
            for relative_path in sources:
                path = REPOSITORY_ROOT / relative_path
                if (
                    path.is_file()
                    and pattern.search(path.read_text(encoding="utf-8"))
                ):
                    observed.append(relative_path)
            if observed:
                self.assertEqual([owner], observed)

    def test_evidence_identities_and_sequences_remain_stable(self) -> None:
        cmake = CMAKE_PATH.read_text(encoding="utf-8")
        runner = (
            REPOSITORY_ROOT / "tools/run_qemu_smoke.py"
        ).read_text(encoding="utf-8")
        ownership_entry = (
            REPOSITORY_ROOT / "tests/host/frame_ownership_test.c"
        ).read_text(encoding="utf-8")
        allocator_entry = (
            REPOSITORY_ROOT / "tests/host/frame_allocator_test.c"
        ).read_text(encoding="utf-8")
        all_host_text = "\n".join(
            (REPOSITORY_ROOT / path).read_text(encoding="utf-8")
            for path in (
                *SOURCE_SETS["MICROS_FRAME_ALLOCATOR_HOST_TEST_SOURCES"],
                *SOURCE_SETS["MICROS_FRAME_OWNERSHIP_HOST_TEST_SOURCES"],
                "tests/host/frame_allocator_test_internal.h",
                "tests/host/frame_ownership_test_internal.h",
            )
            if (REPOSITORY_ROOT / path).is_file()
        )
        marker = (
            "MICROS_FRAME_OWNERSHIP_TEST_PASS "
            "stale=rejected release=blocked "
            "handoff=atomic invariants=preserved"
        )

        self.assertEqual(1, cmake.count("NAME frame-allocator"))
        self.assertEqual(1, cmake.count("NAME frame-ownership"))
        self.assertEqual(1, cmake.count("test-qemu-frame-ownership"))
        self.assertEqual(1, cmake.count(marker))
        for token in marker.split():
            self.assertIn(token, runner)
        self.assertRegex(
            cmake,
            r"--require-frame-ownership-test-report"
            r"(?s:.*?)--timeout\s+8",
        )

        ownership_positions = [
            ownership_entry.index(name)
            for name in OWNERSHIP_CASE_ORDER
        ]
        allocator_positions = [
            allocator_entry.index(name)
            for name in ALLOCATOR_CASE_ORDER
        ]
        self.assertEqual(sorted(ownership_positions), ownership_positions)
        self.assertEqual(sorted(allocator_positions), allocator_positions)

        for token in (
            "MODEL_STEPS = 4096",
            "MODEL_HANDOFF_STEP = 3072",
            "MODEL_FULL_VALIDATION_INTERVAL = 64",
            "UINT32_C(0x20f0a11c)",
            "MODEL_STEPS = 2000",
            "UINT32_C(0x17a110c)",
        ):
            self.assertEqual(1, all_host_text.count(token), token)

    def test_validation_plan_inherits_exact_path_classification(
        self,
    ) -> None:
        baselines = {
            baseline
            for baseline, _ in FRAME_PATH_CLASSIFICATION_EQUIVALENTS
        }
        observed = tuple(
            item
            for item in validation_plan.PATH_CLASSIFICATION_EQUIVALENTS
            if item[0] in baselines
        )

        self.assertEqual(
            FRAME_PATH_CLASSIFICATION_EQUIVALENTS,
            observed,
        )
        for baseline, equivalents in FRAME_PATH_CLASSIFICATION_EQUIVALENTS:
            for equivalent in equivalents:
                for tier in ("fast", "pr"):
                    with self.subTest(
                        baseline=baseline,
                        equivalent=equivalent,
                        tier=tier,
                    ):
                        self.assertEqual(
                            validation_plan.plan(
                                [baseline],
                                tier,
                                "main",
                            ),
                            validation_plan.plan(
                                [equivalent],
                                tier,
                                "main",
                            ),
                        )

        self.assertIn(
            STRUCTURAL_TEST_PATH,
            validation_plan.HOST_UNIT_ONLY_INPUTS,
        )
        self.assertEqual(
            [
                validation_plan.UNIT_FAST,
                *validation_plan.diff_commands("main"),
            ],
            validation_plan.plan(
                [STRUCTURAL_TEST_PATH],
                "fast",
                "main",
            ),
        )

    def test_long_function_migration_matches_review_signal(self) -> None:
        initial = set(INITIAL_LONG_FUNCTION_MIGRATION)
        migration = set(LONG_FUNCTION_MIGRATION)
        oversized = {}

        self.assertEqual(
            len(LONG_FUNCTION_MIGRATION),
            len(migration),
            "long-function migration contains duplicates",
        )
        self.assertTrue(
            migration <= initial,
            "long-function migration may only shrink",
        )

        for relative_path in all_source_set_paths():
            path = REPOSITORY_ROOT / relative_path
            if not path.is_file():
                continue
            for function_name, line_count in c_function_definitions(
                path.read_text(encoding="utf-8")
            ).items():
                if line_count > FUNCTION_REVIEW_LINE_LIMIT:
                    oversized[(relative_path, function_name)] = line_count

        self.assertEqual(migration, set(oversized), oversized)


if __name__ == "__main__":
    unittest.main()
