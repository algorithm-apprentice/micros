#!/usr/bin/env -S python3.15 -E -S -B

"""Observe PLAT03's eighteen fixed scenarios on the actual RISC-V kernel."""

import argparse
from dataclasses import asdict, dataclass
from functools import lru_cache
import hashlib
import io
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tarfile
import tempfile
import time
import traceback
import uuid

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.panic_trace_evidence import (
    DESIGN_PINS, GuestState, TraceCapture, TraceCase, require_case, run_oracles, validate_case,
)
from tests.host.panic_trace_source import (
    RECIPE_ADMISSION, SOURCE_OVERLAYS, SOURCE_PROJECTION, SOURCE_ROLES,
    extend_cmake, extend_presets, observe_source,
    project_observations, project_trace_cmake, project_trace_presets,
    validate_trace_helper_instructions,
)
from tools.check_terminal_failures import (
    MAX_PACKET, SUPPORTED, _data_symbol, _function_instructions, _symbol,
    decode_packet, encode_packet, require_stop,
)
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import encoded, owned_artifact, publish_json, snapshot_files
from tools.qemu_smoke_command import build_qemu_command
from tools.user_elf_parse import load_elf


ROOT = Path(__file__).resolve().parent.parent
SELECTOR = "micros_panic_trace_test_scenario"
REGISTERS = (2, 8, 10, 11, 16, 17, 32)
OBJECT_SIZES = {
    SELECTOR: 4,
    "micros_panic_trace_test_observation": 192,
    "micros_panic_trace_test_work_snapshot": 3056,
    "panic_control": 4,
    "micros_panic_trace_test_history": 12012,
}
GUEST_ADDITIONS = (
    "tests/qemu/panic_trace_kernel.c",
    "tests/qemu/panic_trace_kernel.h",
    "tests/qemu/panic_trace_kernel.S",
)


@dataclass(frozen=True)
class ImageBinding:
    image_sha256: str
    objects: tuple
    stops: tuple
    stacks: tuple
    text: tuple
    trace_present: bool
    fault_pc: int | None = None


class Remote:
    def __init__(self, connection, binding, scenario, deadline):
        require_case(scenario)
        if type(binding) is not ImageBinding or type(binding.trace_present) is not bool:
            raise ValueError("PLAT03 debugger requires an exact bound image/source stage")
        self.connection = connection
        self.binding = binding
        self.scenario = scenario
        self.deadline = deadline
        self.objects = {name: (address, size) for name, address, size in binding.objects}
        self.stacks = {name: (bottom, top) for name, bottom, top in binding.stacks}
        allowed = {"kernel_main", "armed", "reset", "error"}
        if binding.trace_present and scenario != 18:
            allowed.add("started")
        if binding.trace_present and scenario in (15, 16):
            allowed.add("reentry")
        self.stops = {name: (address, width) for name, address, width in binding.stops if name in allowed}
        if (
            len(self.objects) != len(binding.objects)
            or any(name not in OBJECT_SIZES or size != OBJECT_SIZES[name]
                   for name, (_, size) in self.objects.items())
            or set(self.stacks) != {"boot", "primary", "thread", "emergency"}
            or len(self.stacks) != len(binding.stacks)
            or len({name for name, _, _ in binding.stops}) != len(binding.stops)
            or len(set(self.stops.values())) != len(self.stops)
        ):
            raise ValueError("PLAT03 debugger binding has duplicate or undeclared authority")
        self.stopped_at = None
        self.stack_authority = None
        self.selected = False
        self.buffer = bytearray()
        self.packets = []

    def remaining(self):
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("PLAT03 eight-second VM deadline expired")
        return remaining

    def _receive(self):
        self.connection.settimeout(self.remaining())
        data = self.connection.recv(4096)
        if not data:
            raise ValueError("PLAT03 debugger disconnected before its declared observation")
        self.buffer.extend(data)
        if len(self.buffer) > MAX_PACKET + 4:
            raise ValueError("PLAT03 debugger response exceeded its packet bound")

    def _take(self, length):
        while len(self.buffer) < length:
            self._receive()
        value = bytes(self.buffer[:length])
        del self.buffer[:length]
        return value

    def _require_request(self, payload):
        if payload in (SUPPORTED, b"?", b"c"):
            return
        if payload in tuple(f"p{register:x}".encode() for register in REGISTERS):
            return
        breakpoint = re.fullmatch(rb"([Zz])1,([0-9a-f]+),([24])", payload)
        if breakpoint and (int(breakpoint[2], 16), int(breakpoint[3])) in self.stops.values():
            return
        read = re.fullmatch(rb"m([0-9a-f]+),([0-9a-f]+)", payload)
        if read:
            address, size = (int(value, 16) for value in read.groups())
            if not 1 <= size <= 1024:
                raise ValueError("PLAT03 debugger read exceeds its exact chunk bound")
            if self.stopped_at == "kernel_main":
                if self.objects.get(SELECTOR) == (address, size):
                    return
            elif self.stopped_at in ("armed", "started", "reentry", "reset", "error"):
                if any(start <= address and address + size <= start + extent
                       for start, extent in self.objects.values()):
                    return
                if self.stopped_at == "started" and self.stack_authority is not None:
                    bottom, top = self.stack_authority
                    if bottom <= address and address + size <= top:
                        return
        if SELECTOR in self.objects:
            address, size = self.objects[SELECTOR]
            expected = f"M{address:x},4:{self.scenario.to_bytes(4, 'little').hex()}".encode()
            if self.stopped_at == "kernel_main" and not self.selected and size == 4 and payload == expected:
                return
        raise ValueError("PLAT03 debugger request exceeds its exact case/register/object/stack authority")

    def exchange(self, payload, *, reply=True):
        if type(reply) is not bool or not reply and (payload != b"c" or self.stopped_at != "reset"):
            raise ValueError("PLAT03 only omits the reply when continuing its final reset instruction")
        packet = encode_packet(payload)
        self._require_request(payload)
        self.connection.settimeout(self.remaining())
        self.connection.sendall(packet)
        if payload.startswith(b"M"):
            self.selected = True
        if payload == b"c":
            self.stopped_at = None
            self.stack_authority = None
        if self._take(1) != b"+":
            raise ValueError("PLAT03 debugger did not acknowledge the exact request")
        if not reply:
            self.packets.append((payload.decode(), None))
            return None
        if self._take(1) != b"$":
            raise ValueError("PLAT03 debugger did not frame its response")
        while b"#" not in self.buffer:
            self._receive()
        size = self.buffer.index(b"#") + 3
        response = decode_packet(b"$" + self._take(size))
        if self.buffer:
            raise ValueError("PLAT03 debugger supplied an unsolicited extra response")
        self.connection.settimeout(self.remaining())
        self.connection.sendall(b"+")
        self.packets.append((payload.decode(), response.decode("ascii")))
        return response

    def handshake(self):
        response = self.exchange(SUPPORTED)
        if not response.startswith(b"PacketSize=") or b"qXfer:features:read+" not in response.split(b";"):
            raise ValueError("PLAT03 debugger lacks the fixed QEMU protocol")
        size = response.split(b";", 1)[0].partition(b"=")[2]
        if re.fullmatch(rb"[0-9a-f]+", size) is None or int(size, 16) < 4096:
            raise ValueError("PLAT03 debugger packet capacity is unsupported")
        require_stop(self.exchange(b"?"))

    def breakpoint(self, name, enabled):
        if type(enabled) is not bool or name not in self.stops:
            raise ValueError("PLAT03 breakpoint is not admitted for the selected case")
        address, width = self.stops[name]
        if self.exchange(f"{'Z' if enabled else 'z'}1,{address:x},{width}".encode()) != b"OK":
            raise ValueError("PLAT03 debugger rejected a bound breakpoint")

    def register(self, number):
        if type(number) is not int or number not in REGISTERS:
            raise ValueError("PLAT03 register is outside its closed integer witness")
        response = self.exchange(f"p{number:x}".encode())
        if re.fullmatch(rb"[0-9a-f]{16}", response) is None:
            raise ValueError("PLAT03 debugger returned an incomplete RV64 register")
        return int.from_bytes(bytes.fromhex(response.decode()), "little")

    def resume_to(self, expected):
        if type(expected) is not tuple or not expected or any(name not in self.stops for name in expected):
            raise ValueError("PLAT03 continuation has an undeclared checkpoint")
        require_stop(self.exchange(b"c"))
        pc = self.register(32)
        names = tuple(name for name, (address, _) in self.stops.items() if address == pc)
        if len(names) != 1 or names[0] not in expected:
            raise ValueError(f"PLAT03 reached an unexpected breakpoint at {pc:#x}")
        self.stopped_at = names[0]
        return names[0]

    def select_scenario(self):
        if self.stopped_at != "kernel_main" or self.selected or self.objects.get(SELECTOR, (0, 0))[1] != 4:
            raise ValueError("PLAT03 may write its selector only once at kernel_main")
        address, _ = self.objects[SELECTOR]
        if self.exchange(f"M{address:x},4:{self.scenario.to_bytes(4, 'little').hex()}".encode()) != b"OK":
            raise ValueError("PLAT03 debugger did not write the exact selector")

    def _memory(self, address, size):
        response = self.exchange(f"m{address:x},{size:x}".encode())
        if re.fullmatch(rb"[0-9a-f]{" + str(2 * size).encode() + rb"}", response) is None:
            raise ValueError("PLAT03 debugger returned incomplete bounded memory")
        return bytes.fromhex(response.decode())

    def read_object(self, name):
        if name not in self.objects:
            raise ValueError("PLAT03 object has no declared ELF binding")
        address, size = self.objects[name]
        return b"".join(self._memory(address + offset, min(1024, size - offset))
                        for offset in range(0, size, 1024))

    def read_stack(self):
        self.stack_authority = None
        if self.stopped_at != "started" or not self.binding.trace_present or self.scenario == 18:
            raise ValueError("PLAT03 stack reads require the actual before-read trace stop")
        name = (
            "primary" if self.scenario == 3 else "thread" if self.scenario in (4, 13, 14)
            else "emergency" if self.scenario == 5 else "boot"
        )
        bottom, top = self.stacks[name]
        sp = self.register(2)
        if sp % 16 or not bottom <= sp <= top - 16:
            raise ValueError("PLAT03 stopped SP does not authenticate its expected physical stack")
        self.stack_authority = bottom, top
        return b"".join(self._memory(address, min(1024, top - address)) for address in range(bottom, top, 1024))


def bind_image(path, *, baseline):
    if type(baseline) is not bool:
        raise ValueError("PLAT03 image requires its exact source stage")
    image = load_elf(path)
    if (image.header.elf_type, image.header.machine, image.header.version, image.header.flags) != (2, 243, 1, 1):
        raise ValueError("PLAT03 requires its ordinary RV64 kernel ELF")
    objects = []
    for name, size in OBJECT_SIZES.items():
        alignment = 4 if name in (SELECTOR, "panic_control", "micros_panic_trace_test_history") else 8
        symbol = _data_symbol(image, name, size, alignment, 8)
        if image.sections[symbol.section_index].name != ".bss":
            raise ValueError("PLAT03 observation/input object is outside ordinary BSS: " + name)
        objects.append((name, symbol.value, symbol.size))

    def stack(lower, upper, section_name, size):
        first, last = _symbol(image, lower), _symbol(image, upper)
        if (
            not 0 < first.section_index < len(image.sections)
            or first.section_index != last.section_index
            or first.value % 16 or last.value % 16 or last.value - first.value != size
        ):
            raise ValueError("PLAT03 stack symbols lack their exact size/alignment/section")
        section = image.sections[first.section_index]
        if (
            (section.name, section.section_type, section.flags) != (section_name, 8, 3)
            or not section.address <= first.value < last.value <= section.address + section.size
        ):
            raise ValueError("PLAT03 stack symbols are outside their owned NOBITS allocation")
        owners = tuple(program for program in image.programs if (
            program.program_type == 1 and program.flags == 6
            and program.virtual_address == program.physical_address
            and program.virtual_address <= first.value < last.value <= program.virtual_address + program.memory_size
        ))
        if len(owners) != 1:
            raise ValueError("PLAT03 stack lacks one writable, non-executable load owner")
        return first.value, last.value

    boot = stack("__boot_stack_bottom", "__boot_stack_top", ".stack", 16384)
    primary = stack("__trap_stack_bottom", "__trap_stack_top", ".trap_stacks", 16384)
    emergency = stack("__trap_emergency_stack_bottom", "__trap_emergency_stack_top", ".trap_stacks", 4096)
    bank = stack("micros_thread_kernel_stacks", "micros_thread_kernel_stacks_end", ".thread_kernel_stacks", 128 * 16384)
    if bank[0] % 4096:
        raise ValueError("PLAT03 thread bank has lost its page-aligned slot geometry")
    allocations = boot, primary, emergency, bank
    for index, (bottom, top) in enumerate(allocations):
        if any(bottom < other_top and other_bottom < top for other_bottom, other_top in allocations[index + 1:]):
            raise ValueError("PLAT03 stack allocations overlap")
        if any(address < top and bottom < address + size for _, address, size in objects):
            raise ValueError("PLAT03 observation/input aliases a kernel stack")
    for index, (name, address, size) in enumerate(objects):
        if any(address < start + extent and start < address + size for _, start, extent in objects[index + 1:]):
            raise ValueError("PLAT03 named observation/control/input objects overlap")
        if name == "panic_control":
            selected = _symbol(image, name, symbol_type=1)
            for other in image.symbols:
                if other != selected and other.symbol_type == 1 and other.size and other.section_index != 0:
                    if address < other.value + other.size and other.value < address + size:
                        raise ValueError("PLAT03 reset control aliases another object")
    text = tuple(_symbol(image, name).value for name in ("__kernel_text_start", "__kernel_text_end"))
    if not 0 < text[0] < text[1] or text[0] % 4096 or text[1] % 4096:
        raise ValueError("PLAT03 text bounds do not name the fixed executable interval")
    if any(bottom < text[1] and text[0] < top for bottom, top in allocations):
        raise ValueError("PLAT03 stack aliases executable text")
    trace_names = (
        "micros_panic_trace_report", "micros_panic_trace_frame_pointer", "micros_panic_trace_load_word",
    )
    trace_symbols = tuple(symbol for symbol in image.symbols if (
        symbol.name in trace_names and symbol.symbol_type == 2 and symbol.section_index != 0
    ))
    fault_symbols = tuple(symbol for symbol in image.symbols if (
        symbol.name == "micros_panic_trace_test_load_fault" and symbol.section_index != 0
    ))
    fault_pc = None
    if baseline:
        if trace_symbols or fault_symbols:
            raise ValueError("PLAT03 baseline contains a substitute trace implementation")
    else:
        _function_instructions(image, "micros_panic_trace_report")
        fault_pc = validate_trace_helper_instructions(
            _function_instructions(image, "micros_panic_trace_frame_pointer"),
            _function_instructions(image, "micros_panic_trace_load_word"),
            selector=_data_symbol(image, SELECTOR, 4, 4, 8).value,
        )
        if len(fault_symbols) != 1 or fault_symbols[0].value != fault_pc:
            raise ValueError("PLAT03 fault witness does not bind the exact null-load instruction")
    stops = []
    for name, function in (
        ("kernel_main", "kernel_main"), ("armed", "micros_panic_trace_test_armed"),
        ("started", "micros_panic_trace_test_started"), ("reentry", "micros_panic_report"),
    ):
        address, width, _ = _function_instructions(image, function)[0]
        stops.append((name, address, width))
    for name, function, instruction in (
        ("reset", "sbi_system_reset", 0x73),
        ("error", "micros_panic_trace_test_error", 0x10500073),
    ):
        matches = tuple((address, width) for address, width, word in _function_instructions(image, function)
                        if width == 4 and word == instruction)
        if len(matches) != 1:
            raise ValueError("PLAT03 checkpoint lacks one exact actual instruction: " + name)
        stops.append((name, *matches[0]))
    if len({address for _, address, _ in stops}) != len(stops):
        raise ValueError("PLAT03 checkpoint addresses are ambiguous")
    stacks = (
        ("boot", *boot), ("primary", *primary), ("emergency", *emergency),
        ("thread", bank[0] + 16384, bank[0] + 2 * 16384),
    )
    return ImageBinding(hashlib.sha256(image.data).hexdigest(), tuple(objects), tuple(stops), stacks, text, not baseline, fault_pc)


def fixture_return_sites(path):
    image = load_elf(path)
    result = []
    for caller, callee in (
        ("trace_chain_two", "trace_chain_one"),
        ("trace_chain_three", "trace_chain_two"),
        ("micros_panic_trace_test_run", "trace_chain_three"),
    ):
        target = _symbol(image, callee, symbol_type=2).value
        decoded = _function_instructions(image, caller)
        matches = []
        for (address, width, word), (next_address, next_width, next_word) in zip(decoded, decoded[1:]):
            register = word >> 7 & 31
            if (
                width != 4 or word & 0x7f != 0x17 or register == 0
                or next_address != address + 4 or next_width != 4
                or next_word & 0x707f != 0x67 or next_word >> 7 & 31 != 1
                or next_word >> 15 & 31 != register
            ):
                continue
            high = word & 0xfffff000
            high -= (1 << 32) if high & (1 << 31) else 0
            low = next_word >> 20
            low -= 4096 if low & 2048 else 0
            actual = ((address + high + low) & 0xffffffffffffffff) & ~1
            if actual == target:
                matches.append(next_address + 4)
        if len(matches) != 1:
            raise ValueError("PLAT03 fixture lacks one actual non-tail caller return site: " + caller)
        result.append(matches[0])
    return tuple(result)


def capture_state(remote):
    return GuestState(
        remote.read_object("micros_panic_trace_test_observation"),
        remote.read_object("micros_panic_trace_test_work_snapshot"),
        remote.read_object("panic_control"),
        remote.register(2),
        remote.register(8),
    )


def _cleanup_process(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2)


def run_case(image, binding, scenario, qemu, *, baseline):
    require_case(scenario)
    if type(baseline) is not bool or binding.trace_present != (not baseline):
        raise ValueError("PLAT03 execution cannot borrow another source stage")
    if read_regular(image)[1]["sha256"] != binding.image_sha256:
        raise ValueError("PLAT03 kernel changed before its actual VM")
    return_sites = fixture_return_sites(image) if scenario in (1, 2) else ()
    process = None
    with (
        tempfile.TemporaryDirectory(prefix="pt3-gdb-", dir=ROOT / "build") as temporary,
        tempfile.TemporaryFile() as output_file,
        tempfile.TemporaryFile() as diagnostic_file,
    ):
        address = str(Path(temporary) / "gdb")
        command = build_qemu_command(qemu=qemu, kernel=str(image)) + ["-S", "-gdb", "unix:" + address + ",server=off"]
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
            listener.bind(address)
            listener.listen(1)
            deadline = time.monotonic() + 8
            try:
                process = subprocess.Popen(
                    command, stdin=subprocess.DEVNULL, stdout=output_file, stderr=diagnostic_file,
                )
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("PLAT03 startup exhausted the absolute VM deadline")
                listener.settimeout(remaining)
                connection, _ = listener.accept()
                with connection:
                    remote = Remote(connection, binding, scenario, deadline)
                    remote.handshake()
                    if process.poll() is not None:
                        raise ValueError("PLAT03 QEMU exited before the live debugger handshake")
                    for name in ("kernel_main", "armed", "reset", "error"):
                        remote.breakpoint(name, True)
                    if remote.resume_to(("kernel_main", "error")) != "kernel_main":
                        raise ValueError("PLAT03 fixture failed before its kernel selector")
                    if remote.read_object(SELECTOR) != bytes(4):
                        raise ValueError("PLAT03 selector is not genuine cleared BSS")
                    remote.select_scenario()
                    remote.breakpoint("kernel_main", False)
                    if remote.resume_to(("armed", "error")) != "armed":
                        raise ValueError("PLAT03 fixture failed before its armed checkpoint")
                    armed = capture_state(remote)
                    history = remote.read_object("micros_panic_trace_test_history")
                    remote.breakpoint("armed", False)
                    started = stack = reentry = None
                    if "started" in remote.stops:
                        remote.breakpoint("started", True)
                    expected = ("started", "reset", "error") if "started" in remote.stops else ("reset", "error")
                    stopped = remote.resume_to(expected)
                    if stopped == "started":
                        started = capture_state(remote)
                        stack = remote.read_stack()
                        remote.breakpoint("started", False)
                        if "reentry" in remote.stops:
                            remote.breakpoint("reentry", True)
                        expected = ("reentry", "reset", "error") if "reentry" in remote.stops else ("reset", "error")
                        stopped = remote.resume_to(expected)
                    if stopped == "reentry":
                        reentry = tuple((number, remote.register(number)) for number in REGISTERS)
                        remote.breakpoint("reentry", False)
                        stopped = remote.resume_to(("reset", "error"))
                    if stopped != "reset":
                        raise ValueError("PLAT03 actual fixture entered its error stop")
                    final = capture_state(remote)
                    registers = tuple((number, remote.register(number)) for number in REGISTERS)
                    capture = TraceCapture(
                        scenario, armed, started, final, history, stack, registers, reentry,
                        binding.stacks, binding.text, binding.stops, return_sites,
                    )
                    remote.breakpoint("reset", False)
                    remote.breakpoint("error", False)
                    remote.exchange(b"c", reply=False)
                    process.wait(timeout=remote.remaining())
                    remote.remaining()
                output_file.seek(0)
                diagnostic_file.seek(0)
                output = output_file.read(65537)
                diagnostic = diagnostic_file.read(65537)
                if len(output) > 65536 or len(diagnostic) > 65536:
                    raise ValueError("PLAT03 VM produced an oversized transcript")
                if process.returncode != 0:
                    raise ValueError("PLAT03 VM did not exit naturally through firmware with status zero")
                if read_regular(image)[1]["sha256"] != binding.image_sha256:
                    raise ValueError("PLAT03 kernel changed during its actual VM")
                case = TraceCase(capture, output.decode("ascii"), process.returncode)
                validate_case(case, baseline=baseline)
                return case, {"argv": command, "packets": remote.packets, "stderr": diagnostic.decode("utf-8")}
            finally:
                _cleanup_process(process)


def git(*arguments):
    return subprocess.check_output(("git", "--no-pager", *arguments), cwd=ROOT, timeout=30)


def run_cases(image, *, baseline):
    binding = bind_image(image, baseline=baseline)
    qemu = shutil.which("qemu-system-riscv64")
    if qemu is None:
        raise ValueError("PLAT03 requires the configured real QEMU")
    cases, records = [], []
    for scenario in range(1, 19):
        print(f"MICROS_PLAT03_CASE_BEGIN id={scenario} baseline={int(baseline)}", flush=True)
        case, record = run_case(image, binding, scenario, qemu, baseline=baseline)
        cases.append(case)
        records.append(record)
        print(f"MICROS_PLAT03_CASE_COMPLETE id={scenario} exit={case.exit_code}", flush=True)
    return binding, tuple(cases), tuple(records)


@lru_cache(maxsize=1)
def baseline_members():
    reference = DESIGN_PINS["runtime_reference"]
    if git("rev-parse", reference + "^{tree}").decode().strip() != DESIGN_PINS["runtime_tree"]:
        raise ValueError("PLAT03 runtime reference does not have its fixed tree")
    result = []
    for row in git("ls-tree", "-r", "-z", reference).split(b"\0"):
        if not row:
            continue
        header, name = row.split(b"\t", 1)
        mode, kind, identity = header.decode().split()
        if kind != "blob" or mode not in ("100644", "100755"):
            raise ValueError("PLAT03 baseline contains an unadmitted Git member kind")
        result.append((name.decode(), int(mode, 8) & 0o777, identity))
    return tuple(result)


def validate_baseline(source, *, observed=True):
    if type(observed) is not bool:
        raise ValueError("PLAT03 baseline has no exact observation stage")
    source = Path(source)
    if source.is_symlink() or not source.is_dir() or not source.resolve().is_relative_to(ROOT / "build"):
        raise ValueError("PLAT03 baseline is outside its owned ordinary build workspace")
    members = baseline_members()
    expected = {name for name, _, _ in members} | (set(GUEST_ADDITIONS) if observed else set())
    directories = {str(parent) for name in expected for parent in Path(name).parents if str(parent) != "."}
    actual = set()
    for path in source.rglob("*"):
        relative = str(path.relative_to(source))
        if path.is_symlink():
            raise ValueError("PLAT03 baseline namespace contains a symbolic link")
        if path.is_dir():
            if relative not in directories:
                raise ValueError("PLAT03 baseline namespace contains an extra directory")
        else:
            read_regular(owned_artifact(source, relative))
            actual.add(relative)
    if actual != expected:
        raise ValueError("PLAT03 baseline contains missing/extra source or substitute runtime owners")
    for name, mode, identity in members:
        data, current = read_regular(owned_artifact(source, name))
        if observed:
            if name == "CMakeLists.txt":
                data = project_trace_cmake(data.decode(), production=False).encode()
            elif name == "CMakePresets.json":
                data = project_trace_presets(data.decode()).encode()
            elif name in SOURCE_OVERLAYS:
                data = project_observations(name, data.decode()).encode()
        actual_blob = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if current["mode"] != mode or actual_blob != identity:
            raise ValueError("PLAT03 baseline does not inverse to its exact original Git blob/mode: " + name)
    if observed:
        for name in GUEST_ADDITIONS:
            if read_regular(owned_artifact(source, name)) != read_regular(owned_artifact(ROOT, name)):
                raise ValueError("PLAT03 baseline guest fixture differs from its immutable current input")


def prepare_baseline(workspace):
    workspace = Path(workspace)
    if workspace.is_symlink() or not workspace.is_dir() or not workspace.resolve().is_relative_to(ROOT / "build"):
        raise ValueError("PLAT03 baseline workspace is not an owned ordinary directory")
    source = owned_artifact(workspace, "source")
    source.mkdir(mode=0o700)
    archive = git("-c", "tar.umask=0022", "archive", "--format=tar", DESIGN_PINS["runtime_reference"])
    members = {name: mode for name, mode, _ in baseline_members()}
    seen = set()
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:") as entries:
        for member in entries:
            path = owned_artifact(source, member.name.rstrip("/"))
            if member.isdir():
                path.mkdir(parents=True, exist_ok=True)
                continue
            if (
                not member.isfile() or member.name not in members or member.name in seen
                or member.mode & 0o777 != members[member.name]
            ):
                raise ValueError("PLAT03 Git archive has an unadmitted path, mode or member kind")
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open("xb") as stream:
                stream.write(entries.extractfile(member).read())
            path.chmod(members[member.name])
            seen.add(member.name)
    validate_baseline(source, observed=False)
    for name in SOURCE_OVERLAYS:
        path = source / name
        path.write_text(observe_source(name, path.read_text()), encoding="utf-8")
    cmake, presets = source / "CMakeLists.txt", source / "CMakePresets.json"
    cmake.write_text(extend_cmake(cmake.read_text(), production=False), encoding="utf-8")
    presets.write_text(extend_presets(presets.read_text()), encoding="utf-8")
    for name in GUEST_ADDITIONS:
        data, identity = read_regular(owned_artifact(ROOT, name))
        target = owned_artifact(source, name)
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("xb") as stream:
            stream.write(data)
        target.chmod(identity["mode"])
    validate_baseline(source)
    return source


def source_manifest():
    from tests.host.diagnostic_history_evidence import closed_tool_inputs
    from tools.stack_bound_fatal_firmware import resolve_default_firmware
    from tools.validation_context import capture_candidate

    candidate = capture_candidate(ROOT)
    if not candidate.clean or git("ls-files", "--others", "--exclude-standard", "-z"):
        raise ValueError("PLAT03 requires the complete clean staged candidate with no untracked inputs")
    names = tuple(sorted(name.decode() for name in git("ls-files", "-z").split(b"\0") if name))
    if len(names) != len(set(names)):
        raise ValueError("PLAT03 source membership is ambiguous")
    immutable = tuple(name for name in names if name not in SOURCE_ROLES["candidate_paths"])
    subjects = tuple(name for name in SOURCE_ROLES["candidate_paths"] if name in names)
    if not set(SOURCE_ROLES["immutable_additions"]) <= set(immutable):
        raise ValueError("PLAT03 source manifest omitted an immutable fixture/tool owner")
    for name in SOURCE_ROLES["candidate_paths"]:
        if name not in names and ((ROOT / name).exists() or (ROOT / name).is_symlink()):
            raise ValueError("PLAT03 candidate has an untracked or ignored runtime replacement")
    cmake = read_regular(ROOT / "CMakeLists.txt")[0].decode()
    original = project_trace_cmake(cmake)
    if extend_cmake(original) != cmake:
        raise ValueError("PLAT03 current recipe omits a declared source, fixture or frame-chain option")
    presets = read_regular(ROOT / "CMakePresets.json")[0].decode()
    if extend_presets(project_trace_presets(presets)) != presets:
        raise ValueError("PLAT03 current presets omit the declared isolated workflow")
    tools = closed_tool_inputs(ROOT)
    return {
        "candidate": asdict(candidate),
        "immutable": {
            "files": snapshot_files(ROOT, immutable), "tools": tools,
            "firmware": resolve_default_firmware(shutil.which("qemu-system-riscv64")),
        },
        "subjects": snapshot_files(ROOT, subjects),
    }


def require_source_scope(*, baseline):
    from tests.host.evidence_authority_types import digest

    if type(baseline) is not bool:
        raise ValueError("PLAT03 source scope requires an exact execution stage")
    base = tuple(sorted(name.decode() for name in git(
        "ls-tree", "-r", "--name-only", "-z", DESIGN_PINS["design_commit"],
    ).split(b"\0") if name))
    if digest(base) != SOURCE_ROLES["base_membership_sha256"]:
        raise ValueError("PLAT03 design membership differs from its independent pre-edit declaration")
    present = {name.decode() for name in git("ls-files", "-z").split(b"\0") if name}
    allowed = set(base) | set(SOURCE_ROLES["candidate_paths"]) | set(SOURCE_ROLES["immutable_additions"])
    optional = set((*SOURCE_PROJECTION["new_kernel_members"], *SOURCE_PROJECTION["new_arch_members"])) if baseline else set()
    if present - allowed or (allowed - present) - optional:
        raise ValueError("PLAT03 source membership escaped its exact task roles")
    for name, mode, identity in baseline_members():
        if not name.startswith(("arch/", "kernel/", "include/", "lib/", "servers/", "user/")) or name in SOURCE_ROLES["candidate_paths"]:
            continue
        data, current = read_regular(owned_artifact(ROOT, name))
        if name in SOURCE_OVERLAYS:
            data = project_observations(name, data.decode()).encode()
        actual = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if (current["mode"], actual) != (mode, identity):
            raise ValueError("PLAT03 changed production outside its six candidates: " + name)


def build_target(source, workspace):
    if (source / "CMakeUserPresets.json").exists() or (source / "CMakeUserPresets.json").is_symlink():
        raise ValueError("PLAT03 does not admit caller-owned preset overrides")
    directory = workspace / "target"
    commands = (
        ("cmake", "--fresh", "--preset", RECIPE_ADMISSION["target_configure"], "-S", str(source), "-B", str(directory),
         "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"),
        ("cmake", "--build", str(directory), "--clean-first", "--target", "micros"),
    )
    records = []
    for command in commands:
        print("MICROS_PLAT03_EXECUTE " + repr(command), flush=True)
        result = subprocess.run(
            command, cwd=source, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        records.append({"argv": command, "cwd": str(source), "exit_code": result.returncode, "output": result.stdout})
        if result.returncode:
            print(result.stdout, flush=True)
            raise ValueError("PLAT03 configure/compile/link failure is an execution error, never baseline Red")
    return directory, tuple(records)


def case_record(case):
    value = asdict(case)
    for state in ("armed", "started", "final"):
        if value["capture"][state] is not None:
            value["capture"][state] = {
                name: item.hex() if type(item) is bytes else item
                for name, item in value["capture"][state].items()
            }
    for name in ("history", "stack"):
        if value["capture"][name] is not None:
            value["capture"][name] = value["capture"][name].hex()
    return value


def execute(baseline):
    from tests.host.diagnostic_history_evidence import build_artifacts
    from tools.python_host_runtime import require_current_runtime

    require_current_runtime()
    if type(baseline) is not bool:
        raise ValueError("PLAT03 source scope requires an exact execution stage")
    if baseline:
        require_source_scope(baseline=True)
    before = source_manifest()
    if not baseline and set(before["subjects"]) != set(SOURCE_ROLES["candidate_paths"]):
        raise ValueError("PLAT03 current execution requires all six trace production subjects")
    root = owned_artifact(ROOT, "build/panic-trace")
    root.mkdir(parents=True, exist_ok=True)
    workspace = owned_artifact(root, uuid.uuid4().hex)
    workspace.mkdir(mode=0o700)
    source = prepare_baseline(workspace) if baseline else ROOT
    directory, commands = build_target(source, workspace)
    if baseline:
        validate_baseline(source)
    artifacts = snapshot_files(directory, build_artifacts(directory, configure=RECIPE_ADMISSION["target_configure"]))
    binding, cases, records = run_cases(directory / "kernel/micros.elf", baseline=baseline)
    results, output = run_oracles(cases, baseline=baseline)
    print(output, flush=True)
    if baseline:
        validate_baseline(source)
        require_source_scope(baseline=True)
    if source_manifest() != before or snapshot_files(directory, tuple(artifacts)) != artifacts:
        raise ValueError("PLAT03 source, tools, firmware or actual artifacts changed during execution")
    record = {
        "version": 1, "baseline": baseline, "input_manifest": before,
        "source_reference": DESIGN_PINS["runtime_reference"] if baseline else before["candidate"]["source"]["head"],
        "source_directory": str(source), "build_directory": str(directory),
        "commands": commands, "artifacts": artifacts, "binding": asdict(binding),
        "cases": tuple(case_record(case) for case in cases), "debugger": records,
        "oracles": results, "oracle_output": output,
    }
    publish_json(workspace / "result.json", record)
    report = owned_artifact(root, "baseline.json" if baseline else "current.json")
    publish_json(report, record)
    if read_regular(report)[0] != encoded(record) or read_regular(workspace / "result.json")[0] != encoded(record):
        raise ValueError("PLAT03 published evidence does not match the complete actual execution")
    print(
        "MICROS_PLAT03_BASELINE_RED cases=18 positive-controls=1 expected-oracle-failures=4"
        if baseline else "MICROS_PLAT03_TARGET_PASS cases=18 oracles=4",
        flush=True,
    )
    return 1 if baseline else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--baseline", action="store_true", help="Observe only the fixed pre-correction runtime")
    arguments = parser.parse_args(argv)
    try:
        return execute(arguments.baseline)
    except Exception:
        traceback.print_exc()
        print("MICROS_PLAT03_EXECUTION_ERROR", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
