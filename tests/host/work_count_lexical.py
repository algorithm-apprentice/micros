"""Positional inspection using the existing bounded C transport vocabulary."""

from dataclasses import dataclass
import re

from tests.host.c_source_records import (
    c_lexical_text, function_records, matching_lexical, tokens,
)
from tests.host.work_count_types import WorkEvidenceError


TOKEN_PATTERN = (
    r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'
    r"'(?:\\.|[^'\\])*'|[A-Za-z_]\w*|"
    r"0[xX][0-9a-fA-F]+[uUlL]*|\d+[uUlL]*|"
    r">>=|<<=|->|\+\+|--|==|!=|<=|>=|&&|\|\||"
    r"\+=|-=|\*=|/=|%=|&=|\|=|\^=|<<|>>|[^\s]"
)


@dataclass(frozen=True)
class Token:
    value: str
    start: int
    end: int


@dataclass(frozen=True)
class FunctionSpan:
    body_start: int
    body_end: int


@dataclass(frozen=True)
class Edit:
    start: int
    end: int
    replacement: str
    key: tuple[str, str] | None = None


def positional_tokens(text):
    c_lexical_text(text)
    result = tuple(
        Token(match.group(), match.start(), match.end())
        for match in re.finditer(TOKEN_PATTERN, text)
        if not match.group().startswith(("//", "/*"))
    )
    if tuple(token.value for token in result) != tokens(text):
        raise WorkEvidenceError("C token vocabulary drift")
    return result


def function_spans(source):
    lexical = c_lexical_text(source)
    result = {}
    for name in function_records(source):
        candidates = []
        for match in re.finditer(rf"\b{re.escape(name)}\s*\(", lexical):
            opening = lexical.find("(", match.start())
            closing = matching_lexical(lexical, opening)
            body = closing + 1
            while body < len(lexical) and lexical[body].isspace():
                body += 1
            if body < len(lexical) and lexical[body] == "{":
                end = matching_lexical(lexical, body, "{", "}")
                candidates.append(FunctionSpan(body + 1, end))
        if len(candidates) != 1:
            raise WorkEvidenceError(f"ambiguous function: {name}")
        result[name] = candidates[0]
    return result


def call_arguments(source, lexical, opening, closing):
    result = []
    stack = []
    start = opening + 1
    pairs = {")": "(", "]": "[", "}": "{"}
    for index in range(start, closing):
        value = lexical[index]
        if value in "([{":
            stack.append(value)
        elif value in pairs:
            if not stack or stack.pop() != pairs[value]:
                raise WorkEvidenceError("unbalanced observation arguments")
        elif value == "," and not stack:
            result.append(source[start:index].strip())
            start = index + 1
    if stack:
        raise WorkEvidenceError("unbalanced observation arguments")
    result.append(source[start:closing].strip())
    return tuple(result)


def provisional_projection(source, edits):
    parts = []
    markers = {}
    cursor = 0
    output_position = 0
    for edit in sorted(edits, key=lambda item: item.start):
        if edit.start < cursor:
            raise WorkEvidenceError("nested/overlapping observations")
        prefix = source[cursor:edit.start]
        parts.append(prefix)
        output_position += len(prefix)
        if edit.key is not None:
            markers[edit.key] = (
                output_position, output_position + len(edit.replacement),
            )
        parts.append(edit.replacement)
        output_position += len(edit.replacement)
        cursor = edit.end
    parts.append(source[cursor:])
    return "".join(parts), markers


def compound_layout(values):
    stack = []
    stacks = []
    closing = {}
    for index, value in enumerate(values):
        stacks.append(tuple(stack))
        if value == "{":
            stack.append(index)
        elif value == "}":
            if not stack:
                raise WorkEvidenceError("unbalanced compound")
            closing[stack.pop()] = index
    if stack:
        raise WorkEvidenceError("unbalanced compound")
    stacks.append(())
    return tuple(stacks), closing


def boundary_index(body_tokens, position):
    for index, token in enumerate(body_tokens):
        if token.start < position < token.end:
            raise WorkEvidenceError("observation splits a source token")
        if token.start >= position:
            return index
    return len(body_tokens)


def matches_at(values, fragment, start, end):
    width = len(fragment)
    return tuple(
        index for index in range(start, end - width + 1)
        if values[index:index + width] == fragment
    )


def matching_tokens(values, opening):
    pairs = {"(": ")", "[": "]", "{": "}"}
    stack = []
    for index in range(opening, len(values)):
        if values[index] in pairs:
            stack.append(pairs[values[index]])
        elif stack and values[index] == stack[-1]:
            stack.pop()
            if not stack:
                return index
    raise WorkEvidenceError("work primitive has an unbalanced delimiter")


def region_header(values, opening):
    if opening and values[opening - 1] == ")":
        depth = 1
        for start in range(opening - 2, -1, -1):
            if values[start] == ")":
                depth += 1
            elif values[start] == "(":
                depth -= 1
                if depth == 0:
                    if start and values[start - 1] in ("if", "for", "while", "switch"):
                        return start - 1
                    break
        else:
            raise WorkEvidenceError("work primitive has an unbalanced region header")
    if opening and values[opening - 1] in ("else", "do"):
        return opening - 1
    return opening


def enclosing_regions(values, stacks, closings, enclosing):
    result, parent = [], ()
    start, end = 0, len(values)
    for opening in enclosing:
        begin = region_header(values, opening)
        fragment = values[begin:opening + 1]
        candidates = [
            index for index in matches_at(values, fragment, start, end)
            if stacks[index] == parent
        ]
        if begin not in candidates:
            raise WorkEvidenceError("work primitive lacks its enclosing compound")
        result.append((" ".join(fragment), candidates.index(begin)))
        parent = (*parent, opening)
        start, end = opening + 1, closings[opening]
    return tuple(result)


def loop_shape(body):
    values = tuple(token.value for token in positional_tokens(body))
    stacks, closings = compound_layout(values)
    result = []
    for index, value in enumerate(values):
        if value == "do":
            raise WorkEvidenceError("current work observation does not admit a do loop")
        if value not in ("for", "while"):
            continue
        if values[index + 1:index + 2] != ("(",):
            raise WorkEvidenceError("work loop has no complete condition")
        closing = matching_tokens(values, index + 1)
        if values[closing + 1:closing + 2] != ("{",):
            raise WorkEvidenceError("work loop has no explicit compound")
        result.append((
            " ".join(values[index:closing + 2]),
            enclosing_regions(values, stacks, closings, stacks[index]),
        ))
    return tuple(result)
