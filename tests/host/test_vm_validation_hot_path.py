import unittest
from pathlib import Path

from tests.host.test_frame_validation_hot_path import (
    ast_walk,
    call_targets,
    clang_ast,
    clang_preprocessed,
    function_body,
    function_decl,
    function_source,
    function_token_digest,
    loop_body,
    referenced_names,
)


ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = ROOT / "servers/vm/vm_validation.c"
EXPECTED_TOKEN_DIGESTS = {
    "vm_validation_digest_bytes":
        "b7107877ba9a2c632a2c88a7e07f00635e8d1f49ff74ed45726b61166e3944fc",
    "vm_validation_digest_zero_bytes":
        "eb1e24c3ce1d1d7f0bdc3a522b987ca216a81af86deec63edc3c0e097088f549",
    "vm_validation_digest_active_frame_states":
        "19c10c06765c4ba968667309a9f5f9700865cdb483ade3b4d2e2f6563e5dec46",
    "vm_validation_initial_free_tail_is_zero":
        "f2dd657c5fea054551ded2130c941c416d24f84196c338e89cd484371dd96b0e",
    "vm_core_boot_database_is_immutable":
        "f09663c7ca98dfaed5f67e888595b230d6aa32305ed2900427e9e268bf87128e",
}


def member_subscripts(node, member_name):
    return [
        child
        for child in ast_walk(node)
        if child.get("kind") == "ArraySubscriptExpr"
        and any(
            nested.get("kind") == "MemberExpr"
            and nested.get("name") == member_name
            for nested in ast_walk(child)
        )
    ]


def named_variables(node, name):
    return [
        child
        for child in ast_walk(node)
        if child.get("kind") == "VarDecl"
        and child.get("name") == name
    ]


def top_level_loops(function):
    return [
        child
        for child in function_body(function).get("inner", ())
        if child.get("kind") in {"ForStmt", "WhileStmt", "DoStmt"}
    ]


def assert_reviewed_token_identities(preprocessed):
    for name, expected in EXPECTED_TOKEN_DIGESTS.items():
        if function_token_digest(preprocessed, name) != expected:
            raise AssertionError(
                f"{name} differs from the reviewed work-shape identity"
            )


def assert_count_guard_dominates(immutable):
    statements = function_body(immutable).get("inner", ())
    guard_index = next(
        (
            index
            for index, statement in enumerate(statements)
            if statement.get("kind") == "IfStmt"
            and {
                "managed_frame_count",
                "MICROS_VM_MAX_MANAGED_FRAMES",
            } <= referenced_names(statement)
        ),
        None,
    )
    if guard_index is None:
        raise AssertionError("managed-frame count guard is missing")
    operators = {
        node.get("opcode")
        for node in ast_walk(statements[guard_index])
        if node.get("kind") == "BinaryOperator"
    }
    if not {"==", ">"} <= operators:
        raise AssertionError("managed-frame count guard is incomplete")
    for statement in statements[:guard_index]:
        if (
            any(
                node.get("kind") in {
                    "ArraySubscriptExpr",
                    "CallExpr",
                    "ForStmt",
                    "WhileStmt",
                    "DoStmt",
                }
                for node in ast_walk(statement)
            )
            or "managed_frame_count" in referenced_names(statement)
            and any(
                node.get("kind") == "BinaryOperator"
                and node.get("opcode") in {"-", "/", "%"}
                for node in ast_walk(statement)
            )
        ):
            raise AssertionError(
                "managed-frame count is consumed before its guard"
            )


def assert_zero_advance(tree):
    function = function_decl(tree, "vm_validation_digest_zero_bytes")
    if (
        function.get("storageClass") != "static"
        or not function.get("type", {}).get("qualType", "").startswith(
            "uint64_t ("
        )
    ):
        raise AssertionError("zero advance declaration is unsupported")
    loops = top_level_loops(function)
    if len(loops) != 1 or loops[0].get("kind") != "WhileStmt":
        raise AssertionError("zero advance must own one while loop")
    loop = loops[0]
    shifts = [
        node
        for node in ast_walk(loop)
        if node.get("kind") == "CompoundAssignOperator"
        and node.get("opcode") == ">>="
        and "zero_count" in referenced_names(node)
    ]
    if len(shifts) != 1:
        raise AssertionError("zero advance does not halve its count")
    if any(
        node.get("kind") == "UnaryOperator"
        and node.get("opcode") in {"--", "++"}
        and "zero_count" in referenced_names(node)
        for node in ast_walk(loop)
    ):
        raise AssertionError("zero advance changes its count linearly")
    if call_targets(function):
        raise AssertionError("zero advance calls an unreviewed helper")


def assert_active_scan(tree):
    function = function_decl(
        tree,
        "vm_validation_digest_active_frame_states",
    )
    if (
        function.get("storageClass") != "static"
        or not function.get("type", {}).get("qualType", "").startswith(
            "uint64_t ("
        )
    ):
        raise AssertionError("active digest declaration is unsupported")
    loops = top_level_loops(function)
    if len(loops) != 1 or loops[0].get("kind") != "ForStmt":
        raise AssertionError("active digest must own one outer word loop")
    outer = loops[0]
    inner_loops = [
        node
        for node in loop_body(outer).get("inner", ())
        if node.get("kind") == "ForStmt"
    ]
    if len(inner_loops) != 1:
        raise AssertionError("active digest must own one inner frame loop")
    inner = inner_loops[0]
    bitmap = member_subscripts(function, "initial_free_bitmap")
    if len(bitmap) != 1:
        raise AssertionError("active digest must load one bitmap word")
    words = named_variables(function, "initial_free_word")
    if (
        len(words) != 1
        or words[0].get("type", {}).get("qualType") != "uint64_t"
        or member_subscripts(words[0], "initial_free_bitmap") != bitmap
        or referenced_names(bitmap[0]) != {"state", "word_index"}
    ):
        raise AssertionError("active bitmap word is not a scalar word load")
    if member_subscripts(inner, "initial_free_bitmap"):
        raise AssertionError("active inner loop reloads the bitmap")
    if not {"initial_free_word", "bit"} <= referenced_names(inner):
        raise AssertionError("active inner loop does not consume the word")
    frame_states = member_subscripts(inner, "frame_states")
    if (
        len(frame_states) != 1
        or referenced_names(frame_states[0]) != {"state", "frame_index"}
    ):
        raise AssertionError("active frame-state access is unsupported")
    if call_targets(function):
        raise AssertionError("active digest calls an unreviewed helper")


def assert_inactive_bitmap_scan(tree):
    function = function_decl(
        tree,
        "vm_validation_initial_free_tail_is_zero",
    )
    if (
        function.get("storageClass") != "static"
        or not function.get("type", {}).get("qualType", "").startswith(
            ("bool (", "_Bool (")
        )
    ):
        raise AssertionError("inactive bitmap declaration is unsupported")
    loops = top_level_loops(function)
    if len(loops) != 1 or loops[0].get("kind") != "ForStmt":
        raise AssertionError("inactive bitmap must own one full-word loop")
    loop = loops[0]
    subscripts = member_subscripts(function, "initial_free_bitmap")
    if len(subscripts) != 2:
        raise AssertionError("inactive bitmap access is incomplete")
    first_words = named_variables(function, "first_word")
    boundary_bits = named_variables(function, "boundary_bit")
    if (
        len(first_words) != 1
        or referenced_names(first_words[0]) != {"managed_frame_count"}
        or len(boundary_bits) != 1
        or referenced_names(boundary_bits[0]) != {"managed_frame_count"}
    ):
        raise AssertionError("inactive bitmap start is not count derived")
    start_operators = {
        node.get("opcode")
        for node in ast_walk(first_words[0])
        if node.get("kind") == "BinaryOperator"
    }
    boundary_operators = {
        node.get("opcode")
        for node in ast_walk(boundary_bits[0])
        if node.get("kind") == "BinaryOperator"
    }
    if "/" not in start_operators or "%" not in boundary_operators:
        raise AssertionError("inactive bitmap boundary arithmetic is missing")
    if not {
        "first_word",
        "MICROS_VM_INITIAL_FREE_BITMAP_WORDS",
        "state",
    } <= referenced_names(loop):
        raise AssertionError("inactive bitmap loop bounds are unsupported")
    increments = [
        node
        for node in ast_walk(loop)
        if node.get("kind") == "UnaryOperator"
        and node.get("opcode") == "++"
        and "first_word" in referenced_names(node)
    ]
    if len(increments) != 1:
        raise AssertionError("inactive bitmap loop is not contiguous")
    if call_targets(function):
        raise AssertionError("inactive bitmap scan calls a helper")


def assert_immutable_orchestration(tree):
    immutable = function_decl(
        tree,
        "vm_core_boot_database_is_immutable",
    )
    if (
        immutable.get("storageClass") != "static"
        or not immutable.get("type", {}).get("qualType", "").startswith(
            ("bool (", "_Bool (")
        )
    ):
        raise AssertionError("immutability declaration is unsupported")
    assert_count_guard_dominates(immutable)
    if top_level_loops(immutable):
        raise AssertionError("immutability orchestration owns a linear loop")
    expected_calls = {
        "vm_validation_digest_bytes",
        "vm_validation_digest_zero_bytes",
        "vm_validation_digest_active_frame_states",
        "vm_validation_initial_free_tail_is_zero",
        "vm_core_bytes_are_zero",
    }
    if call_targets(immutable) != expected_calls:
        raise AssertionError("immutability calls differ from reviewed owners")
    if "vm_core_initial_frame_is_free" in call_targets(immutable):
        raise AssertionError("immutability calls the per-frame bitmap helper")
    if len(member_subscripts(immutable, "frame_states")) != 1:
        raise AssertionError("inactive frame-state tail check is missing")


def assert_bounded_vm_validation(text):
    preprocessed = clang_preprocessed(text)
    assert_reviewed_token_identities(preprocessed)
    tree = clang_ast(text)
    assert_zero_advance(tree)
    assert_active_scan(tree)
    assert_inactive_bitmap_scan(tree)
    assert_immutable_orchestration(tree)
    if any(
        node.get("kind") in {"GotoStmt", "LabelStmt"}
        for name in EXPECTED_TOKEN_DIGESTS
        for node in ast_walk(function_decl(tree, name))
    ):
        raise AssertionError("VM validation uses unstructured control flow")


class VmValidationHotPathTests(unittest.TestCase):
    def test_boot_database_validation_has_bounded_work_shape(self):
        assert_bounded_vm_validation(
            VALIDATOR.read_text(encoding="utf-8")
        )

    def assert_mutation_rejected(self, changed, pattern):
        with self.assertRaisesRegex(AssertionError, pattern):
            assert_bounded_vm_validation(changed)

    def test_per_frame_bitmap_helper_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        active = function_source(
            text,
            "vm_validation_digest_active_frame_states",
        )
        changed_active = active.replace(
            "(initial_free_word & bit) != 0",
            "vm_core_initial_frame_is_free(state, frame_index)",
            1,
        )
        self.assert_mutation_rejected(
            text.replace(active, changed_active, 1),
            "reviewed work-shape identity|unreviewed helper",
        )

    def test_per_frame_bitmap_reload_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        active = function_source(
            text,
            "vm_validation_digest_active_frame_states",
        )
        changed_active = active.replace(
            "(initial_free_word & bit) != 0",
            "(\n"
            "                state->initial_free_bitmap[frame_index / 64]\n"
            "                & bit\n"
            "            ) != 0",
            1,
        )
        self.assert_mutation_rejected(
            text.replace(active, changed_active, 1),
            "reviewed work-shape identity|bitmap word|reloads",
        )

    def test_missing_inactive_frame_tail_check_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        changed = text.replace(
            "vm_core_bytes_are_zero(\n"
            "            &state->boot_info->frame_states[active_frame_count],\n"
            "            inactive_frame_count\n"
            "        )",
            "true",
            1,
        )
        self.assert_mutation_rejected(
            changed,
            "reviewed work-shape identity|tail check",
        )

    def test_shifted_first_inactive_word_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        changed = text.replace(
            "managed_frame_count / 64;",
            "managed_frame_count / 64 + 1;",
            1,
        )
        self.assert_mutation_rejected(
            changed,
            "reviewed work-shape identity|start",
        )

    def test_skipped_inactive_word_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        tail = function_source(
            text,
            "vm_validation_initial_free_tail_is_zero",
        )
        changed_tail = tail.replace(
            "++first_word\n"
            "    )",
            "first_word += 2\n"
            "    )",
            1,
        )
        self.assert_mutation_rejected(
            text.replace(tail, changed_tail, 1),
            "reviewed work-shape identity|contiguous",
        )

    def test_early_inactive_word_end_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        changed = text.replace(
            "first_word < MICROS_VM_INITIAL_FREE_BITMAP_WORDS;",
            "first_word + 1 < MICROS_VM_INITIAL_FREE_BITMAP_WORDS;",
            1,
        )
        self.assert_mutation_rejected(
            changed,
            "reviewed work-shape identity|bounds",
        )

    def test_linear_zero_advance_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        changed = text.replace(
            "zero_count >>= 1;",
            "--zero_count;",
            1,
        )
        self.assert_mutation_rejected(
            changed,
            "reviewed work-shape identity|halve|linearly",
        )

    def test_late_managed_count_guard_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        immutable = function_source(
            text,
            "vm_core_boot_database_is_immutable",
        )
        guard = (
            "    if (\n"
            "        managed_frame_count == 0\n"
            "        || managed_frame_count > MICROS_VM_MAX_MANAGED_FRAMES\n"
            "    ) {\n"
            "        return false;\n"
            "    }\n"
        )
        changed_immutable = immutable.replace(guard, "", 1).replace(
            "    return digest == state->boot_summary.digest;",
            guard + "    return digest == state->boot_summary.digest;",
            1,
        )
        self.assert_mutation_rejected(
            text.replace(immutable, changed_immutable, 1),
            "reviewed work-shape identity|consumed before",
        )
