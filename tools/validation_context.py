"""Closed, deliberately bounded reusable execution context (ADR-0064)."""

import ctypes
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shlex
import shutil
import stat
import subprocess


PROFILE_NAME = "darwin-arm64-ninja-v1"
PROFILES = {
    "test-unit": ("host-debug", "test-unit", False),
    "test-qemu-smoke": ("riscv64-debug", "test-qemu-smoke", True),
    "build-tty-service-image": ("riscv64-tty-service-image", "micros_tty_service", True),
}
RECIPE_DIGESTS = {
    "CMakeLists.txt": "afb07b0e891e11c677782570ac5a5310dd3cd3d2418c5b741555fe0263a04877",
    "cmake/toolchains/riscv64-clang.cmake": "1a1bbab2b91b54222bfbae93cdf2ae9d7847f521760dcba9119d306d3a36d459",
}
SYSTEM_ROOTS = (Path("/usr/bin"), Path("/usr/lib"), Path("/System/Library"))
STANDARD_SEARCH_ROOTS = (Path("/usr/local/include"), Path("/usr/local/lib"))
VENDOR_STARTUP = {
    "Versions/3.9/lib/python3.9/site-packages/distutils-precedence.pth":
        "7ea7ffef3fe2a117ee12c68ed6553617f0d7fd2f0590257c25c484959a3b7373",
}


class UnsupportedContext(ValueError):
    pass


@dataclass(frozen=True)
class Profile:
    configure: str
    directory: Path
    target: str
    target_build: bool
    cache_variables: dict


def _json_digest(value):
    return hashlib.sha256(
        json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()
    ).hexdigest()


def _inside(path, roots):
    return any(path == root or root in path.parents for root in roots)


def _file_digest(path):
    before = path.stat()
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    after = path.stat()
    if (
        before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_mode,
    ) != (
        after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_mode,
    ):
        raise UnsupportedContext("input changed while being fingerprinted")
    return digest.hexdigest()


def fingerprint_tree(path, admitted_roots):
    path = Path(path)
    roots = tuple(Path(root).resolve() for root in admitted_roots)
    digest = hashlib.sha256()

    def visit(selected, relative):
        information = selected.lstat()
        mode = information.st_mode
        entry = [relative, stat.S_IMODE(mode)]
        if stat.S_ISLNK(mode):
            try:
                resolved = selected.resolve(strict=True)
            except (OSError, RuntimeError) as error:
                raise UnsupportedContext("unresolved manifest link") from error
            if not _inside(resolved, (*roots, *SYSTEM_ROOTS)):
                raise UnsupportedContext("manifest link escapes admitted roots")
            entry.extend(("link", os.readlink(selected), str(resolved)))
        elif stat.S_ISREG(mode):
            entry.extend(("file", information.st_size, _file_digest(selected)))
        elif stat.S_ISDIR(mode):
            entry.append("directory")
        else:
            raise UnsupportedContext("unsupported input kind")
        digest.update(json.dumps(entry, ensure_ascii=True, separators=(",", ":")).encode())
        digest.update(b"\n")
        if stat.S_ISDIR(mode):
            children = sorted(selected.iterdir(), key=lambda child: child.name)
            for child in children:
                visit(child, relative + "/" + child.name)
            after = selected.stat()
            if (information.st_ino, information.st_mtime_ns) != (after.st_ino, after.st_mtime_ns):
                raise UnsupportedContext("directory changed while being fingerprinted")

    visit(path, ".")
    return digest.hexdigest()


def environment_digest(environment):
    return _json_digest(sorted(environment.items()))


def validate_environment(environment, *, target):
    if environment.get("PYTHONNOUSERSITE") != "1":
        raise UnsupportedContext("profile requires PYTHONNOUSERSITE=1")
    forbidden = {
        "LD_PRELOAD", "LD_LIBRARY_PATH", "PYTHONPATH", "PYTHONHOME", "PYTHONSTARTUP",
        "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH",
        "COMPILER_PATH", "GCC_EXEC_PREFIX", "CFLAGS", "CPPFLAGS",
        "CXXFLAGS", "ASFLAGS", "LDFLAGS", "MACOSX_DEPLOYMENT_TARGET",
        "CMAKE_TOOLCHAIN_FILE", "CMAKE_PROJECT_INCLUDE", "CMAKE_PROJECT_INCLUDE_BEFORE",
        "CMAKE_PREFIX_PATH", "CMAKE_MODULE_PATH", "CMAKE_GENERATOR",
        "GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE",
        "CC", "CXX", "AS", "LD", "AR", "RANLIB", "NM",
        "BASH_ENV", "ENV", "SHELLOPTS", "IFS", "GIO_EXTRA_MODULES", "GIO_MODULE_DIR",
        "_PYTHON_SYSCONFIGDATA_NAME",
    }
    rejected = [
        name for name, value in environment.items()
        if value and (name in forbidden or name.startswith(("DYLD_", "QEMU_")))
    ]
    for name in ("SDKROOT", "CPATH", "LIBRARY_PATH"):
        if environment.get(name) and any(
            not Path(value).is_absolute() for value in environment[name].split(os.pathsep)
        ):
            rejected.append(name)
    if rejected:
        raise UnsupportedContext("unsupported inherited environment names: " + ", ".join(sorted(rejected)))
    if target:
        for name in ("MICROS_CLANG", "MICROS_LD_LLD"):
            if not environment.get(name):
                raise UnsupportedContext(f"profile requires explicit {name}")


def validate_sdk_environment(environment, sdk, toolchain):
    if environment.get("SDKROOT") and Path(environment["SDKROOT"]).resolve() != sdk:
        raise UnsupportedContext("SDKROOT differs from the selected active SDK")
    for name in ("CPATH", "LIBRARY_PATH"):
        for spelling in environment.get(name, "").split(os.pathsep):
            if not spelling and not environment.get(name):
                continue
            if Path(spelling) in STANDARD_SEARCH_ROOTS:
                _absent_paths((Path(spelling),))
                continue
            path = Path(spelling).resolve(strict=True)
            if not path.is_dir() or not _inside(path, (sdk, toolchain)):
                raise UnsupportedContext(f"{name} escapes the declared SDK/toolchain roots")


def _absent_paths(paths):
    identities = {}
    for path in paths:
        try:
            path.lstat()
        except FileNotFoundError:
            identities[str(path)] = "absent"
        else:
            raise UnsupportedContext("standard fallback search root exists outside the admitted closure")
    return identities


def validate_python_startup(framework, environment):
    if environment.get("SETUPTOOLS_USE_DISTUTILS", "stdlib") != "stdlib":
        raise UnsupportedContext("vendor distutils import redirection is unsupported")
    if any(framework.rglob("sitecustomize.py")) or any(framework.rglob("usercustomize.py")):
        raise UnsupportedContext("Python import customization is unsupported")
    for path in framework.rglob("*.pth"):
        expected = VENDOR_STARTUP.get(str(path.relative_to(framework)))
        if path.is_symlink() or expected is None or _file_digest(path) != expected:
            raise UnsupportedContext("unreviewed Python startup declaration")


def _run(command, *, cwd=None):
    try:
        completed = subprocess.run(
            tuple(map(str, command)), cwd=cwd, check=False,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise UnsupportedContext(f"metadata query unavailable: {Path(command[0]).name}") from error
    if completed.returncode != 0:
        raise UnsupportedContext(f"metadata query failed: {Path(command[0]).name}")
    return completed.stdout


def platform_identity(*, system=None, machine=None):
    system = platform.system() if system is None else system
    machine = platform.machine() if machine is None else machine
    if (system, machine) != ("Darwin", "arm64"):
        raise UnsupportedContext("only the reviewed Darwin arm64 profile supports reuse")
    seal = _run(("/usr/bin/csrutil", "authenticated-root", "status"))
    volume = plistlib.loads(_run(("/usr/sbin/diskutil", "info", "-plist", "/")))
    if (
        b"enabled" not in seal or volume.get("Sealed") != "Yes"
        or volume.get("APFSSnapshot") is not True
        or not volume.get("APFSSnapshotUUID")
    ):
        raise UnsupportedContext("sealed active system snapshot identity unavailable")
    try:
        function = ctypes.CDLL(None)._dyld_get_shared_cache_uuid
    except AttributeError as error:
        raise UnsupportedContext("active dyld runtime identity unavailable") from error
    value = (ctypes.c_ubyte * 16)()
    function.argtypes = [ctypes.POINTER(ctypes.c_ubyte)]
    function.restype = ctypes.c_bool
    if not function(value) or not any(value):
        raise UnsupportedContext("active dyld runtime identity unavailable")
    return {
        "system": system, "machine": machine,
        "runtime_sha256": _json_digest({
            "snapshot": volume["APFSSnapshotUUID"],
            "version": _run(("/usr/bin/sw_vers",)).decode(),
            "kernel": platform.uname()._asdict(),
            "dyld": bytes(value).hex(),
            "boot": _run(("/usr/sbin/sysctl", "-n", "kern.bootsessionuuid")).decode().strip(),
        }),
    }


def _selected(values, name):
    matching = [value for value in values if value.get("name") == name]
    if len(matching) != 1:
        raise UnsupportedContext("missing or ambiguous preset identity")
    return matching[0]


def resolve_preset(root, workflow):
    root = Path(root).resolve()
    if workflow not in PROFILES:
        raise UnsupportedContext("workflow has no reviewed reusable declaration")
    if (root / "CMakeUserPresets.json").exists():
        raise UnsupportedContext("user presets are outside the supported profile")
    presets = json.loads((root / "CMakePresets.json").read_bytes())
    if "include" in presets:
        raise UnsupportedContext("external preset includes are unsupported")
    configure, target, target_build = PROFILES[workflow]
    selected_workflow = _selected(presets["workflowPresets"], workflow)
    if set(selected_workflow) - {"name", "displayName", "description", "steps"} or selected_workflow["steps"] != [
        {"type": "configure", "name": configure},
        {"type": "build", "name": workflow},
    ]:
        raise UnsupportedContext("workflow recipe differs from the reviewed steps")
    build = _selected(presets["buildPresets"], workflow)
    if (
        set(build) - {"name", "displayName", "description", "configurePreset", "targets"}
        or build.get("configurePreset") != configure or build.get("targets") != [target]
    ):
        raise UnsupportedContext("build preset differs from the reviewed target")
    selected = _selected(presets["configurePresets"], configure)
    inherited = selected.get("inherits")
    if inherited is not None:
        if inherited != "riscv64-debug" or configure != "riscv64-tty-service-image":
            raise UnsupportedContext("unsupported configure inheritance")
        parent = _selected(presets["configurePresets"], inherited)
        selected = {
            **parent, **selected,
            "cacheVariables": {**parent["cacheVariables"], **selected["cacheVariables"]},
        }
    expected_variables = {"CMAKE_BUILD_TYPE": "Debug"}
    if workflow == "test-unit":
        expected_variables["MICROS_BUILD_HOST_TESTS"] = "ON"
    elif workflow == "build-tty-service-image":
        expected_variables["MICROS_BUILD_TTY_SERVICE_IMAGE"] = "ON"
    if (
        set(selected) - {
            "name", "displayName", "description", "inherits", "generator",
            "binaryDir", "cacheVariables", "toolchainFile",
        }
        or selected.get("generator") != "Ninja"
        or selected.get("binaryDir") != "${sourceDir}/build/" + configure
        or selected.get("cacheVariables") != expected_variables
        or selected.get("toolchainFile") != (
            "${sourceDir}/cmake/toolchains/riscv64-clang.cmake" if target_build else None
        )
    ):
        raise UnsupportedContext("configure preset differs from the reviewed profile")
    return Profile(configure, root / "build" / configure, target, target_build, expected_variables)


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


def native_outputs(root, directory, inventory):
    source = (Path(root) / "CMakeLists.txt").read_text(encoding="utf-8")
    declared = re.findall(
        r"add_test\(\s*NAME\s+([A-Za-z0-9_-]+)\s+COMMAND\s+([A-Za-z0-9_]+)", source,
    )
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
            if not path.is_absolute() or not _inside(path.resolve(), (directory,)) or not path.is_file():
                raise UnsupportedContext("unclosed native executable or file argument")
            outputs.add(path)
    return tuple(sorted(outputs))


def native_runtime_executables(inventory):
    return tuple(sorted({Path(test["command"][0]) for test in inventory["tests"]}))


@dataclass(frozen=True)
class Candidate:
    repository: dict
    source: dict
    clean: bool


def capture_candidate(root):
    root = Path(root).resolve()
    actual = Path(_run(("git", "rev-parse", "--show-toplevel"), cwd=root).decode().strip()).resolve()
    if actual != root:
        raise UnsupportedContext("Git metadata does not identify the planner repository")
    untracked = _run(("git", "ls-files", "--others", "--exclude-standard", "-z"), cwd=root)
    if untracked:
        raise UnsupportedContext("untracked task files block validation evidence")
    common = Path(_run(("git", "rev-parse", "--git-common-dir"), cwd=root).decode().strip())
    common = (root / common).resolve()
    repository = {"root": str(root), "git_common": str(common)}
    for name, path in (("worktree", root), ("git_directory", common)):
        information = path.stat()
        repository[name + "_identity"] = [information.st_dev, information.st_ino]
    head = _run(("git", "rev-parse", "--verify", "HEAD"), cwd=root).decode().strip()
    tree = _run(("git", "write-tree"), cwd=root).decode().strip()
    if any(re.fullmatch(r"[a-f0-9]{40}|[a-f0-9]{64}", value) is None for value in (head, tree)):
        raise UnsupportedContext("unsupported candidate object identity")
    tracked = _run(("git", "ls-files", "--stage", "-z"), cwd=root)
    digest = hashlib.sha256()
    for entry in tracked.split(b"\0"):
        if not entry:
            continue
        metadata, separator, filename = entry.partition(b"\t")
        fields = metadata.split()
        if not separator or len(fields) != 3 or fields[2] != b"0":
            raise UnsupportedContext("unmerged or unsupported index entry")
        relative = os.fsdecode(filename)
        path = root / relative
        if not _inside(path.resolve(strict=True), (root,)):
            raise UnsupportedContext("source link escapes the repository")
        digest.update(json.dumps([relative, fields[0].decode()]).encode())
        digest.update(fingerprint_tree(path, (root,)).encode())
    clean = not _run(("git", "diff", "--name-only", "-z"), cwd=root)
    return Candidate(repository, {
        "head": head, "tree": tree, "source_sha256": digest.hexdigest(),
    }, clean)


def _cache(root, profile):
    path = profile.directory / "CMakeCache.txt"
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("#", "//")):
            continue
        key, separator, remaining = line.partition(":")
        kind, equals, value = remaining.partition("=")
        if not separator or not equals or key in values:
            raise UnsupportedContext("unsupported CMake cache grammar")
        values[key] = value
    required = {
        **profile.cache_variables, "CMAKE_GENERATOR": "Ninja",
        "CMAKE_HOME_DIRECTORY": str(root),
    }
    if any(values.get(key) != value for key, value in required.items()):
        raise UnsupportedContext("configured cache differs from the reviewed preset")
    for key, value in values.items():
        if key.startswith("MICROS_BUILD_") and value != (
            "ON" if profile.cache_variables.get(key) == "ON" else "OFF"
        ):
            raise UnsupportedContext("unreviewed additional image configuration")
        if value and (
            key.endswith(("_LAUNCHER", "_RULE_LAUNCH_COMPILE", "_RULE_LAUNCH_LINK"))
            or key in {
                "CMAKE_PROJECT_INCLUDE", "CMAKE_PROJECT_INCLUDE_BEFORE",
                "CMAKE_USER_MAKE_RULES_OVERRIDE", "CMAKE_MODULE_PATH", "CMAKE_PREFIX_PATH",
                "CMAKE_C_COMPILER_ARG1", "CMAKE_ASM_COMPILER_ARG1",
                "CMAKE_C_FLAGS", "CMAKE_ASM_FLAGS", "CMAKE_EXE_LINKER_FLAGS",
                "CMAKE_C_STANDARD_LIBRARIES", "CMAKE_SYSROOT",
            }
        ):
            raise UnsupportedContext("unclosed CMake compiler/project override")
        if key.endswith("_FLAGS_DEBUG") and value not in ("", "-g"):
            raise UnsupportedContext("unreviewed Debug flag override")
    return values


def _file_identity(path):
    information = path.lstat()
    if not stat.S_ISREG(information.st_mode):
        raise UnsupportedContext("artifact/input is not a regular file")
    return {
        "kind": "file", "mode": stat.S_IMODE(information.st_mode),
        "size": information.st_size, "sha256": _file_digest(path),
    }


def _cellar_root(path):
    for prefix in (Path("/opt/homebrew/Cellar"), Path("/usr/local/Cellar")):
        if prefix in path.parents:
            relative = path.relative_to(prefix)
            if len(relative.parts) >= 3:
                return prefix / relative.parts[0] / relative.parts[1]
    raise UnsupportedContext("tool/dependency is outside the admitted installations")


def _system(path):
    return _inside(path, SYSTEM_ROOTS)


class DarwinTools:
    def __init__(self, root, profile, cache, environment):
        self.root = root
        self.profile = profile
        self.cache = cache
        self.environment = environment
        self.roots = set()
        self.executables = {}
        self.versions = {}
        self._visited_libraries = set()
        self.developer = Path(_run(("/usr/bin/xcrun", "--show-sdk-platform-path")).decode().strip()).parents[1]
        self.sdk = Path(_run(("/usr/bin/xcrun", "--show-sdk-path")).decode().strip()).resolve()
        self.clang = Path(_run(("/usr/bin/xcrun", "--find", "clang")).decode().strip()).resolve()
        self.toolchain = self.clang.parent.parent
        self.roots.update((self.sdk, self.toolchain))
        validate_sdk_environment(environment, self.sdk, self.toolchain)
        self._python()
        self._selected_tools()

    def _admit(self, path, *, single=False):
        path = Path(path).resolve(strict=True)
        if _system(path) or _inside(path, tuple(self.roots)):
            return path
        if single:
            self.roots.add(path)
        else:
            self.roots.add(_cellar_root(path))
        return path

    def _tool(self, role, requested, *, single=False):
        if not requested:
            raise UnsupportedContext("required tool cannot be resolved")
        requested = Path(requested)
        path = self._admit(requested, single=single)
        if not path.is_file() or not os.access(path, os.X_OK):
            raise UnsupportedContext("required tool is not executable")
        self.executables[role] = {"path": str(requested), "resolved": str(path)}
        self.versions[role] = hashlib.sha256(_run((requested, "--version"))).hexdigest()
        return path

    def _python(self):
        requested = self.cache.get("MICROS_PYTHON")
        if not requested:
            raise UnsupportedContext("configured Python cannot be resolved")
        if Path(requested).resolve() != Path("/usr/bin/python3"):
            raise UnsupportedContext("initial profile requires the active Xcode Python shim")
        declared_framework = self.developer / "Library/Frameworks/Python3.framework"
        validate_python_startup(declared_framework, self.environment)
        information = json.loads(_run((
            requested, "-B", "-c",
            "import json,sys,site; print(json.dumps({"
            "'executable':sys.executable,'prefix':sys.prefix,'paths':sys.path,"
            "'user_site':site.ENABLE_USER_SITE}))",
        ), cwd=self.root))
        prefix = Path(information["prefix"]).resolve()
        framework = next((path for path in (prefix, *prefix.parents) if path.name == "Python3.framework"), None)
        if framework is None or information["user_site"] is not False:
            raise UnsupportedContext("unclosed Python framework/user-site resolution")
        self.python = Path(information["executable"]).resolve()
        self.roots.add(framework)
        self.roots.add(self.python)
        self.python_paths = []
        for spelling in information["paths"]:
            path = (self.root if not spelling else Path(spelling)).resolve()
            if path != self.root and not _inside(path, (framework,)):
                raise UnsupportedContext("unclosed Python import-search directory")
            if path.name in ("sitecustomize.py", "usercustomize.py"):
                raise UnsupportedContext("Python import customization is unsupported")
            self.python_paths.append({"path": str(path), "exists": path.exists()})
        if any(framework.rglob("sitecustomize.py")) or any(framework.rglob("usercustomize.py")):
            raise UnsupportedContext("Python import customization is unsupported")
        self._tool("python", requested)
        self._tool("python-runtime", self.python)

    def _selected_tools(self):
        cmake = shutil.which("cmake")
        self._tool("cmake", cmake)
        self._tool("ctest", Path(cmake).parent / "ctest")
        self._tool("ninja", self.cache.get("CMAKE_MAKE_PROGRAM"))
        self._tool("compiler", self.cache.get("CMAKE_C_COMPILER"))
        self._tool("native-compiler", self.clang)
        self._tool("git", shutil.which("git"))
        self._tool("probe-make", shutil.which("gmake") or shutil.which("make"))
        for name in ("git", "make"):
            selected = Path(_run(("/usr/bin/xcrun", "--find", name)).decode().strip())
            self._tool("native-" + name, selected, single=True)
        if self.profile.target_build:
            for variable, key, role in (
                ("MICROS_CLANG", "CMAKE_C_COMPILER", "compiler"),
                ("MICROS_LD_LLD", "MICROS_LD_LLD", "linker"),
            ):
                selected = Path(self.environment[variable])
                cached = Path(self.cache[key])
                if not selected.is_absolute() or selected != cached:
                    raise UnsupportedContext("target tool selection differs from its pinned environment")
                self._tool(role, selected)
            self._tool("assembler", self.cache.get("CMAKE_ASM_COMPILER"))
            self._tool("nm", self.cache.get("CMAKE_NM"))
            self._tool("readelf", self.cache.get("CMAKE_READELF"))
        elif Path(self.cache["CMAKE_C_COMPILER"]).resolve() not in (
            Path("/usr/bin/cc"), Path("/usr/bin/clang"), self.clang,
        ):
            raise UnsupportedContext("native compiler differs from the active Xcode selection")

    def _expand(self, spelling, loader, executable):
        if spelling == "@loader_path":
            return loader.parent
        if spelling == "@executable_path":
            return executable.parent
        if spelling.startswith("@loader_path/"):
            return (loader.parent / spelling[len("@loader_path/"):]).resolve()
        if spelling.startswith("@executable_path/"):
            return (executable.parent / spelling[len("@executable_path/"):]).resolve()
        if spelling.startswith("/"):
            return Path(spelling).resolve()
        raise UnsupportedContext("unsupported Mach-O dependency path")

    def libraries(self, executable, *, loader=None, inherited=()):
        executable = Path(executable).resolve()
        loader = executable if loader is None else loader
        key = (loader, executable.parent, inherited)
        if key in self._visited_libraries or _system(loader):
            return
        self._visited_libraries.add(key)
        with loader.open("rb") as source:
            if source.read(4) not in (
                b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf",
                b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
                b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca",
            ):
                raise UnsupportedContext("native runtime input is not a supported Mach-O image")
        commands = _run(("/usr/bin/otool", "-l", "-arch", "arm64", loader)).decode()
        install_ids = set(re.findall(
            r"cmd LC_ID_DYLIB\s+cmdsize \d+\s+name (.+?) \(offset \d+\)", commands,
        ))
        rpaths = tuple(dict.fromkeys(tuple(
            self._expand(value, loader, executable)
            for value in re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset \d+\)", commands)
        ) + inherited))
        for line in _run(("/usr/bin/otool", "-L", "-arch", "arm64", loader)).decode().splitlines()[1:]:
            spelling = line.strip().split(" (compatibility version", 1)[0]
            if not spelling:
                raise UnsupportedContext("unsupported Mach-O library metadata")
            if spelling in install_ids:
                continue
            if spelling.startswith("@rpath/"):
                candidates = {
                    (path / spelling[len("@rpath/"):]).resolve()
                    for path in rpaths if (path / spelling[len("@rpath/"):]).exists()
                }
                if len(candidates) != 1:
                    raise UnsupportedContext(
                        f"unresolved or ambiguous Mach-O rpath: {loader.name} / {Path(spelling).name}"
                    )
                path = candidates.pop()
            else:
                path = self._expand(spelling, loader, executable)
            if _system(path):
                continue
            path = self._admit(path)
            if path != loader:
                self.libraries(executable, loader=path, inherited=rpaths)

    def close_libraries(self, native_executables):
        for tool in self.executables.values():
            self.libraries(Path(tool["resolved"]))
        for path in native_executables:
            self.libraries(path)
        framework = next(path for path in self.roots if path.name == "Python3.framework")
        commands = _run(("/usr/bin/otool", "-l", "-arch", "arm64", self.python)).decode()
        inherited = tuple(
            self._expand(value, self.python, self.python)
            for value in re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset \d+\)", commands)
        )
        for extension in sorted(framework.rglob("*.so")):
            self.libraries(self.python, loader=extension.resolve(), inherited=inherited)

    def qemu(self):
        path = self._tool("qemu", self.cache.get("MICROS_QEMU"))
        installation = _cellar_root(path)
        roots = [
            Path(line).resolve()
            for line in _run((path, "-L", "help")).decode().splitlines()
        ]
        if not roots or any(not _inside(root, (installation,)) for root in roots):
            raise UnsupportedContext("unrecognized QEMU data root")
        firmware = next((
            root / "opensbi-riscv64-generic-fw_dynamic.bin"
            for root in roots if (root / "opensbi-riscv64-generic-fw_dynamic.bin").is_file()
        ), None)
        if firmware is None or not _inside(firmware.resolve(), (installation,)):
            raise UnsupportedContext("default OpenSBI firmware cannot be resolved")
        return {
            "data_roots": [{"path": str(root), "exists": root.exists()} for root in roots],
            "firmware": {"path": str(firmware.resolve()), **_file_identity(firmware.resolve())},
        }


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


def _dependency_inputs(tools, selected):
    content = _run((tools.executables["ninja"]["path"], "-C", tools.profile.directory, "-t", "deps")).decode()
    active = False
    dependencies = set()
    for line in content.splitlines():
        header = re.fullmatch(r"(.+): #deps \d+, deps mtime \d+ \((VALID|STALE)\)", line)
        if header:
            active = header[1] in selected
            if active and header[2] != "VALID":
                raise UnsupportedContext("selected compiler dependency record is stale")
        elif line.startswith("    ") and active:
            spelling = line[4:]
            path = Path(spelling)
            if not path.is_absolute():
                path = tools.profile.directory / path
            dependencies.add(path)
        elif line:
            raise UnsupportedContext("unrecognized compiler dependency metadata")
    return dependencies


def _build_manifest(root, profile, tools):
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
    outputs = set()
    inputs = set()
    for spelling in graph["files"]:
        path = Path(spelling)
        if not path.is_absolute():
            path = profile.directory / path
        if path.is_file():
            (outputs if _inside(path.resolve(), (profile.directory,)) else inputs).add(path)
        elif path in (root, profile.directory) and path.is_dir():
            continue
        elif rules.get(spelling) not in ("phony", "CUSTOM_COMMAND"):
            raise UnsupportedContext("selected artifact/input is missing or unrecognized")
    for spelling in rules:
        path = profile.directory / spelling
        if _inside(path.resolve(), (profile.directory,)) and path.is_file():
            outputs.add(path)
    selected = {str(path.relative_to(profile.directory)) for path in outputs}
    inputs.update(_dependency_inputs(tools, selected))
    inventory = None
    if profile.target_build:
        prefix = "kernel/micros" if profile.target == "test-qemu-smoke" else "bootstrap/micros-tty-service"
        outputs.update(profile.directory / (prefix + suffix) for suffix in (".elf", ".map"))
    else:
        inventory = json.loads(_run((
            tools.executables["ctest"]["path"], "--test-dir", profile.directory, "--show-only=json-v1",
        )))
        outputs.update(native_outputs(root, profile.directory, inventory))
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


def collect(root, workflow):
    """Resolve one reviewed profile completely, or explain why it is ineligible."""
    root = Path(root).resolve()
    try:
        profile = resolve_preset(root, workflow)
        environment = dict(os.environ)
        validate_environment(environment, target=profile.target_build)
        host = platform_identity()
        for name, expected in RECIPE_DIGESTS.items():
            if _file_digest(root / name) != expected:
                raise UnsupportedContext("build recipe is outside the initial reviewed closure")
        if list(root.glob("test_*.py")):
            raise UnsupportedContext("repository-root files would expand the native discovery pattern")
        cache = _cache(root, profile)
        tools = DarwinTools(root, profile, cache, environment)
        qemu = tools.qemu() if workflow == "test-qemu-smoke" else None
        inputs, paths = _build_manifest(root, profile, tools)
        inputs["standard_search_roots"] = _absent_paths(STANDARD_SEARCH_ROOTS)
        native = native_runtime_executables(inputs["ctest"]) if not profile.target_build else ()
        tools.close_libraries(native)
        roots = tuple(sorted(tools.roots))
        inputs["external_roots"] = {
            str(path): fingerprint_tree(path, roots) for path in roots
        }
        python_roots = (root / "tools", root / "tests/host")
        inputs["repository_python"] = {
            str(path.relative_to(root)): fingerprint_tree(path, (*roots, *python_roots))
            for path in python_roots
        }
        inputs["repository_root_names"] = sorted(path.name for path in root.iterdir())
        return {
            "context": {
                "profile": PROFILE_NAME, "workflow": workflow, "platform": host,
                "environment_sha256": environment_digest(environment),
                "tools": tools.executables, "tool_versions": tools.versions,
                "python_paths": tools.python_paths, "qemu": qemu,
            },
            "inputs": inputs,
            "outputs": {
                str(path.relative_to(root)): _file_identity(path)
                for path in sorted(paths)
            },
        }
    except (OSError, UnicodeError, json.JSONDecodeError, KeyError) as error:
        raise UnsupportedContext(f"unclosed profile metadata: {type(error).__name__}") from error
