"""Bounded inspection of the repository's literal CMake source declarations."""

import re


def cmake_command(text, command, first_argument):
    match = re.search(
        rf"\b{re.escape(command)}\s*\(\s*{re.escape(first_argument)}(?=\s|\))", text,
    )
    if match is None:
        raise AssertionError(f"missing {command} command for {first_argument}")
    opening = text.find("(", match.start())
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                return text[match.start():index + 1]
    raise AssertionError(f"unterminated {command} command for {first_argument}")


def cmake_commands(text, command, first_argument):
    return tuple(
        cmake_command(text[match.start():], command, first_argument)
        for match in re.finditer(
            rf"\b{re.escape(command)}\s*\(\s*{re.escape(first_argument)}(?=\s|\))", text,
        )
    )


def cmake_if_blocks(text, condition):
    pattern = re.compile(rf"\bif\s*\(\s*{re.escape(condition)}\s*\)")
    blocks, offset = [], 0
    while True:
        match = pattern.search(text, offset)
        if match is None:
            break
        depth = 1
        for token in re.finditer(r"\b(?P<command>if|endif)\s*\(", text[match.end():]):
            depth += 1 if token.group("command") == "if" else -1
            if depth == 0:
                end = match.end() + token.end()
                blocks.append(text[match.start():end])
                offset = end
                break
        else:
            raise AssertionError(f"unterminated if block for {condition}")
    if not blocks:
        raise AssertionError(f"missing if block for {condition}")
    return tuple(blocks)
