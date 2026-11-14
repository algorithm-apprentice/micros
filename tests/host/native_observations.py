"""Current native primitive bindings, without ancestor source or author claims."""

from functools import lru_cache
import json
from pathlib import Path
import re
from types import MappingProxyType

from tests.host.c_source_records import c_lexical_text, file_scope_objects, function_records, tokens
from tests.host.source_snapshot import source_bytes
from tests.host.work_count_lexical import (
    compound_layout, enclosing_regions, function_spans, loop_shape,
    matches_at, matching_tokens, positional_tokens,
)
from tests.host.work_count_projection import resolve_regions
from tests.host.work_count_types import Kind, Owner, Region


ROOT = Path(__file__).resolve().parents[2]
DECLARATION = "tests/host/native_observations.json"
PREFIX = (
    '#include "micros/work_observation.h"\n'
    "#if defined(MICROS_TEST_WORK_COUNTS)\n"
    "#define MICROS_NATIVE_OBSERVE_VALUE(site, owner, kind, value) "
    "(micros_test_work_record((owner), (kind), 1), (value))\n"
    "#else\n#define MICROS_NATIVE_OBSERVE_VALUE(site, owner, kind, value) (value)\n#endif\n"
)
MEASURED_ARRAY_FIELDS = frozenset((
    "owners", "handoff_targets", "allocated_bitmap", "bitmap", "owner_links",
    "process_frames", "words", "positions", "entries", "managed_ranges",
    "processes", "threads", "harts", "endpoints", "owner_counts", "mappings",
    "ready_head", "ready_tail", "requests", "transitions",
))


def direct_calls(body):
    lexical = tokens(body)
    cast = re.compile(
        r"(?:(?:const|volatile)\s+)*(?:"
        r"(?:struct|union|enum)\s+[A-Za-z_]\w*|"
        r"(?:unsigned|signed)(?:\s+(?:char|short|int|long))*|"
        r"size_t|uintptr_t|intptr_t|u?int(?:8|16|32|64)_t|char|short|int|long|bool|void"
        r")(?:\s+(?:\*|const|volatile|restrict))*"
    )
    for index, (left, right) in enumerate(zip(lexical, lexical[1:])):
        if left == ")" and right == "(":
            depth, opening = 1, index - 1
            while opening >= 0:
                depth += (lexical[opening] == ")") - (lexical[opening] == "(")
                if depth == 0:
                    break
                opening -= 1
            if opening < 0 or cast.fullmatch(" ".join(lexical[opening + 1:index])) is None:
                raise ValueError("native observations require a closed direct-call boundary")
    return tuple(sorted(set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", c_lexical_text(body))) - {
        "if", "while", "for", "switch", "return", "sizeof", "_Alignof", "UINT64_C", "UINT32_C", "UINT16_C",
    }))


@lru_cache(maxsize=2048)
def _function_contract(body, guards):
    values = tokens(body)
    stacks, closings = compound_layout(values)
    accesses, aliases, storage = [], [], []
    for index, value in enumerate(values):
        if value == "[":
            end = matching_tokens(values, index)
            accesses.append((
                " ".join(values[max(0, index - 1):end + 1]),
                enclosing_regions(values, stacks, closings, stacks[index]),
            ))
        if value in MEASURED_ARRAY_FIELDS and index and values[index - 1] in (".", "->"):
            aliases.append((value, values[index + 1:index + 2]))
        if value in ("static", "_Thread_local"):
            end = values.index(";", index)
            storage.append(" ".join(values[index:end + 1]))
    return MappingProxyType({
        "guards": guards, "calls": direct_calls(body),
        "loops": loop_shape(body), "accesses": tuple(accesses),
        "aliases": tuple(aliases), "storage": tuple(storage),
        "directives": tuple(
            " ".join(tokens(line)) for line in
            re.sub(r"\\\n", "", c_lexical_text(body)).splitlines()
            if line.lstrip().startswith("#")
        ),
    })


def function_contract(record):
    return {"signature": " ".join(tokens(record.signature)), **_function_contract(record.body, record.guards)}


def validate_function(record, expected):
    if function_contract(record) != expected:
        raise ValueError("native measured function changed its calls, loops, accesses or storage: " + record.name)


def _immutable(value):
    if type(value) is dict:
        return MappingProxyType({key: _immutable(item) for key, item in value.items()})
    if type(value) is list:
        return tuple(_immutable(item) for item in value)
    if type(value) in (str, int):
        return value
    raise ValueError("native observation declaration contains an untyped value")


def _pairs(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError("native observation declaration contains a duplicate field")
        result[key] = value
    return result


@lru_cache(maxsize=4)
def _decode(raw):
    value = json.loads(raw, object_pairs_hook=_pairs)
    if (
        type(value) is not dict or set(value) != {"version", "reference", "source_tree", "sources", "families"}
        or type(value["version"]) is not int or value["version"] != 1
        or type(value["sources"]) is not dict or type(value["families"]) is not dict
    ):
        raise ValueError("native observation declaration has an unknown schema")
    return _immutable(value)


def declarations(root=ROOT):
    return _decode(source_bytes(root, Path(root) / DECLARATION))


def native_inputs(root, family):
    root = Path(root)
    members = declarations(root)["families"][family]["native_sources"]
    if len(set(members)) != len(members) or any(
        (not name.startswith("kernel/")
         and not (family == "GRANT01" and name == "lib/runtime/memory.c"))
        or not name.endswith(".c") or ".." in Path(name).parts
        for name in members
    ):
        raise ValueError("native source membership is incomplete or leaves the project")
    headers = {
        str(path.relative_to(root))
        for directory in ("arch", "include", "kernel")
        for path in (root / directory).rglob("*.h")
    }
    if family == "GRANT01" and "lib/runtime/memory.c" in members:
        headers.add("lib/runtime/memory.h")
    if family in ("MEM04", "MEM05"):
        headers.update(("tests/qemu/vm_validation_work.h", "tests/work/work_counts.h"))
    return tuple(sorted({*members, *headers}))


@lru_cache(maxsize=128)
def _source_records(text):
    return MappingProxyType(function_records(text)), file_scope_objects(text)


@lru_cache(maxsize=128)
def _source_spans(text):
    return MappingProxyType(function_spans(text))


def validate_source(root, path, text):
    declared = declarations(root)["sources"].get(path)
    if declared is None:
        raise ValueError("native source has no current measurement qualification: " + path)
    records, storage = _source_records(text)
    expected = declared["functions"]
    if (
        not expected.keys() <= records.keys()
        or declared["complete"] == 1 and records.keys() != expected.keys()
        or storage != declared["storage"]
    ):
        raise ValueError("native source changed its measured closure or state ownership: " + path)
    for name, contract in expected.items():
        validate_function(records[name], contract)


@lru_cache(maxsize=2048)
def bind_sites(body, declarations):
    positions = positional_tokens(body)
    values = tuple(item.value for item in positions)
    stacks, closings = compound_layout(values)
    result, identities = [], set()
    for site in declarations:
        if len(site) != 10:
            raise ValueError("native primitive declaration has an unknown schema")
        identifier, mode, owner, kind, units, placement, primitive, regions, occurrence, count = site
        if (
            type(identifier) is not str or re.fullmatch(r"[a-zA-Z0-9_./-]+", identifier) is None
            or identifier in identities or owner not in Owner.__members__ or kind not in Kind.__members__
            or type(units) is not int or not 1 <= units < 1 << 64
            or type(occurrence) is not int or occurrence < 0
            or type(count) is not int or count < 0
            or mode not in ("insert", "wrap", "legacy-wrap", "compare")
            or mode != "insert" and units != 1
            or mode == "compare" and kind != "COMPARISON"
            or any(len(row) != 2 or type(row[0]) is not str or type(row[1]) is not int or row[1] < 0 for row in regions)
        ):
            raise ValueError("native primitive has duplicate ownership or invalid dimensions")
        identities.add(identifier)
        start, end, enclosing = resolve_regions(values, stacks, closings, tuple(Region(*row) for row in regions))
        if placement == "entry":
            if mode != "insert" or primitive or occurrence or count:
                raise ValueError("native entry observation names another primitive")
            begin = finish = positions[enclosing[-1]].end if enclosing else 0
        else:
            fragment = tokens(primitive)
            matches = [
                index for index in matches_at(values, fragment, start, end)
                if stacks[index] == enclosing
            ] if fragment else []
            if len(matches) != count or occurrence >= count:
                raise ValueError("native primitive is missing or ambiguous: " + identifier)
            first = matches[occurrence]
            last = first + len(fragment)
            begin, finish = positions[first].start, positions[last - 1].end
            if placement == "before" and mode == "insert":
                finish = begin
            elif placement == "after" and mode == "insert" and fragment[-1] == ";":
                begin = finish
            elif placement != "value" or mode == "insert":
                raise ValueError("native primitive has an unknown placement")
        result.append((identifier, mode, "", owner, kind, begin, finish, body[begin:finish], regions, units))
    positions = tuple(site[5:7] for site in result)
    if positions != tuple(sorted(positions)):
        raise ValueError("native measured primitives moved across their declared ordering boundary")
    return tuple(result)


def _observation_call(site, fragment=None, *, value_macro="MICROS_MEM05_OBSERVE_VALUE", legacy_macro="MICROS_AS_OBSERVE_VALUE"):
    identifier, mode, _, owner, kind, _, _, original, _, units = site
    if mode in ("wrap", "legacy-wrap", "compare"):
        expression = original if fragment is None else fragment
        if mode == "compare":
            return f'MICROS_TEST_WORK_COMPARE("{identifier}", MICROS_WORK_OWNER_{owner}, ({expression}))'
        macro = legacy_macro if mode == "legacy-wrap" else value_macro
        return f'{macro}("{identifier}", MICROS_WORK_OWNER_{owner}, MICROS_WORK_KIND_{kind}, ({expression}))'
    return f'MICROS_TEST_WORK_HIT("{identifier}", MICROS_WORK_OWNER_{owner}, MICROS_WORK_KIND_{kind}, {units});'


def render_body(body, sites, *, value_macro="MICROS_MEM05_OBSERVE_VALUE", legacy_macro="MICROS_AS_OBSERVE_VALUE"):
    if any(site[1] not in ("wrap", "legacy-wrap", "compare", "insert") for site in sites):
        raise ValueError("native observation has an unknown rendering mode")
    wraps = sorted((site for site in sites if site[1] != "insert"), key=lambda site: (site[5], -site[6]))
    top, stack = [], []
    for site in wraps:
        node = [site, []]
        while stack and site[5] >= stack[-1][0][6]:
            stack.pop()
        if stack:
            if site[6] > stack[-1][0][6] or site[5:7] == stack[-1][0][5:7]:
                raise ValueError("native observed expressions have ambiguous overlap")
            stack[-1][1].append(node)
        else:
            top.append(node)
        stack.append(node)

    def call(site, fragment=None):
        return _observation_call(site, fragment, value_macro=value_macro, legacy_macro=legacy_macro)

    def render(node):
        site, children = node
        fragment = body[site[5]:site[6]]
        for child in reversed(children):
            first, last = child[0][5:7]
            fragment = fragment[:first - site[5]] + render(child) + fragment[last - site[5]:]
        return call(site, fragment)

    edits = [(node[0][5], node[0][6], render(node)) for node in top]
    insertions = {}
    for site in sites:
        if site[1] != "insert":
            continue
        if any(node[0][5] < site[5] < node[0][6] for node in top):
            raise ValueError("native statement observation enters an expression")
        insertions.setdefault(site[5], []).append(call(site))
    edits.extend((position, position, "".join(calls)) for position, calls in insertions.items())
    for first, last, replacement in sorted(edits, reverse=True):
        body = body[:first] + replacement + body[last:]
    return body


def observe_source(root, family, path, text, channel):
    declared = declarations(root)
    owner = declared["families"][family]
    if channel not in (*owner["channels"], "ordinary"):
        raise ValueError("native observation has an unknown channel")
    validate_source(root, path, text)
    spans = _source_spans(text)
    for name, sites in owner["sites"].get(path, {}).items():
        span = spans[name]
        bind_sites(text[span.body_start:span.body_end], tuple(site for _, site in sites))
    if channel == "ordinary":
        return text
    for name, sites in sorted(owner["sites"].get(path, {}).items(), key=lambda item: spans[item[0]].body_start, reverse=True):
        span = spans[name]
        body = text[span.body_start:span.body_end]
        selected = tuple(site for channels, site in sites if channel in channels)
        annotated = render_body(
            body, bind_sites(body, selected),
            value_macro="MICROS_NATIVE_OBSERVE_VALUE", legacy_macro="MICROS_NATIVE_OBSERVE_VALUE",
        )
        text = text[:span.body_start] + annotated + text[span.body_end:]
    return text
