"""Current VM primitive/site closure, separate from semantic/work expectations."""

from tests.host.c_source_records import function_records
from tests.host.native_observations import bind_sites, declarations
from tests.host.work_count_lexical import (
    compound_layout, enclosing_regions, matching_tokens, positional_tokens, region_header,
)


INDEX_FIELDS = frozenset((
    "mapping_records", "mappings", "managed_ranges", "frame_states", "initial_free_bitmap",
    "entries", "table_frames", "data_frames", "pages", "segments", "arguments", "environment",
))


def validate_native_sites(root, path, text):
    from tests.host.selected_vm_evidence import NATIVE_SUBSETS

    records = function_records(text)
    if path in NATIVE_SUBSETS:
        records = {name: records[name] for name in NATIVE_SUBSETS[path]}
    family = declarations(root)["families"]["VM01"]
    declared = family["sites"].get(path, {})
    if declared.keys() != records.keys():
        raise ValueError("VM01 declarations omit a compiled function: " + path)
    for name, record in records.items():
        if any(tuple(channels) != ("work",) for channels, _ in declared[name]):
            raise ValueError("VM01 primitive belongs to another observation channel")
        sites = tuple(site for _, site in declared[name])
        bound = bind_sites(record.body, sites)
        positions = positional_tokens(record.body)
        values = tuple(item.value for item in positions)
        stacks, closings = compound_layout(values)
        for index, value in enumerate(values):
            if value == "{":
                first = region_header(values, index)
                if values[first] in ("for", "while"):
                    scope = enclosing_regions(values, stacks, closings, (*stacks[index], index))
                    if not any(site[1] == "insert" and site[5] == "entry" and site[7] == scope for site in sites):
                        raise ValueError("VM01 loop lost its executing primitive: " + name)
            if value != "[" or index < 3 or values[index - 1] not in INDEX_FIELDS:
                continue
            if values[index - 2] not in (".", "->"):
                continue
            end = matching_tokens(values, index)
            extent = (positions[index + 1].start, positions[end - 1].end)
            if sum(site[1] == "wrap" and site[3] == "INDEX_PROBE" and observation[5:7] == extent
                   for site, observation in zip(sites, bound, strict=True)) != 1:
                raise ValueError("VM01 indexed record lost its primitive: " + name)
