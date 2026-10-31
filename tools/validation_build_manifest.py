"""Selected Ninja/CTest artifacts and canonical build-recipe closure."""

import hashlib
import json
from pathlib import Path
import re
import shlex
import stat

from tools.validation_inputs import UnsupportedContext, _file_identity, _inside, _run


class PreparationRequired(UnsupportedContext):
    reason_code = None


class SelectedOutputAbsent(PreparationRequired):
    reason_code = "selected-output-absent"


class DeclaredOutputAbsent(PreparationRequired):
    reason_code = "declared-output-absent"


class SelectedDependencyStale(PreparationRequired):
    reason_code = "selected-dependency-stale"


def _regular_output_state(path, directory):
    directory = Path(directory).resolve(strict=True)
    path = Path(path)
    try:
        information = path.lstat()
    except FileNotFoundError:
        if not _inside(path.parent.resolve(), (directory,)):
            raise UnsupportedContext(
                "absent selected output escapes its build directory"
            )
        return False
    if not stat.S_ISREG(information.st_mode):
        raise UnsupportedContext("selected output is not an ordinary file")
    if not _inside(path.resolve(strict=True), (directory,)):
        raise UnsupportedContext("selected output escapes its build directory")
    return True


def graph_nodes(content):
    nodes = {}
    edges = []
    identifier = r'"(0x[0-9a-fA-F]+)"'
    for line in content.splitlines():
        if line in (
            "digraph ninja {", 'rankdir="LR"', "}",
            "node [fontsize=10, shape=box, height=0.25]", "edge [fontsize=10]",
        ):
            continue
        node = re.fullmatch(
            identifier + r' \[label=("(?:[^"\\]|\\.)*")(, shape=ellipse)?\]', line,
        )
        if node:
            if node[1] in nodes:
                raise UnsupportedContext("duplicate Ninja graph identity")
            try:
                nodes[node[1]] = (json.loads(node[2]), bool(node[3]))
            except ValueError as error:
                raise UnsupportedContext("unsupported Ninja graph escape") from error
            continue
        edge = re.fullmatch(
            identifier + " -> " + identifier
            + r'(?: \[(arrowhead=none(?: style=dotted)?|style=dotted|label=" phony")\])?', line,
        )
        if not edge:
            raise UnsupportedContext("unsupported Ninja graph grammar")
        edges.append((edge[1], edge[2], edge[3]))
    unselected = []
    selected_edges = []
    for first, last, attribute in edges:
        if first not in nodes:
            raise UnsupportedContext("incomplete Ninja input graph")
        if last not in nodes:
            if not nodes[first][1]:
                raise UnsupportedContext("unrecognized incomplete Ninja edge")
            unselected.append(nodes[first][0])
        else:
            selected_edges.append((first, last, attribute))
    if not nodes:
        raise UnsupportedContext("empty Ninja graph")
    return {
        "files": tuple(sorted(value for value, rule in nodes.values() if not rule)),
        "rules": tuple(sorted({value for value, rule in nodes.values() if rule})),
        "edges": tuple(sorted(
            (nodes[first][0], nodes[last][0], attribute or "")
            for first, last, attribute in selected_edges
        )),
        "unselected_outputs": tuple(sorted(unselected)),
    }


def command_tools(content, root, allowed):
    tools = set()
    for line in content.splitlines():
        if any(marker in line for marker in ("$(", "${", "`")):
            raise UnsupportedContext("dynamic command resolution is unsupported")
        lexer = shlex.shlex(line, posix=True, punctuation_chars=";&|<>")
        lexer.whitespace_split = True
        lexer.commenters = ""
        blocks = [[]]
        try:
            for token in lexer:
                if any(
                    part.startswith(("@", "--config")) for part in token.split(",")
                ):
                    raise UnsupportedContext("explicit configuration/response input is unsupported")
                if token == "&&":
                    blocks.append([])
                elif token and all(character in ";&|<>" for character in token):
                    raise UnsupportedContext("unreviewed shell operator")
                else:
                    blocks[-1].append(token)
        except ValueError as error:
            raise UnsupportedContext("unsupported generated command quoting") from error
        for block in blocks:
            if not block:
                raise UnsupportedContext("empty generated command")
            if block == [":"]:
                continue
            if block[0] == "cd":
                if len(block) != 2 or not _inside(Path(block[1]).resolve(), (Path(root),)):
                    raise UnsupportedContext("generated command escapes the repository")
                continue
            if block[0] not in allowed:
                raise UnsupportedContext("unreviewed generated command executable")
            tools.add(block[0])
    return tools


def native_outputs(root, directory, inventory, *, preparation=None):
    source = (Path(root) / "CMakeLists.txt").read_text(encoding="utf-8")
    declared = re.findall(
        r"add_test\(\s*NAME\s+([A-Za-z0-9_-]+)\s+COMMAND\s+([A-Za-z0-9_]+)", source,
    )
    from tests.host.work_count_recipe import native_test_declarations
    declared.extend(native_test_declarations(root))
    expected = dict(declared)
    slow = set(re.findall(
        r"set_tests_properties\(\s*([A-Za-z0-9_-]+)\s+PROPERTIES\s+LABELS\s+slow\s*\)",
        source,
    ))
    tests = inventory.get("tests", [])
    names = [test.get("name") for test in tests]
    if len(expected) != len(declared) or len(names) != len(set(names)) or set(names) != set(expected):
        raise UnsupportedContext("complete native CTest inventory differs from committed tests")
    outputs = set()
    for test in tests:
        name = test["name"]
        command = test.get("command", [])
        if not command or Path(command[0]) != directory / expected[name]:
            raise UnsupportedContext("native executable inventory differs from committed tests")
        properties = {item["name"]: item["value"] for item in test.get("properties", [])}
        if set(properties) - {"WORKING_DIRECTORY", "LABELS"}:
            raise UnsupportedContext("unreviewed CTest runtime properties")
        if properties.get("WORKING_DIRECTORY", str(directory)) != str(directory):
            raise UnsupportedContext("unreviewed CTest working directory")
        if (name in slow) != ("slow" in properties.get("LABELS", [])):
            raise UnsupportedContext("slow CTest model inclusion differs from committed tests")
        for argument in command:
            path = Path(argument)
            if not path.is_absolute():
                raise UnsupportedContext("unclosed native executable or file argument")
            if not _regular_output_state(path, directory):
                if preparation is None:
                    raise UnsupportedContext(
                        "unclosed native executable or file argument"
                    )
                if not any(
                    isinstance(item, DeclaredOutputAbsent)
                    for item in preparation
                ):
                    preparation.append(
                        DeclaredOutputAbsent(
                            "declared native output is absent"
                        )
                    )
            outputs.add(path)
    return tuple(sorted(outputs))


def native_runtime_executables(inventory):
    return tuple(sorted({Path(test["command"][0]) for test in inventory["tests"]}))


def _generator_inputs(directory):
    pending = [directory / "build.ninja", directory / "CMakeCache.txt"]
    pending.extend(sorted(directory.glob("CMakeFiles/*/CMake*.cmake")))
    if not any(path.name == "CMakeCCompiler.cmake" for path in pending):
        raise UnsupportedContext("configured compiler metadata is missing")
    result = {}
    while pending:
        path = pending.pop()
        if not _inside(path.resolve(), (directory,)):
            raise UnsupportedContext("external generator include is unsupported")
        name = str(path.relative_to(directory))
        if name in result:
            continue
        result[name] = _file_identity(path)
        if path.suffix == ".ninja":
            for spelling in re.findall(r"(?m)^(?:include|subninja) (.+)$", path.read_text()):
                if "$" in spelling:
                    raise UnsupportedContext("dynamic generator include is unsupported")
                pending.append(directory / spelling)
    return result


def _dependency_inputs(tools, selected, *, preparation=None):
    content = _run((tools.executables["ninja"]["path"], "-C", tools.profile.directory, "-t", "deps")).decode()
    active = False
    dependencies = set()
    for line in content.splitlines():
        header = re.fullmatch(r"(.+): #deps \d+, deps mtime \d+ \((VALID|STALE)\)", line)
        if header:
            active = header[1] in selected
            if active and header[2] != "VALID":
                if preparation is None:
                    raise UnsupportedContext(
                        "selected compiler dependency record is stale"
                    )
                if not any(
                    isinstance(item, SelectedDependencyStale)
                    for item in preparation
                ):
                    preparation.append(
                        SelectedDependencyStale(
                            "selected dependency record is stale"
                        )
                    )
        elif line.startswith("    ") and active:
            spelling = line[4:]
            path = Path(spelling)
            if not path.is_absolute():
                path = tools.profile.directory / path
            dependencies.add(path)
        elif line:
            raise UnsupportedContext("unrecognized compiler dependency metadata")
    return dependencies


def _selected_graph_files(root, profile, graph, rules, preparation):
    outputs = set()
    inputs = set()
    for spelling in graph["files"]:
        path = Path(spelling)
        if not path.is_absolute():
            path = profile.directory / path
        try:
            information = path.lstat()
        except FileNotFoundError:
            rule = rules.get(spelling)
            if rule is None:
                raise UnsupportedContext(
                    "missing graph node has no reviewed producer rule"
                )
            if rule == "phony":
                continue
            if (
                rule == "CUSTOM_COMMAND"
                and re.fullmatch(r"CMakeFiles/[A-Za-z0-9_.+-]+", spelling)
            ):
                continue
            if preparation is not None and not _regular_output_state(
                path, profile.directory
            ):
                if not any(
                    isinstance(item, SelectedOutputAbsent)
                    for item in preparation
                ):
                    preparation.append(
                        SelectedOutputAbsent(
                            "selected build output is absent"
                        )
                    )
                outputs.add(path)
            else:
                raise UnsupportedContext(
                    "selected artifact/input is missing or unrecognized"
                )
        else:
            if stat.S_ISREG(information.st_mode):
                resolved = path.resolve(strict=True)
                (
                    outputs
                    if _inside(resolved, (profile.directory,))
                    else inputs
                ).add(path)
            elif (
                stat.S_ISDIR(information.st_mode)
                and path in (root, profile.directory)
            ):
                continue
            else:
                raise UnsupportedContext(
                    "selected artifact/input is not an ordinary file"
                )
    return outputs, inputs


def _target_outputs(profile, preparation):
    prefix = (
        "kernel/micros"
        if profile.target == "test-qemu-smoke"
        else "bootstrap/micros-tty-service"
    )
    declared = tuple(
        profile.directory / (prefix + suffix)
        for suffix in (".elf", ".map")
    )
    for path in declared:
        if not _regular_output_state(path, profile.directory):
            if preparation is None:
                raise UnsupportedContext(
                    "declared target output is absent"
                )
            if not any(
                isinstance(item, DeclaredOutputAbsent)
                for item in preparation
            ):
                preparation.append(
                    DeclaredOutputAbsent(
                        "declared target output is absent"
                    )
                )
    return declared


def _build_manifest(root, profile, tools, *, preparation=None):
    ninja = tools.executables["ninja"]["path"]
    graph = graph_nodes(_run((ninja, "-C", profile.directory, "-t", "graph", profile.target)).decode())
    if any(
        rule not in ("phony", "CUSTOM_COMMAND")
        and not rule.startswith(("C_COMPILER__", "ASM_COMPILER__", "C_EXECUTABLE_LINKER__"))
        for rule in graph["rules"]
    ):
        raise UnsupportedContext("unreviewed selected build rule")
    commands = _run((ninja, "-C", profile.directory, "-t", "commands", profile.target)).decode()
    allowed = {
        value for tool in tools.executables.values() for value in tool.values()
    }
    command_tools(commands, root, allowed)
    rules = {}
    for line in _run((ninja, "-C", profile.directory, "-t", "targets", "all")).decode().splitlines():
        name, separator, rule = line.rpartition(": ")
        if not separator:
            raise UnsupportedContext("unrecognized Ninja target metadata")
        rules[name] = rule
    outputs, inputs = _selected_graph_files(
        root, profile, graph, rules, preparation,
    )
    for spelling in rules:
        path = profile.directory / spelling
        if _inside(path.resolve(), (profile.directory,)) and path.is_file():
            outputs.add(path)
    selected = {str(path.relative_to(profile.directory)) for path in outputs}
    inputs.update(
        _dependency_inputs(
            tools,
            selected,
            preparation=preparation,
        )
    )
    inventory = None
    if profile.target_build:
        outputs.update(_target_outputs(profile, preparation))
    else:
        inventory = json.loads(_run((
            tools.executables["ctest"]["path"], "--test-dir", profile.directory, "--show-only=json-v1",
        )))
        outputs.update(
            native_outputs(
                root,
                profile.directory,
                inventory,
                preparation=preparation,
            )
        )
    regular_inputs = {}
    for path in sorted(inputs):
        resolved = path.resolve(strict=True)
        if not _inside(resolved, (root,)):
            tools._admit(resolved)
        regular_inputs[str(path)] = _file_identity(resolved)
    return {
        "generator": _generator_inputs(profile.directory),
        "graph": graph, "commands_sha256": hashlib.sha256(commands.encode()).hexdigest(),
        "compiler_inputs": regular_inputs, "ctest": inventory,
    }, outputs
