"""Shared bounded C function-record inspection for source-contract gates."""

from __future__ import annotations

from dataclasses import dataclass
from functools import lru_cache
import re

from tests.host.test_frame_ownership_source_layout import (
    c_function_definitions,
    c_lexical_text,
    c_signature_start,
    tokens,
)
from tests.host.test_vm_service_source_layout import c_declaration_text


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
    declarations = c_declaration_text(text)
    result = {}
    for name, lines in c_function_definitions(text).items():
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
