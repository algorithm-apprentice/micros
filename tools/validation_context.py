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
import shutil
import stat
import sys
from typing import Optional

from tools.validation_build_manifest import (
    DeclaredOutputAbsent,
    SelectedDependencyStale,
    SelectedOutputAbsent,
    _build_manifest,
    native_runtime_executables,
)
from tools.validation_inputs import (
    SYSTEM_ROOTS, UnsupportedContext, _absent_paths, _file_digest, _file_identity,
    _inside, _json_digest, _run, fingerprint_tree,
)
from tools.python_host_runtime import (
    PYTHON_ARGS,
    UnsupportedPythonRuntime,
    validate_environment as validate_python_environment,
)


PROFILE_NAME = "darwin-arm64-ninja-python315-v2"
PREPARATION_REASON_CODES = (
    SelectedOutputAbsent.reason_code,
    DeclaredOutputAbsent.reason_code,
    SelectedDependencyStale.reason_code,
)
PREPARATION_TYPES = (
    SelectedOutputAbsent,
    DeclaredOutputAbsent,
    SelectedDependencyStale,
)
PROFILES = {
    "test-unit": ("host-debug", "test-unit", False),
    "test-qemu-smoke": ("riscv64-debug", "test-qemu-smoke", True),
    "build-tty-service-image": ("riscv64-tty-service-image", "micros_tty_service", True),
}
RECIPE_DIGESTS = {
    "CMakeLists.txt": "fa35a3f3a5ddc9259124a1ef6099952f7514d5038c11a39374cc00f21a23bc95",
    "cmake/toolchains/riscv64-clang.cmake": "1a1bbab2b91b54222bfbae93cdf2ae9d7847f521760dcba9119d306d3a36d459",
}
QEMU_RUNNER_DIGEST = "c48d0bcac58f6bd28695cfa3cf49967052849c58974f3f8f4ac75526c224bc3f"
QEMU_RUNNER_SOURCE_DIGESTS = {
    "tools/run_qemu_smoke.py": QEMU_RUNNER_DIGEST,
    "tools/qemu_smoke_records.py": "c1ddf036528c55ac93dda15522558d37cdd5efb09ed57675f3dea976bb96f8ec",
    "tools/qemu_smoke_platform.py": "e7e4b16fe4c729ae9d0f275c9276a01717ab2e67f12cd666bb4f0fe94f0cea16",
    "tools/qemu_smoke_objects.py": "5998b2bd91c71491e39cf5ad61a0e331aeb8a5bf4586a675aa7564af3a69cf00",
    "tools/qemu_smoke_faults.py": "72f7d248ebe3884b8987471d387f1903dc5686eea9ac2b853e6e2df961fc3bbe",
    "tools/qemu_smoke_command.py": "cfd4ce156aa945eff9233755c36fa885e61696bad5d8bd4f4537772aba504484",
    "tools/qemu_smoke_execution.py": "62a3146a854f6faea843ac40d6675efc381f8c30b153d95ac7395f4109127493",
    "tools/qemu_smoke_outcome.py": "78639dd9d203c0d93fc189277f16347b82f739e7f5f0e3890423c6bb4068f897",
    "tools/qemu_smoke_arguments.py": "e66ac296f6ce79fd71bb1ff56704de67d6414456987126b0fd620ff52607af9c",
    "tools/qemu_smoke_cli.py": "b8646433cd99c534ae9a5d37dff5e0652455e50c60045ed0255f4ce22d5e4019",
}
QEMU_LOADER_PROBE = """
import importlib.machinery as m, json, sys
root = sys.argv[1]
names = json.loads(sys.argv[2])
def metadata(spec):
    if spec is None:
        return {"origin": None, "loader": None, "locations": []}
    return {"origin": spec.origin,
            "loader": type(spec.loader).__name__ if spec.loader is not None else None,
            "locations": list(spec.submodule_search_locations or ())}
namespace = m.PathFinder.find_spec(
    "tools", [root, root + "/tools", *[value or root for value in sys.path]],
)
helpers = {name: metadata(m.PathFinder.find_spec("tools." + name, [root + "/tools"])) for name in names}
print(json.dumps({"implementation": sys.implementation.name,
                  "source": m.SOURCE_SUFFIXES, "bytecode": m.BYTECODE_SUFFIXES,
                  "extensions": m.EXTENSION_SUFFIXES,
                  "namespace": metadata(namespace), "helpers": helpers}))
"""
STANDARD_SEARCH_ROOTS = (Path("/usr/local/include"), Path("/usr/local/lib"))


@dataclass(frozen=True)
class Profile:
    configure: str
    directory: Path
    target: str
    target_build: bool
    cache_variables: dict


@dataclass(frozen=True)
class ProfileReadiness:
    outcome: str
    reason_code: str
    profile: Optional[dict]
    preparation_argv: Optional[tuple]
    detail: Optional[str]


def _validate_qemu_runner_family(root):
    root = Path(root)
    directory = root / "tools"
    if directory.is_symlink() or not directory.is_dir():
        raise UnsupportedContext("QEMU argument builder is outside the reviewed closure")
    for name, expected in QEMU_RUNNER_SOURCE_DIGESTS.items():
        path = root / name
        if path.is_symlink() or not path.is_file() or _file_digest(path) != expected:
            raise UnsupportedContext("QEMU argument builder is outside the reviewed closure")
    names = tuple(Path(name).stem for name in QEMU_RUNNER_SOURCE_DIGESTS if name != "tools/run_qemu_smoke.py")
    forbidden = [
        directory / ("__init__" + suffix) for suffix in (".py", ".pyc", ".pyo")
    ] + [root / ("tools" + suffix) for suffix in (".py", ".pyc", ".pyo")]
    for name in names:
        forbidden.extend((directory / name, directory / (name + ".pyc"), directory / (name + ".pyo")))
    _absent_paths(tuple(forbidden))
    cache = directory / "__pycache__"
    if cache.is_symlink() or cache.exists() and not cache.is_dir():
        raise UnsupportedContext("QEMU argument-builder cache metadata is unsupported")
    if cache.is_dir():
        for entry in cache.iterdir():
            spelling = entry.name.casefold()
            if spelling.endswith((".pyc", ".pyo")) and any(spelling.startswith(name.casefold() + ".") for name in names):
                raise UnsupportedContext("QEMU argument-builder replacement bytecode is unsupported")


def _validate_qemu_runner_namespace(root, tools):
    root = Path(root)
    names = tuple(Path(name).stem for name in QEMU_RUNNER_SOURCE_DIGESTS if name != "tools/run_qemu_smoke.py")
    information = json.loads(_run(
        (
            tools.python,
            *PYTHON_ARGS,
            "-c",
            QEMU_LOADER_PROBE,
            str(root),
            json.dumps(names),
        ),
        cwd=root,
    ))
    if not isinstance(information, dict) or set(information) != {
        "implementation", "source", "bytecode", "extensions", "namespace", "helpers",
    }:
        raise UnsupportedContext("QEMU argument-builder loader metadata is unsupported")
    extensions = information["extensions"]
    if (
        information["implementation"] != "cpython"
        or information["source"] != [".py"] or information["bytecode"] != [".pyc"]
        or not isinstance(extensions, list) or not extensions
        or any(not isinstance(value, str) or not value.startswith(".") or not value.endswith(".so")
               or Path(value).name != value for value in extensions)
    ):
        raise UnsupportedContext("QEMU argument-builder loader layout is unsupported")
    namespace = information["namespace"]
    directory = root / "tools"
    if (
        not isinstance(namespace, dict) or set(namespace) != {"origin", "loader", "locations"}
        or namespace["origin"] is not None or namespace["loader"] is not None
        or not isinstance(namespace["locations"], list)
        or any(not isinstance(value, str) for value in namespace["locations"])
        or {Path(value).resolve() for value in namespace["locations"]} != {directory.resolve()}
    ):
        raise UnsupportedContext("QEMU argument-builder namespace is outside the reviewed closure")
    helpers = information["helpers"]
    if not isinstance(helpers, dict) or set(helpers) != set(names):
        raise UnsupportedContext("QEMU argument-builder helper metadata is incomplete")
    for name in names:
        value = helpers[name]
        if (
            not isinstance(value, dict) or set(value) != {"origin", "loader", "locations"}
            or value["origin"] != str(directory / (name + ".py"))
            or value["loader"] != "SourceFileLoader" or value["locations"] != []
        ):
            raise UnsupportedContext("QEMU argument-builder source loader is outside the reviewed closure")
    forbidden = [directory / ("__init__" + suffix) for suffix in extensions]
    forbidden.extend(root / ("tools" + suffix) for suffix in extensions)
    forbidden.extend(directory / (name + suffix) for name in names for suffix in extensions)
    _absent_paths(tuple(forbidden))


def environment_digest(environment):
    return _json_digest(sorted(environment.items()))


def validate_environment(environment, *, target):
    try:
        validate_python_environment(environment)
    except UnsupportedPythonRuntime as error:
        raise UnsupportedContext(str(error)) from error
    if environment.get("CLANG_NO_DEFAULT_CONFIG") != "1":
        raise UnsupportedContext("profile requires CLANG_NO_DEFAULT_CONFIG=1")
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
        "_PYTHON_SYSCONFIGDATA_NAME", "MICROS_PYTHON_ROOT",
        "CCC_OVERRIDE_OPTIONS", "CCC_ADD_ARGS",
    }
    rejected = [
        name for name, value in environment.items()
        if value and (
            name in forbidden or name == "LLVM_SYMBOLIZER_OPTS"
            or name.startswith((
                "DYLD_", "QEMU_", "ASAN_", "UBSAN_", "LSAN_", "TSAN_",
                "MSAN_", "HWASAN_", "DFSAN_", "SCUDO_", "GWP_ASAN_", "SANITIZER_",
            ))
        )
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


@dataclass(frozen=True)
class Candidate:
    repository: dict
    source: dict
    clean: bool


def _index_entries(tracked, root):
    entries = []
    regular = set()
    for entry in tracked.split(b"\0"):
        if not entry:
            continue
        metadata, separator, filename = entry.partition(b"\t")
        fields = metadata.split()
        if (
            not separator or len(fields) != 3 or fields[2] != b"0"
            or fields[0] not in (b"100644", b"100755", b"120000")
        ):
            raise UnsupportedContext("unmerged or unsupported index entry")
        relative = os.fsdecode(filename)
        path = root / relative
        try:
            mode = path.lstat().st_mode
        except FileNotFoundError:
            entries.append((fields, relative, path, None, None))
            continue
        resolved = path.resolve(strict=True)
        if not _inside(resolved, (root,)):
            raise UnsupportedContext("source link escapes the repository")
        if stat.S_ISREG(mode):
            regular.add(resolved)
        entries.append((fields, relative, path, mode, resolved))
    for fields, _, _, mode, resolved in entries:
        if mode is None:
            continue
        if fields[0] == b"120000":
            if not stat.S_ISLNK(mode) or resolved not in regular:
                raise UnsupportedContext("source link target is not a reviewed regular input")
        elif not stat.S_ISREG(mode):
            raise UnsupportedContext("tracked source kind differs from the index")
    return entries


def _matches_index(path, fields, mode):
    linked = fields[0] == b"120000"
    data = os.fsencode(os.readlink(path)) if linked else path.read_bytes()
    algorithm = {40: "sha1", 64: "sha256"}.get(len(fields[1]))
    if algorithm is None:
        raise UnsupportedContext("unsupported index object format")
    digest = hashlib.new(algorithm, b"blob " + str(len(data)).encode() + b"\0" + data)
    expected_executable = fields[0] == b"100755"
    return digest.hexdigest().encode() == fields[1] and (
        linked or bool(mode & 0o111) == expected_executable
    )


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
    exact = True
    missing = []
    for fields, relative, path, mode, _ in _index_entries(tracked, root):
        digest.update(json.dumps([relative, fields[0].decode()]).encode())
        if mode is None:
            exact = False
            missing.append(relative)
            digest.update(b"\0absent-tracked-input\0")
        else:
            exact = _matches_index(path, fields, mode) and exact
            digest.update(fingerprint_tree(path, (root,)).encode())
    clean = not _run(("git", "diff", "--name-only", "-z"), cwd=root) and exact
    return Candidate(repository, {
        "head": head, "tree": tree, "source_sha256": digest.hexdigest(),
        "missing_paths": missing,
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


def _cellar_root(path):
    for prefix in (Path("/opt/homebrew/Cellar"), Path("/usr/local/Cellar")):
        if prefix in path.parents:
            relative = path.relative_to(prefix)
            if len(relative.parts) >= 3:
                return prefix / relative.parts[0] / relative.parts[1]
    raise UnsupportedContext("tool/dependency is outside the admitted installations")


def _system(path):
    return _inside(path, SYSTEM_ROOTS)


def compiler_version(text, *, target):
    pattern = r"(?m)^(?:Homebrew )?clang version (23)\." if target else r"(?m)^Apple clang version (21)\."
    match = re.search(pattern, text)
    if match is None:
        raise UnsupportedContext("compiler family/major has no reviewed configuration policy")
    return {"family": "LLVMClang" if target else "AppleClang", "major": int(match[1])}


def _selected_target_compiler(path, variable):
    selected = Path(path)
    if (
        not selected.is_absolute()
        or not selected.is_file()
        or not os.access(selected, os.X_OK)
    ):
        raise UnsupportedContext(f"{variable} does not select an executable")
    return selected


def target_compiler(environment, *, system=None):
    selected = environment.get("MICROS_CLANG")
    if selected:
        return _selected_target_compiler(selected, "MICROS_CLANG")
    root = environment.get("MICROS_LLVM_ROOT")
    if root:
        return _selected_target_compiler(
            Path(root) / "bin/clang",
            "MICROS_LLVM_ROOT",
        )
    system = platform.system() if system is None else system
    if system == "Darwin":
        brew = shutil.which("brew")
        if brew is not None:
            try:
                root = Path(
                    _run((brew, "--prefix", "llvm")).decode().strip()
                )
            except UnsupportedContext:
                pass
            else:
                candidate = root / "bin/clang"
                if candidate.exists():
                    return _selected_target_compiler(
                        candidate,
                        "Homebrew LLVM",
                    )
    for name in ("clang", "clang-23", "clang-22"):
        selected = shutil.which(name)
        if selected is not None:
            return _selected_target_compiler(selected, "PATH")
    raise UnsupportedContext("RISC-V Clang cannot be resolved")


def compiler_configuration(installation):
    alias = installation / "etc/clang"
    metadata = {"policy": "automatic-disabled", "alias": {"path": str(alias)}}
    try:
        information = alias.lstat()
    except FileNotFoundError:
        metadata["alias"]["kind"] = "absent"
        return metadata, ()
    if stat.S_ISDIR(information.st_mode) and alias.resolve() == alias:
        metadata["alias"].update(kind="directory", mode=stat.S_IMODE(information.st_mode))
        return metadata, ()
    if not stat.S_ISLNK(information.st_mode) or installation.parent.parent.name != "Cellar":
        raise UnsupportedContext("unrecognized compiler configuration alias")
    expected = installation.parents[2] / "etc/clang"
    try:
        resolved = alias.resolve(strict=True)
        target = expected.lstat()
    except (OSError, RuntimeError) as error:
        raise UnsupportedContext("unresolved compiler configuration alias") from error
    if resolved != expected or not stat.S_ISDIR(target.st_mode):
        raise UnsupportedContext("compiler configuration alias leaves the declared same-prefix root")
    metadata["alias"].update(
        kind="link", mode=stat.S_IMODE(information.st_mode),
        spelling=os.readlink(alias), resolved=str(resolved),
    )
    return metadata, (expected,)


def qemu_configuration(executable, installation):
    if installation.parent.parent.name != "Cellar" or not re.fullmatch(r"11\.[0-9A-Za-z._+-]+", installation.name):
        raise UnsupportedContext("QEMU installation has no reviewed configuration layout")
    expected = installation / "etc/qemu/qemu.conf"
    data = executable.read_bytes()
    spellings = set(re.findall(
        rb"(/[A-Za-z0-9_./@+-]+/etc/qemu/qemu[.]conf)\x00", data,
    ))
    if spellings != {str(expected).encode()}:
        raise UnsupportedContext("QEMU compiled default-config layout is unrecognized")
    absent = _absent_paths((
        expected, installation.parents[2] / "etc/qemu/qemu.conf",
        executable.parent / "qemu-bundle",
    ))
    return {"compiled_default": str(expected), "absent": absent}


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
        self.configuration = {"policy": "automatic-disabled", "alias": None}
        if profile.target_build:
            self.configuration, additional = compiler_configuration(
                _cellar_root(Path(self.executables["compiler"]["resolved"]))
            )
            self.roots.update(additional)

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
        version = _run((
            requested,
            *(PYTHON_ARGS if role in ("python", "python-runtime") else ()),
            "--version",
        ))
        self.versions[role] = {"sha256": hashlib.sha256(version).hexdigest()}
        if role in ("compiler", "assembler", "native-compiler"):
            self.versions[role].update(compiler_version(
                version.decode(), target=self.profile.target_build and role != "native-compiler",
            ))
        if role == "qemu":
            if re.search(r"(?m)^QEMU emulator version 11\.", version.decode()) is None:
                raise UnsupportedContext("QEMU family/major has no reviewed configuration layout")
            self.versions[role]["major"] = 11
        return path

    def _python(self):
        requested = self.cache.get("MICROS_PYTHON")
        if not requested:
            raise UnsupportedContext("configured Python cannot be resolved")
        selected = Path(requested)
        if (
            not selected.is_absolute()
            or self.environment.get("MICROS_PYTHON") != requested
            or selected.resolve(strict=True) != Path(sys.executable).resolve(strict=True)
        ):
            raise UnsupportedContext(
                "reusable profile requires one explicit current MICROS_PYTHON"
            )
        information = json.loads(_run((
            requested,
            *PYTHON_ARGS,
            str(self.root / "tools/python_host_runtime.py"),
            "--selected",
            requested,
            "--metadata",
        ), cwd=self.root))
        expected = {
            "implementation", "version", "releaselevel", "cache_tag",
            "gil_enabled", "py_gil_disabled", "flags", "executable",
            "base_executable", "prefix", "base_prefix", "stdlib", "paths",
            "cafile", "capath", "certificate_count",
        }
        if set(information) != expected:
            raise UnsupportedContext("Python runtime metadata is incomplete")
        prefix = Path(information["prefix"]).resolve()
        framework = next(
            (
                path
                for path in (prefix, *prefix.parents)
                if path.name == "Python.framework"
            ),
            None,
        )
        if framework is None:
            raise UnsupportedContext("unclosed Python framework resolution")
        self.python = Path(information["executable"]).resolve()
        installation = _cellar_root(self.python)
        if installation.parent.name != "python@3.15":
            raise UnsupportedContext("Python is outside the versioned python@3.15 Cellar")
        expected_framework = (
            installation / "Frameworks/Python.framework"
        ).resolve(strict=True)
        if framework != expected_framework:
            raise UnsupportedContext(
                "Python framework differs from the selected installation"
            )
        self.framework = framework
        self.python_installation = installation
        self.python_inert_roots = (
            installation / "lib/python3.15/site-packages",
            framework / "Versions/3.15/lib/python3.15/site-packages",
        )
        self.roots.add(installation)
        self.python_paths = []
        for spelling in information["paths"]:
            path = Path(spelling).resolve()
            if not _inside(path, (self.root, framework)):
                raise UnsupportedContext("unclosed Python import-search directory")
            self.python_paths.append({"path": str(path), "exists": path.exists()})
        spellings = [selected]
        formula = next(
            (
                path
                for path in selected.parents
                if path.name == "python@3.15" and path.parent.name == "opt"
            ),
            None,
        )
        if formula is not None:
            spellings.append(formula)
        self.python_spellings = {
            str(path): fingerprint_tree(path, (installation, framework))
            for path in spellings
        }
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

    def target_preprocessor(self):
        if not (
            self.environment.get("MICROS_CLANG")
            or self.environment.get("MICROS_LLVM_ROOT")
        ):
            raise UnsupportedContext(
                "test-unit reuse requires an explicit target compiler"
            )
        requested = target_compiler(self.environment)
        resolved = requested.resolve(strict=True)
        configuration, additional = compiler_configuration(
            _cellar_root(resolved)
        )
        self.roots.update(additional)
        path = self._admit(requested)
        role = "target-preprocessor"
        self.executables[role] = {
            "path": str(requested),
            "resolved": str(path),
        }
        version = _run((requested, "--version"))
        self.versions[role] = {
            "sha256": hashlib.sha256(version).hexdigest(),
            **compiler_version(version.decode(), target=True),
        }
        return configuration

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
        commands = _run(("/usr/bin/otool", "-l", "-arch", "arm64", self.python)).decode()
        inherited = tuple(
            self._expand(value, self.python, self.python)
            for value in re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset \d+\)", commands)
        )
        for extension in sorted(self.framework.rglob("*.so")):
            self.libraries(self.python, loader=extension.resolve(), inherited=inherited)

    def qemu(self):
        requested = self.cache.get("MICROS_QEMU")
        if not requested:
            raise UnsupportedContext("configured QEMU is missing")
        path = Path(requested).resolve(strict=True)
        installation = _cellar_root(path)
        configuration = qemu_configuration(path, installation)
        self._tool("qemu", requested)
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
            "configuration": configuration,
        }


def collect(root, workflow, *, preparation=None):
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
        if workflow == "test-qemu-smoke":
            _validate_qemu_runner_family(root)
        if list(root.glob("test_*.py")):
            raise UnsupportedContext("repository-root files would expand the native discovery pattern")
        cache = _cache(root, profile)
        tools = DarwinTools(root, profile, cache, environment)
        target_preprocessor_configuration = (
            tools.target_preprocessor()
            if workflow == "test-unit"
            else None
        )
        if workflow == "test-qemu-smoke":
            _validate_qemu_runner_namespace(root, tools)
        qemu = tools.qemu() if workflow == "test-qemu-smoke" else None
        inputs, paths = _build_manifest(
            root,
            profile,
            tools,
            preparation=preparation,
        )
        inputs["standard_search_roots"] = _absent_paths(STANDARD_SEARCH_ROOTS)
        inputs["compiler_configuration"] = tools.configuration
        if target_preprocessor_configuration is not None:
            inputs["target_preprocessor_configuration"] = (
                target_preprocessor_configuration
            )
        native = native_runtime_executables(inputs["ctest"]) if not profile.target_build else ()
        if preparation is not None:
            native = tuple(path for path in native if path.is_file())
        tools.close_libraries(native)
        roots = tuple(sorted(tools.roots))
        inputs["external_roots"] = {}
        for path in roots:
            inputs["external_roots"][str(path)] = fingerprint_tree(
                path,
                roots,
                inert_link_roots=(
                    tools.python_inert_roots
                    if path == tools.python_installation
                    else ()
                ),
            )
        inputs["python_spellings"] = tools.python_spellings
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
                if preparation is None or path.is_file()
            },
        }
    except (OSError, UnicodeError, json.JSONDecodeError, KeyError) as error:
        raise UnsupportedContext(f"unclosed profile metadata: {type(error).__name__}") from error


def collect_readiness(root, workflow):
    preparation = []
    try:
        profile = collect(root, workflow, preparation=preparation)
    except UnsupportedContext as error:
        return ProfileReadiness(
            "UNSUPPORTED",
            "profile-unsupported",
            None,
            None,
            str(error),
        )
    if preparation:
        condition = next(
            item
            for kind in PREPARATION_TYPES
            for item in preparation
            if isinstance(item, kind)
        )
        return ProfileReadiness(
            "PREPARE",
            condition.reason_code,
            None,
            ("cmake", "--workflow", "--preset", workflow),
            str(condition),
        )
    return ProfileReadiness("READY", "ready", profile, None, None)
