"""Finite, anchored projection of passive work observations."""

import re
from pathlib import PurePosixPath

from tests.host.c_source_records import cpp_guards, matching_lexical, tokens
from tests.host.work_count_lexical import (
    Edit, boundary_index, call_arguments, compound_layout, function_spans,
    matches_at, positional_tokens, provisional_projection, c_lexical_text,
)
from tests.host.work_count_types import (
    Kind, ObservationSite, Owner, Placement, Primitive, Region,
    SourceAdmission, Stage, WorkEvidenceError,
)


HEADER_PATTERN = (
    r'^[ \t]*#[ \t]*include[ \t]+"micros/work_observation\.h"[ \t]*\r?\n?'
)
CALL_PATTERN = r"\bMICROS_TEST_WORK_(HIT|COMPARE)\s*\("
VOLUME_KINDS = (Kind.COPIED_BYTE, Kind.CLEARED_BYTE)


def require_path(value):
    if (
        type(value) is not str or not value or "\\" in value
        or PurePosixPath(value).is_absolute()
        or str(PurePosixPath(value)) != value
        or any(part in (".", "..") for part in PurePosixPath(value).parts)
    ):
        raise WorkEvidenceError("source path must be normalized and repository-relative")


def require_occurrence(value):
    if type(value) is not int or value < 0:
        raise WorkEvidenceError("invalid primitive/region occurrence")


def validate_site(site):
    if type(site) is not ObservationSite:
        raise WorkEvidenceError("invalid observation declaration")
    if (
        type(site.function) is not str
        or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", site.function)
        or type(site.site_id) is not str
        or not re.fullmatch(r"[a-z][a-z0-9-]{0,63}", site.site_id)
        or type(site.owner) is not Owner or type(site.kind) is not Kind
        or type(site.placement) is not Placement
        or type(site.units) is not int
    ):
        raise WorkEvidenceError("invalid site identity/dimensions")
    if not 1 <= site.units <= 4096 or (
        site.kind not in VOLUME_KINDS and site.units != 1
    ):
        raise WorkEvidenceError("invalid primitive units")
    if type(site.regions) is not tuple:
        raise WorkEvidenceError("invalid enclosing-region declaration")
    for region in site.regions:
        if type(region) is not Region or type(region.tokens) is not str:
            raise WorkEvidenceError("invalid enclosing-region selector")
        require_occurrence(region.occurrence)
        fragment = tokens(region.tokens)
        if not fragment or fragment[-1] != "{" or fragment.count("{") != 1 or "}" in fragment:
            raise WorkEvidenceError("region selector must end at one opening brace")
    if site.placement is Placement.FUNCTION_ENTRY:
        if (
            site.kind is not Kind.COMPLETE_VALIDATOR_CALL
            or site.regions or site.primitive is not None
        ):
            raise WorkEvidenceError("invalid function-entry observation")
    else:
        if type(site.primitive) is not Primitive or type(site.primitive.tokens) is not str:
            raise WorkEvidenceError("missing primitive locator")
        require_occurrence(site.primitive.occurrence)
        fragment = tokens(site.primitive.tokens)
        if not fragment or "{" in fragment or "}" in fragment:
            raise WorkEvidenceError("invalid primitive fragment")
        if site.kind is Kind.COMPLETE_VALIDATOR_CALL:
            raise WorkEvidenceError("complete validator requires function-entry placement")
        if site.placement is Placement.WRAPS:
            if site.kind is not Kind.COMPARISON:
                raise WorkEvidenceError("comparison wrapper has another work kind")
            validate_comparison(site.primitive.tokens)
        elif fragment[-1] != ";" or fragment[0] in (
            "if", "for", "while", "do", "switch", "return", "goto",
        ):
            raise WorkEvidenceError("statement primitive must be complete and non-control")
    if site.kind in VOLUME_KINDS and site.placement is not Placement.AFTER:
        raise WorkEvidenceError("byte volume must follow completed movement")


def validate_comparison(expression):
    values = tokens(expression)
    comparisons = sum(value in ("==", "!=", "<", ">", "<=", ">=") for value in values)
    forbidden = (
        "&&", "||", "?", ":", "=", "+=", "-=", "*=", "/=", "%=",
        "&=", "|=", "^=", "<<=", ">>=", "++", "--", ";", "{", "}",
    )
    if comparisons != 1 or any(value in forbidden for value in values):
        raise WorkEvidenceError("wrapper requires one unchanged scalar comparison")
    stack = []
    for index, value in enumerate(values):
        if value in ("(", "["):
            named_call = (
                value == "(" and index > 0
                and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", values[index - 1])
                and values[index - 1] not in ("sizeof", "_Alignof")
            )
            stack.append((value, bool(named_call)))
        elif value in (")", "]"):
            opening = "(" if value == ")" else "["
            if not stack or stack.pop()[0] != opening:
                raise WorkEvidenceError("unbalanced comparison")
        elif value == "," and (not stack or not stack[-1][1]):
            raise WorkEvidenceError("comma operator is not an admitted comparison")
    if stack:
        raise WorkEvidenceError("unbalanced comparison")


def declarations(source_path, admission, stage):
    require_path(source_path)
    if (
        type(admission) is not SourceAdmission or type(stage) is not Stage
        or admission.stage is not stage or admission.source_path != source_path
        or type(admission.sites) is not tuple
        or type(admission.include_header) is not bool
    ):
        raise WorkEvidenceError("source/stage declaration does not match")
    result = {}
    entries = set()
    for site in admission.sites:
        validate_site(site)
        key = (site.function, site.site_id)
        if key in result:
            raise WorkEvidenceError("duplicate site declaration")
        if site.placement is Placement.FUNCTION_ENTRY:
            if site.function in entries:
                raise WorkEvidenceError("duplicate function-entry observation")
            entries.add(site.function)
        result[key] = site
    return result


def header_edit(source, lexical, spans, include_header):
    matches = [
        match for match in re.finditer(HEADER_PATTERN, source, re.MULTILINE)
        if lexical[match.start():match.end()].lstrip().startswith("#")
    ]
    if len(matches) != int(include_header):
        raise WorkEvidenceError("observation include does not match declaration")
    if not matches:
        return []
    match = matches[0]
    if cpp_guards(source, match.start()) or any(
        span.body_start <= match.start() < span.body_end for span in spans.values()
    ):
        raise WorkEvidenceError("observation include is not unconditional top-level")
    return [Edit(match.start(), match.end(), "")]


def observation_edit(source, lexical, spans, declared, match):
    opening = lexical.find("(", match.start())
    closing = matching_lexical(lexical, opening)
    arguments = call_arguments(source, lexical, opening, closing)
    function = next(
        (name for name, span in spans.items()
         if span.body_start <= match.start() < span.body_end),
        None,
    )
    if function is None or not arguments or not re.fullmatch(
        r'"[a-z][a-z0-9-]{0,63}"', arguments[0],
    ):
        raise WorkEvidenceError("observation is outside a declared function/site")
    key = (function, arguments[0][1:-1])
    if key not in declared:
        raise WorkEvidenceError("undeclared observation site")
    site = declared[key]
    if len(arguments) < 2 or tokens(arguments[1]) != (
        "MICROS_WORK_OWNER_" + site.owner.name,
    ):
        raise WorkEvidenceError("observation owner does not match declaration")
    if match.group(1) == "COMPARE":
        if len(arguments) != 3 or site.placement is not Placement.WRAPS:
            raise WorkEvidenceError("comparison form does not match declaration")
        validate_comparison(arguments[2])
        return Edit(match.start(), closing + 1, arguments[2], key)
    if len(arguments) != 4 or site.placement is Placement.WRAPS:
        raise WorkEvidenceError("statement form does not match declaration")
    if tokens(arguments[2]) != ("MICROS_WORK_KIND_" + site.kind.name,):
        raise WorkEvidenceError("observation kind does not match declaration")
    if arguments[3] != str(site.units):
        raise WorkEvidenceError("observation units do not match declaration")
    end = closing + 1
    while end < len(lexical) and lexical[end].isspace():
        end += 1
    if end == len(lexical) or lexical[end] != ";":
        raise WorkEvidenceError("observation is not a complete statement")
    prior = tokens(source[spans[function].body_start:match.start()])
    if prior and prior[-1] not in (";", "{", "}"):
        raise WorkEvidenceError("observation is in an unbraced/declaration/return context")
    return Edit(match.start(), end + 1, "", key)


def resolve_regions(values, stacks, closings, regions):
    start, end, enclosing = 0, len(values), ()
    for region in regions:
        fragment = tokens(region.tokens)
        matches = [
            position for position in matches_at(values, fragment, start, end)
            if stacks[position] == enclosing
        ]
        if region.occurrence >= len(matches):
            raise WorkEvidenceError("missing enclosing primitive region")
        opening = matches[region.occurrence] + len(fragment) - 1
        if opening not in closings or closings[opening] >= end:
            raise WorkEvidenceError("invalid enclosing compound")
        start, end = opening + 1, closings[opening]
        enclosing = (*enclosing, opening)
    return start, end, enclosing


def validate_placement(projected, spans, site, marker):
    span = spans.get(site.function)
    if span is None:
        raise WorkEvidenceError("projected function is missing")
    body_tokens = positional_tokens(projected[span.body_start:span.body_end])
    values = tuple(token.value for token in body_tokens)
    stacks, closings = compound_layout(values)
    mark_start = boundary_index(body_tokens, marker[0] - span.body_start)
    mark_end = boundary_index(body_tokens, marker[1] - span.body_start)
    if site.placement is Placement.FUNCTION_ENTRY:
        if mark_start != 0 or stacks[mark_start]:
            raise WorkEvidenceError("function-entry observation was relocated")
        return
    start, end, enclosing = resolve_regions(values, stacks, closings, site.regions)
    fragment = tokens(site.primitive.tokens)
    matches = [
        position for position in matches_at(values, fragment, start, end)
        if stacks[position] == enclosing
        and (
            site.placement is Placement.WRAPS or position == 0
            or values[position - 1] in (";", "{", "}")
        )
    ]
    if site.primitive.occurrence >= len(matches):
        raise WorkEvidenceError("missing declared primitive occurrence")
    primitive_start = matches[site.primitive.occurrence]
    primitive_end = primitive_start + len(fragment)
    if stacks[mark_start] != enclosing:
        raise WorkEvidenceError("observation moved across its enclosing compound")
    if site.placement is Placement.WRAPS:
        before = values[primitive_start - 1] if primitive_start else None
        after = values[primitive_end] if primitive_end < len(values) else None
        if before not in (
            None, "(", "[", "{", ";", ",", "=", "return", "&&", "||", "?", ":",
        ) or after not in (
            ")", "]", "}", ";", ",", "&&", "||", "?", ":",
        ):
            raise WorkEvidenceError("comparison primitive lacks complete expression boundaries")
        valid = mark_start == primitive_start and mark_end == primitive_end
    else:
        expected = primitive_start if site.placement is Placement.BEFORE else primitive_end
        valid = mark_start == expected and mark_end == mark_start
    if not valid:
        raise WorkEvidenceError("observation was relocated from its primitive")


def project_observed_source(source_path, source, admission, *, stage):
    if type(source) is not str:
        raise WorkEvidenceError("source must be text")
    declared = declarations(source_path, admission, stage)
    try:
        lexical = c_lexical_text(source)
        if re.search(
            r"^\s*#\s*(?:define|undef)\s+MICROS_(?:TEST_WORK|WORK)_",
            lexical, re.MULTILINE,
        ):
            raise WorkEvidenceError("observation namespace was redefined")
        identifiers = set(re.findall(r"\bMICROS_TEST_WORK_([A-Za-z0-9_]+)\b", lexical))
        if identifiers - {"HIT", "COMPARE", "COUNTS"} or re.search(
            r"\bmicros_test_work_(?:begin|record|finish)\s*\(", lexical,
        ):
            raise WorkEvidenceError("unsupported observation form")
        spans = function_spans(source)
        edits = header_edit(source, lexical, spans, admission.include_header)
        seen = set()
        for match in re.finditer(CALL_PATTERN, lexical):
            edit = observation_edit(source, lexical, spans, declared, match)
            if edit.key in seen:
                raise WorkEvidenceError("duplicate observation site")
            seen.add(edit.key)
            edits.append(edit)
        if seen != set(declared):
            raise WorkEvidenceError("missing observation site")
        projected, markers = provisional_projection(source, edits)
        projected_spans = function_spans(projected)
        for key, site in declared.items():
            validate_placement(projected, projected_spans, site, markers[key])
        return projected
    except AssertionError as error:
        raise WorkEvidenceError(f"unsupported C source: {error}") from error
