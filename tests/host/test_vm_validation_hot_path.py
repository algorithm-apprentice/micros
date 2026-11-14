import unittest
from pathlib import Path

from tests.host.test_frame_validation_hot_path import (
    ast_walk,
    call_targets,
    clang_ast,
    function_body,
    function_decl,
    function_source,
    loop_body,
    referenced_names,
)


ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = ROOT / "servers/vm/vm_validation.c"
WORK_HELPERS = (
    "vm_validation_digest_bytes", "vm_validation_digest_zero_bytes",
    "vm_validation_digest_active_frame_states", "vm_validation_free_tail_is_zero",
    "vm_core_boot_database_is_immutable",
)


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


def scalar_expression(node):
    kind = node.get("kind")
    children = node.get("inner", ())
    if kind in ("ImplicitCastExpr", "CStyleCastExpr", "ParenExpr"):
        return scalar_expression(children[0])
    if kind == "DeclRefExpr":
        return ("name", node["referencedDecl"]["name"])
    if kind == "IntegerLiteral":
        return ("integer", int(node["value"]))
    if kind == "BinaryOperator":
        return (node["opcode"], *(scalar_expression(child) for child in children))
    raise AssertionError("unsupported bitmap boundary expression")


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
        raise AssertionError("active digest must own one frame loop")
    loop = loops[0]
    if any(node.get("kind") in {"ForStmt", "WhileStmt", "DoStmt"} for node in ast_walk(loop_body(loop))):
        raise AssertionError("active digest contains a nested scan")
    indexes = named_variables(function, "frame_index")
    conditions = [node for node in loop.get("inner", ()) if node.get("kind") == "BinaryOperator"]
    increments = [
        node for node in loop.get("inner", ())
        if node.get("kind") == "UnaryOperator" and node.get("opcode") == "++"
    ]
    if (
        len(indexes) != 1 or scalar_expression(indexes[0]["inner"][-1]) != ("integer", 0)
        or len(conditions) != 1
        or scalar_expression(conditions[0]) != ("<", ("name", "frame_index"), ("name", "managed_frame_count"))
        or len(increments) != 1 or referenced_names(increments[0]) != {"frame_index"}
    ):
        raise AssertionError("active frame bounds are incomplete")
    if member_subscripts(function, "free_bitmap"):
        raise AssertionError("active digest uses current availability as original provenance")
    frame_states = member_subscripts(function, "frame_states")
    local = named_variables(function, "local")
    if (
        len(frame_states) != 1
        or referenced_names(frame_states[0]) != {"state", "frame_index"}
        or len(local) != 1 or local[0].get("type", {}).get("qualType") != "uint8_t"
        or member_subscripts(local[0], "frame_states") != frame_states
    ):
        raise AssertionError("active frame-state scalar load is unsupported")
    choices = [node for node in ast_walk(function) if node.get("kind") == "ConditionalOperator"]
    if len(choices) != 1 or tuple(scalar_expression(node) for node in choices[0]["inner"]) != (
        ("&&", (">=", ("name", "local"), ("name", "MICROS_VM_LOCAL_FRAME_TRANSFERABLE")),
         ("<=", ("name", "local"), ("name", "MICROS_VM_LOCAL_FRAME_PROCESS_PAGE_TABLE"))),
        ("name", "MICROS_VM_FRAME_FREE"), ("name", "local"),
    ):
        raise AssertionError("active original-class reconstruction changed")
    if call_targets(function):
        raise AssertionError("active digest calls an unreviewed helper")


def assert_inactive_bitmap_scan(tree):
    function = function_decl(
        tree,
        "vm_validation_free_tail_is_zero",
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
    subscripts = member_subscripts(function, "free_bitmap")
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
    if (
        scalar_expression(first_words[0]["inner"][-1])
        != ("/", ("name", "managed_frame_count"), ("integer", 64))
        or scalar_expression(boundary_bits[0]["inner"][-1])
        != ("%", ("name", "managed_frame_count"), ("integer", 64))
    ):
        raise AssertionError("inactive bitmap start is shifted")
    conditions = [
        node for node in loop.get("inner", ())
        if node.get("kind") == "BinaryOperator" and node.get("opcode") == "<"
    ]
    if len(conditions) != 1 or scalar_expression(conditions[0]) != (
        "<", ("name", "first_word"), ("name", "MICROS_VM_FREE_BITMAP_WORDS"),
    ):
        raise AssertionError("inactive bitmap loop bounds are incomplete")
    if not {
        "first_word",
        "MICROS_VM_FREE_BITMAP_WORDS",
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
        "vm_validation_free_tail_is_zero",
        "vm_core_bytes_are_zero",
    }
    if call_targets(immutable) != expected_calls:
        raise AssertionError("immutability calls differ from reviewed owners")
    if "vm_core_initial_frame_is_free" in call_targets(immutable):
        raise AssertionError("immutability calls the per-frame bitmap helper")
    if len(member_subscripts(immutable, "frame_states")) != 1:
        raise AssertionError("inactive frame-state tail check is missing")


def assert_bounded_vm_validation(text):
    tree = clang_ast(text)
    assert_zero_advance(tree)
    assert_active_scan(tree)
    assert_inactive_bitmap_scan(tree)
    assert_immutable_orchestration(tree)
    if any(
        node.get("kind") in {"GotoStmt", "LabelStmt"}
        for name in WORK_HELPERS
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

    def test_per_frame_state_helper_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        active = function_source(
            text,
            "vm_validation_digest_active_frame_states",
        )
        changed_active = active.replace(
            "state->boot_info->frame_states[frame_index];",
            "vm_core_initial_frame_is_free(state, frame_index) ? MICROS_VM_FRAME_FREE"
            " : state->boot_info->frame_states[frame_index];",
            1,
        )
        self.assertNotEqual(active, changed_active)
        self.assert_mutation_rejected(
            text.replace(active, changed_active, 1),
            "reconstruction|unreviewed helper",
        )

    def test_current_bitmap_cannot_replace_original_class(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        active = function_source(
            text,
            "vm_validation_digest_active_frame_states",
        )
        changed_active = active.replace(
            "local >= MICROS_VM_LOCAL_FRAME_TRANSFERABLE\n"
            "                && local <= MICROS_VM_LOCAL_FRAME_PROCESS_PAGE_TABLE",
            "(state->free_bitmap[frame_index / 64] & (UINT64_C(1) << (frame_index % 64))) != 0",
            1,
        )
        self.assertNotEqual(active, changed_active)
        self.assert_mutation_rejected(
            text.replace(active, changed_active, 1),
            "current availability",
        )

    def test_active_scan_cannot_skip_frame_boundaries(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        active = function_source(text, "vm_validation_digest_active_frame_states")
        for old, new in (
            ("frame_index = 0;", "frame_index = 1;"),
            ("frame_index < managed_frame_count;", "frame_index + 1 < managed_frame_count;"),
            ("++frame_index)", "frame_index += 2)"),
        ):
            changed = active.replace(old, new, 1)
            self.assertNotEqual(active, changed)
            self.assert_mutation_rejected(text.replace(active, changed, 1), "active frame bounds")

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
            "immutability calls|tail check",
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
            "vm_validation_free_tail_is_zero",
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
            "first_word < MICROS_VM_FREE_BITMAP_WORDS;",
            "first_word + 1 < MICROS_VM_FREE_BITMAP_WORDS;",
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
