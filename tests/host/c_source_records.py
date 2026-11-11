"""Shared bounded C function-record inspection for source-contract gates."""

from __future__ import annotations

from dataclasses import dataclass
from functools import lru_cache
import re


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


def tokens(text: str) -> tuple[str, ...]:
    pattern = (
        r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'
        r"'(?:\\.|[^'\\])*'|[A-Za-z_]\w*|"
        r"0[xX][0-9a-fA-F]+[uUlL]*|\d+[uUlL]*|"
        r">>=|<<=|->|\+\+|--|==|!=|<=|>=|&&|\|\||"
        r"\+=|-=|\*=|/=|%=|&=|\|=|\^=|<<|>>|[^\s]"
    )
    return tuple(
        match.group() for match in re.finditer(pattern, text)
        if not match.group().startswith(("//", "/*"))
    )


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
    return _function_definitions(text, c_lexical_text(text))


def _function_definitions(text: str, lexical: str) -> dict[str, int]:
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
                open_parenthesis = matching_open_parenthesis(declaration, close_parenthesis)
                before = declaration[:open_parenthesis].rstrip()
                match = re.search(r"([A-Za-z_][A-Za-z0-9_]*)$", before)
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
                            raise AssertionError(f"unterminated body for {name}")
                        name_index = segment_start + open_parenthesis - len(match.group(0))
                        start = c_signature_start(lexical, segment_start, name_index)
                        definitions[name] = text.count("\n", start, body_end - 1) + 1
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


def c_function_body(text: str, name: str) -> str:
    lexical = c_lexical_text(text)
    candidates = re.finditer(rf"\b{re.escape(name)}\s*\([^;{{}}]*\)\s*\{{", lexical)
    match = next((
        candidate for candidate in candidates
        if lexical.count("{", 0, candidate.start()) == lexical.count("}", 0, candidate.start())
    ), None)
    if match is None:
        raise AssertionError(f"missing function body: {name}")
    start = match.end() - 1
    depth = 0
    for token in re.finditer(r"[{}]", lexical[start:]):
        depth += 1 if token.group() == "{" else -1
        if depth == 0:
            return text[start:start + token.end()]
    raise AssertionError(f"unterminated function body: {name}")


def c_declaration_text(text: str) -> str:
    return _declaration_text(c_lexical_text(text))


def _declaration_text(lexical: str) -> str:
    lines = lexical.splitlines(keepends=True)
    output = []
    continuation = False
    for line in lines:
        if continuation or line.lstrip().startswith("#"):
            output.append("".join("\n" if char == "\n" else " " for char in line))
            continuation = line.rstrip().endswith("\\")
        else:
            output.append(line)
    return "".join(output)


def require_single_object_declaration(declaration: str) -> None:
    depth = 0
    for char in declaration:
        if char in "({[":
            depth += 1
        elif char in ")}]":
            depth -= 1
            if depth < 0:
                raise AssertionError("unbalanced object declaration")
        elif char == "," and depth == 0:
            raise AssertionError("multiple objects in one file-scope declaration")
    if depth != 0:
        raise AssertionError("unbalanced object declaration")


def file_scope_objects(text: str) -> tuple[str, ...]:
    lexical = c_declaration_text(text)
    objects = []
    depth = 0
    start = 0
    function_body = False
    for index, char in enumerate(lexical):
        if char == "{":
            if depth == 0:
                prefix = lexical[start:index].strip()
                function_body = prefix.endswith(")") and "=" not in prefix
            depth += 1
        elif char == "}":
            depth -= 1
            if depth < 0:
                raise AssertionError("unexpected closing brace")
            if depth == 0 and function_body:
                start = index + 1
                function_body = False
        elif char == ";" and depth == 0:
            declaration = lexical[start:index + 1].strip()
            start = index + 1
            if (
                not declaration
                or declaration.startswith("typedef ")
                or declaration.startswith("_Static_assert")
                or (
                    declaration.startswith("extern ")
                    and "=" not in declaration and "{" not in declaration
                )
                or re.fullmatch(
                    r"[A-Za-z_][A-Za-z0-9_\s*]*"
                    r"\b[A-Za-z_][A-Za-z0-9_]*\s*\([^;]*\)\s*;",
                    declaration, re.DOTALL,
                )
                or (
                    "{" in declaration
                    and re.fullmatch(
                        r"(?:struct|union|enum)(?:\s+[A-Za-z_][A-Za-z0-9_]*)?",
                        declaration.split("{", 1)[0].strip(),
                    )
                    and declaration.rsplit("}", 1)[-1].strip() == ";"
                )
                or re.fullmatch(
                    r"(?:struct|union|enum)\s+[A-Za-z_][A-Za-z0-9_]*\s*;",
                    declaration,
                )
                or re.fullmatch(
                    r"MICROS_(?:TRAP|HART_TRAP|USER_CONTEXT)_ASSERT_OFFSET"
                    r"\([^;]*\)\s*;", declaration, re.DOTALL,
                )
            ):
                continue
            require_single_object_declaration(declaration)
            objects.append(declaration)
    if depth != 0 or lexical[start:].strip():
        raise AssertionError("unterminated file-scope declaration")
    return tuple(objects)


def compact(text: str) -> str:
    return " ".join(tokens(text))


def matching(text: str, start: int, opening: str = "(", closing: str = ")") -> int:
    return matching_lexical(c_lexical_text(text), start, opening, closing)


def matching_lexical(
    lexical: str, start: int, opening: str = "(", closing: str = ")",
) -> int:
    depth = 0
    for index in range(start, len(lexical)):
        if lexical[index] == opening:
            depth += 1
        elif lexical[index] == closing:
            depth -= 1
            if depth == 0:
                return index
    raise AssertionError(f"unbalanced {opening}{closing}")


def cpp_guards(text: str, end: int) -> tuple[str, ...]:
    stack = []
    logical = re.sub(r"\\\n", " ", text[:end])
    for line in logical.splitlines():
        directive = re.match(r"\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", line)
        if not directive:
            continue
        operation, expression = directive.groups()
        if operation in ("if", "ifdef", "ifndef"):
            stack.append(compact(operation + " " + expression))
        elif operation in ("elif", "else"):
            if not stack:
                raise AssertionError("unmatched CPP alternative")
            stack[-1] += " / " + compact(operation + " " + expression)
        else:
            if not stack:
                raise AssertionError("unmatched CPP endif")
            stack.pop()
    return tuple(stack)


@dataclass(frozen=True)
class Function:
    name: str
    signature: str
    body: str
    lines: int
    guards: tuple[str, ...]


@lru_cache(maxsize=128)
def function_records(text: str) -> dict[str, Function]:
    lexical = c_lexical_text(text)
    declarations = _declaration_text(lexical)
    result = {}
    for name, lines in _function_definitions(text, lexical).items():
        candidates = []
        for match in re.finditer(rf"\b{re.escape(name)}\s*\(", lexical):
            open_at = lexical.find("(", match.start())
            close_at = matching_lexical(lexical, open_at)
            body_at = close_at + 1
            while body_at < len(lexical) and lexical[body_at].isspace():
                body_at += 1
            if body_at < len(lexical) and lexical[body_at] == "{":
                candidates.append((match.start(), body_at))
        if len(candidates) != 1:
            raise AssertionError(f"ambiguous function definition: {name}")
        name_at, body_at = candidates[0]
        boundary = max(lexical.rfind("}", 0, name_at), lexical.rfind(";", 0, name_at)) + 1
        start = c_signature_start(declarations, boundary, name_at)
        body_end = matching_lexical(lexical, body_at, "{", "}")
        result[name] = Function(
            name, text[start:body_at], text[body_at + 1:body_end],
            lines, cpp_guards(text, name_at),
        )
    return result
