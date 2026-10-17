import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from tests.host.test_frame_ownership_source_layout import (
    c_lexical_text,
    c_signature_start,
    tokens as c_tokens,
)


ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = ROOT / "kernel/memory/frame_ownership_validation.c"
ACTIVE_TOKENS_SHA256 = (
    "d2abefb28aa1d0de7d2a5eb15badea0319d9da9b5689c3bf29faebe3c6cb43ca"
)
INACTIVE_TOKENS_SHA256 = (
    "a6ce4e4e0f0b143d874094b5ace3e1fbcd7ff4238bbff8bfaae46a02e3235887"
)


def function_source(text, name):
    lexical = c_lexical_text(text)
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;]*?\)\s*\{", lexical)
    if match is None:
        raise AssertionError(f"missing function: {name}")
    start = c_signature_start(text, 0, match.start())
    opening = lexical.find("{", match.start())
    depth = 1
    end = opening + 1
    while end < len(lexical) and depth:
        if lexical[end] == "{":
            depth += 1
        elif lexical[end] == "}":
            depth -= 1
        end += 1
    if depth:
        raise AssertionError(f"unterminated function: {name}")
    return text[start:end]


def function_token_digest(text, name):
    value = json.dumps(
        c_tokens(c_lexical_text(function_source(text, name))),
        separators=(",", ":"),
    ).encode()
    return hashlib.sha256(value).hexdigest()


def clang_command(path, *arguments):
    compiler = shutil.which("clang")
    if compiler is None:
        raise AssertionError("Clang is required for C structural evidence")
    return (
        compiler,
        "--target=riscv64-unknown-elf",
        "-ffreestanding",
        "-nostdlibinc",
        "-std=c17",
        "-I",
        str(ROOT),
        "-I",
        str(ROOT / "include"),
        *arguments,
        str(path),
    )


def clang_preprocessed(text):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "frame_ownership_validation.c"
        path.write_text(text, encoding="utf-8")
        completed = subprocess.run(
            clang_command(path, "-E", "-P"),
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
    if completed.returncode != 0:
        raise AssertionError(
            "Clang rejected preprocessing fixture:\n" + completed.stderr
        )
    return completed.stdout


def ast_walk(node):
    yield node
    for child in node.get("inner", ()):
        if isinstance(child, dict):
            yield from ast_walk(child)


def clang_ast(text):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "frame_ownership_validation.c"
        path.write_text(text, encoding="utf-8")
        completed = subprocess.run(
            clang_command(
                path,
                "-Xclang",
                "-ast-dump=json",
                "-fsyntax-only",
            ),
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
    if completed.returncode != 0:
        raise AssertionError("Clang rejected structural fixture:\n" + completed.stderr)
    return json.loads(completed.stdout)


def function_decl(tree, name):
    matches = [
        node
        for node in ast_walk(tree)
        if node.get("kind") == "FunctionDecl"
        and node.get("name") == name
        and any(
            child.get("kind") == "CompoundStmt"
            for child in node.get("inner", ())
            if isinstance(child, dict)
        )
    ]
    if len(matches) != 1:
        raise AssertionError(f"missing or ambiguous function AST: {name}")
    return matches[0]


def function_body(function):
    return next(
        child
        for child in function["inner"]
        if child.get("kind") == "CompoundStmt"
    )


def loop_body(loop):
    children = [
        child for child in loop.get("inner", ()) if child.get("kind")
    ]
    if not children or children[-1].get("kind") != "CompoundStmt":
        raise AssertionError("for loop must own one explicit compound body")
    return children[-1]


def referenced_names(node):
    return {
        child.get("referencedDecl", {}).get("name")
        for child in ast_walk(node)
        if child.get("kind") == "DeclRefExpr"
    }


def bitmap_subscripts(node):
    return [
        child
        for child in ast_walk(node)
        if child.get("kind") == "ArraySubscriptExpr"
        and any(
            nested.get("kind") == "MemberExpr"
            and nested.get("name") == "allocated_bitmap"
            for nested in ast_walk(child)
        )
    ]


def bitmap_members(node):
    return [
        child
        for child in ast_walk(node)
        if child.get("kind") == "MemberExpr"
        and child.get("name") == "allocated_bitmap"
    ]


def call_targets(node):
    return {
        nested.get("referencedDecl", {}).get("name")
        for call in ast_walk(node)
        if call.get("kind") == "CallExpr"
        for nested in ast_walk(call)
        if nested.get("kind") == "DeclRefExpr"
        and nested.get("referencedDecl", {}).get("kind")
            == "FunctionDecl"
    }


def assert_word_bounded_scans(text):
    preprocessed = clang_preprocessed(text)
    if (
        function_token_digest(preprocessed, "validate_active_frames")
        != ACTIVE_TOKENS_SHA256
        or function_token_digest(preprocessed, "inactive_state_is_zero")
        != INACTIVE_TOKENS_SHA256
    ):
        raise AssertionError("validator work shape differs from reviewed identity")
    tree = clang_ast(text)
    active = function_decl(tree, "validate_active_frames")
    if (
        active.get("storageClass") != "static"
        or not active.get("type", {}).get("qualType", "").startswith(
            "enum micros_frame_ownership_error ("
        )
    ):
        raise AssertionError("active helper declaration is unsupported")
    active_body = function_body(active)
    active_outer = [
        child
        for child in active_body.get("inner", ())
        if child.get("kind") == "ForStmt"
    ]
    if len(active_outer) != 1:
        raise AssertionError("active scan must have one outer word loop")
    outer = active_outer[0]
    outer_body = loop_body(outer)
    active_inner = [
        child
        for child in outer_body.get("inner", ())
        if child.get("kind") == "ForStmt"
    ]
    if len(active_inner) != 1:
        raise AssertionError("active scan must have one inner frame loop")
    inner = active_inner[0]
    active_subscripts = bitmap_subscripts(active)
    active_members = bitmap_members(active)
    if len(active_subscripts) != 1 or len(active_members) != 1:
        raise AssertionError("active scan must read exactly one bitmap word")
    allocated_words = [
        node
        for node in ast_walk(outer_body)
        if node.get("kind") == "VarDecl"
        and node.get("name") == "allocated_word"
    ]
    if len(allocated_words) != 1:
        raise AssertionError("active scan must own one allocated_word")
    allocated_word = allocated_words[0]
    if allocated_word.get("type", {}).get("qualType") != "uint64_t" or (
        bitmap_subscripts(allocated_word) != active_subscripts
    ) or bitmap_members(allocated_word) != active_members:
        raise AssertionError("active bitmap word is not loaded by scalar value")
    if referenced_names(active_subscripts[0]) != {"ownership", "word_index"}:
        raise AssertionError("active bitmap load is not word indexed")
    if bitmap_subscripts(inner):
        raise AssertionError("active inner loop reloads the bitmap")
    if "allocated_word" not in referenced_names(inner):
        raise AssertionError("active inner loop does not consume allocated_word")
    allocated = [
        node
        for node in ast_walk(inner)
        if node.get("kind") == "VarDecl"
        and node.get("name") == "allocated"
    ]
    if len(allocated) != 1 or referenced_names(allocated[0]) != {
        "allocated_word",
        "bit",
    }:
        raise AssertionError("active allocation does not derive from cached word")
    allocation_operators = {
        node.get("opcode")
        for node in ast_walk(allocated[0])
        if node.get("kind") == "BinaryOperator"
    }
    if not {"&", "!="} <= allocation_operators:
        raise AssertionError("active allocation expression is unsupported")
    if call_targets(active) != {
        "micros_frame_owner_is_valid",
        "micros_frame_ownership_handoff_target_is_valid",
    }:
        raise AssertionError("active scan has unreviewed calls")
    if any(
        node.get("kind") in {"GotoStmt", "LabelStmt"}
        for node in ast_walk(active)
    ):
        raise AssertionError("active scan has unstructured control flow")

    inactive = function_decl(tree, "inactive_state_is_zero")
    if (
        inactive.get("storageClass") != "static"
        or not inactive.get("type", {}).get("qualType", "").startswith(
            "_Bool ("
        )
    ):
        raise AssertionError("inactive helper declaration is unsupported")
    inactive_body = function_body(inactive)
    inactive_loops = [
        child
        for child in inactive_body.get("inner", ())
        if child.get("kind") == "ForStmt"
    ]
    if len(inactive_loops) != 2:
        raise AssertionError("inactive scan must have frame and word loops")
    frame_loop = next(
        loop
        for loop in inactive_loops
        if "frame_index" in referenced_names(loop)
    )
    word_loop = next(loop for loop in inactive_loops if loop is not frame_loop)
    if any(node.get("kind") == "CallExpr" for node in ast_walk(inactive)):
        raise AssertionError("inactive scan calls a helper")
    if any(
        node.get("kind") in {"GotoStmt", "LabelStmt"}
        for node in ast_walk(inactive)
    ):
        raise AssertionError("inactive scan has unstructured control flow")
    if bitmap_subscripts(frame_loop) or bitmap_members(frame_loop):
        raise AssertionError("inactive frame loop reads allocator bitmap")
    inactive_subscripts = bitmap_subscripts(inactive)
    inactive_members = bitmap_members(inactive)
    if len(inactive_subscripts) != 2 or len(inactive_members) != 2:
        raise AssertionError("inactive scan has unreviewed bitmap access")
    if {
        node["id"] for subscript in inactive_subscripts
        for node in bitmap_members(subscript)
    } != {node["id"] for node in inactive_members}:
        raise AssertionError("inactive bitmap decays to a pointer alias")
    word_subscripts = bitmap_subscripts(word_loop)
    if (
        len(word_subscripts) != 1
        or referenced_names(word_subscripts[0])
            != {"ownership", "word_index"}
        or "frame_index" in referenced_names(word_loop)
    ):
        raise AssertionError("inactive bitmap tail is not word indexed")
    if (
        "inactive_mask" not in referenced_names(inactive)
        or "MICROS_FRAME_ALLOCATOR_BITMAP_WORDS"
            not in referenced_names(inactive)
    ):
        raise AssertionError("inactive boundary or tail bound is missing")


class FrameValidationHotPathTests(unittest.TestCase):
    def test_complete_validation_uses_bitmap_word_bounded_scans(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        assert_word_bounded_scans(text)

    def test_active_inner_loop_bitmap_reload_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        changed = text.replace(
            "ownership->allocator->allocated_bitmap[word_index];",
            "UINT64_C(0);",
            1,
        ).replace(
            "bool allocated = (allocated_word & bit) != 0;",
            "bool allocated = (\n"
            "                ownership->allocator->allocated_bitmap[word_index]\n"
            "                & bit\n"
            "            ) != 0;",
            1,
        )
        with self.assertRaisesRegex(
            AssertionError,
            "reviewed identity|bitmap word|inner loop",
        ):
            assert_word_bounded_scans(changed)

    def test_active_bitmap_pointer_alias_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        changed = text.replace(
            "uint64_t allocated_word =\n"
            "            ownership->allocator->allocated_bitmap[word_index];",
            "uint64_t *allocated_word =\n"
            "            &ownership->allocator->allocated_bitmap [word_index];",
            1,
        ).replace(
            "bool allocated = (allocated_word & bit) != 0;",
            "bool allocated = (*allocated_word & bit) != 0;",
            1,
        )
        with self.assertRaisesRegex(
            AssertionError,
            "reviewed identity|scalar value",
        ):
            assert_word_bounded_scans(changed)

    def test_active_loop_header_bitmap_reload_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        changed = text.replace(
            "++frame_index, bit <<= 1",
            "++frame_index,\n"
            "            allocated_word =\n"
            "                ownership->allocator->allocated_bitmap [word_index]",
            1,
        )
        with self.assertRaisesRegex(
            AssertionError,
            "reviewed identity|bitmap word|inner loop",
        ):
            assert_word_bounded_scans(changed)

    def test_active_parenthesized_bit_helper_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        changed = text.replace(
            "bool allocated = (allocated_word & bit) != 0;",
            "bool allocated = (\n"
            "                (void)allocated_word,\n"
            "                (micros_frame_ownership_allocator_bit_is_set)(\n"
            "                    ownership->allocator,\n"
            "                    frame_index\n"
            "                )\n"
            "            );",
            1,
        )
        with self.assertRaisesRegex(
            AssertionError,
            "reviewed identity|cached word|bit helper",
        ):
            assert_word_bounded_scans(changed)

    def test_inactive_inner_loop_helper_call_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        inactive = function_source(text, "inactive_state_is_zero")
        changed_inactive = inactive.replace(
            "struct micros_frame_owner owner =\n"
            "            ownership->owners[frame_index];",
            "struct micros_frame_owner owner =\n"
            "            ownership->owners[frame_index];\n"
            "        (void)micros_frame_owner_is_valid(owner);",
            1,
        )
        changed = text.replace(inactive, changed_inactive, 1)
        with self.assertRaisesRegex(
            AssertionError,
            "reviewed identity|calls a helper",
        ):
            assert_word_bounded_scans(changed)

    def test_inactive_compound_literal_call_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        inactive = function_source(text, "inactive_state_is_zero")
        support = (
            "typedef void (*inactive_probe_fn)(uint64_t);\n"
            "static void inactive_probe(uint64_t frame_index)\n"
            "{\n"
            "    (void)frame_index;\n"
            "}\n\n"
        )
        changed_inactive = inactive.replace(
            "++frame_index\n"
            "    )",
            "++frame_index,\n"
            "        ((inactive_probe_fn){inactive_probe})(frame_index)\n"
            "    )",
            1,
        )
        changed = text.replace(
            "static bool inactive_state_is_zero(",
            support + "static bool inactive_state_is_zero(",
            1,
        ).replace(inactive, changed_inactive, 1)
        with self.assertRaisesRegex(
            AssertionError,
            "reviewed identity|calls a helper",
        ):
            assert_word_bounded_scans(changed)

    def test_inactive_bitmap_pointer_alias_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        inactive = function_source(text, "inactive_state_is_zero")
        changed_inactive = inactive.replace(
            "size_t boundary_bit;",
            "size_t boundary_bit;\n"
            "    const volatile uint64_t *bitmap_alias =\n"
            "        ownership->allocator->allocated_bitmap;",
            1,
        ).replace(
            "struct micros_frame_owner owner =\n"
            "            ownership->owners[frame_index];",
            "struct micros_frame_owner owner =\n"
            "            ownership->owners[frame_index];\n"
            "        (void)*(bitmap_alias + frame_index / 64);",
            1,
        )
        changed = text.replace(inactive, changed_inactive, 1)
        with self.assertRaisesRegex(
            AssertionError,
            "reviewed identity|bitmap access|pointer alias",
        ):
            assert_word_bounded_scans(changed)

    def test_inactive_goto_helper_call_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        inactive = function_source(text, "inactive_state_is_zero")
        support = (
            "static void inactive_probe(uint64_t frame_index)\n"
            "{\n"
            "    (void)frame_index;\n"
            "}\n\n"
        )
        changed_inactive = inactive.replace(
            "        }\n"
            "    }\n\n"
            "    word_index =",
            "        }\n"
            "        goto inactive_probe_label;\n"
            "inactive_continue:\n"
            "        ;\n"
            "    }\n"
            "    goto inactive_done;\n"
            "inactive_probe_label:\n"
            "    inactive_probe(frame_index);\n"
            "    goto inactive_continue;\n"
            "inactive_done:\n\n"
            "    word_index =",
            1,
        )
        changed = text.replace(
            "static bool inactive_state_is_zero(",
            support + "static bool inactive_state_is_zero(",
            1,
        ).replace(inactive, changed_inactive, 1)
        with self.assertRaisesRegex(
            AssertionError,
            "reviewed identity|calls a helper|unstructured control flow",
        ):
            assert_word_bounded_scans(changed)

    def test_line_splice_cannot_hide_inactive_field_check(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        changed = text.replace(
            "owner.generation != 0\n"
            "            || owner.slot != 0",
            "owner.generation != 0 // hide slot \\\n"
            "            || owner.slot != 0",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "reviewed identity"):
            assert_word_bounded_scans(changed)

    def test_disabled_reviewed_copy_cannot_hide_live_mutation(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        active = function_source(text, "validate_active_frames")
        inactive = function_source(text, "inactive_state_is_zero")
        support = (
            "static bool unreviewed_active_bit(\n"
            "    const struct micros_frame_ownership *ownership,\n"
            "    uint64_t frame_index\n"
            ")\n"
            "{\n"
            "    return (\n"
            "        ownership->allocator->allocated_bitmap[frame_index / 64]\n"
            "        & (UINT64_C(1) << (frame_index % 64))\n"
            "    ) != 0;\n"
            "}\n\n"
        )
        changed_active = active.replace(
            "bool allocated = (allocated_word & bit) != 0;",
            "bool allocated = (\n"
            "                (void)allocated_word,\n"
            "                unreviewed_active_bit(ownership, frame_index)\n"
            "            );",
            1,
        )
        changed = text.replace(active, changed_active, 1).replace(
            "static enum micros_frame_ownership_error validate_active_frames(",
            "#if 0\n"
            + active
            + "\n"
            + inactive
            + "\n#endif\n\n"
            + support
            + "static enum micros_frame_ownership_error "
            + "validate_active_frames(",
            1,
        )
        with self.assertRaisesRegex(AssertionError, "reviewed identity"):
            assert_word_bounded_scans(changed)

    def test_active_return_type_change_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        changed = text.replace(
            "static enum micros_frame_ownership_error "
            "validate_active_frames(",
            "static bool validate_active_frames(",
            1,
        )
        with self.assertRaisesRegex(
            AssertionError,
            "reviewed identity|declaration",
        ):
            assert_word_bounded_scans(changed)

    def test_inactive_storage_class_change_is_rejected(self):
        text = VALIDATOR.read_text(encoding="utf-8")
        changed = text.replace(
            "static bool inactive_state_is_zero(",
            "bool inactive_state_is_zero(",
            1,
        )
        with self.assertRaisesRegex(
            AssertionError,
            "reviewed identity|declaration",
        ):
            assert_word_bounded_scans(changed)
