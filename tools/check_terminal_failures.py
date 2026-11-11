#!/usr/bin/env -S python3.15 -E -S -B

"""Observe the twelve closed PLAT02 cases on the actual RISC-V kernel."""

import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import traceback
import uuid

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tests.host.terminal_failure_evidence import (
    GuestState, TerminalCapture, TerminalCase, require_case, run_oracles, validate_capture, validate_case,
)
from tests.host.terminal_failure_source import RECIPE_ADMISSION
from tools.qemu_smoke_command import build_qemu_command
from tools.stack_artifact_files import read_regular
from tools.stack_bound_manifest import encoded, owned_artifact, publish_json, snapshot_files
from tools.stack_bound_objects import instructions
from tools.user_elf_parse import load_elf


ROOT = Path(__file__).resolve().parent.parent
MAX_PACKET = 16384
SELECTOR = "micros_terminal_test_scenario"
REGISTERS = (2, 10, 11, 16, 17, 32)
SUPPORTED = b"qSupported:swbreak+;hwbreak+"


@dataclass(frozen=True)
class ImageBinding:
    image_sha256: str
    objects: tuple
    stops: tuple
    stack: tuple
    control_present: bool


def encode_packet(payload):
    if (
        type(payload) is not bytes or len(payload) > MAX_PACKET
        or any(byte < 32 or byte > 126 or byte in b"$#}*" for byte in payload)
    ):
        raise ValueError("PLAT02 has no binary, escaped, repeated or oversized debugger packet")
    return b"$" + payload + b"#" + f"{sum(payload) & 255:02x}".encode()


def decode_packet(packet):
    if (
        type(packet) is not bytes or not 4 <= len(packet) <= MAX_PACKET + 4
        or packet[:1] != b"$" or packet[-3:-2] != b"#"
        or re.fullmatch(rb"[0-9a-f]{2}", packet[-2:]) is None
    ):
        raise ValueError("PLAT02 debugger packet framing is invalid")
    payload = packet[1:-3]
    if packet != encode_packet(payload):
        raise ValueError("PLAT02 debugger packet checksum is invalid")
    return payload


def require_stop(payload):
    if payload == b"S05":
        return
    if not payload.startswith(b"T05") or not payload.endswith(b";"):
        raise ValueError("PLAT02 debugger did not reach a breakpoint stop")
    seen = set()
    for field in payload[3:-1].split(b";"):
        key, separator, value = field.partition(b":")
        if not separator or key in seen:
            raise ValueError("PLAT02 debugger stop has duplicate or malformed metadata")
        seen.add(key)
        if key in (b"core", b"thread"):
            if re.fullmatch(rb"(?:p[0-9a-f]+\.)?[0-9a-f]+", value) is None:
                raise ValueError("PLAT02 debugger stop has an invalid core/thread identity")
        elif key not in (b"hwbreak", b"swbreak") or value:
            raise ValueError("PLAT02 debugger stop has unsupported metadata")


class Remote:
    def __init__(self, connection, binding, scenario, deadline):
        require_case(scenario)
        if type(binding) is not ImageBinding or binding.control_present is not True:
            raise ValueError("PLAT02 debugger requires its bound image objects")
        self.connection = connection
        self.binding = binding
        self.scenario = scenario
        self.deadline = deadline
        self.objects = {name: (address, size) for name, address, size in binding.objects}
        self.stops = {name: (address, width) for name, address, width in binding.stops}
        self.stopped_at = None
        self.selected = False
        self.buffer = bytearray()
        self.packets = []

    def remaining(self):
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("PLAT02 eight-second VM deadline expired")
        return remaining

    def _receive(self):
        self.connection.settimeout(self.remaining())
        data = self.connection.recv(4096)
        if not data:
            raise ValueError("PLAT02 debugger connection ended before the required observation")
        self.buffer.extend(data)
        if len(self.buffer) > MAX_PACKET + 4:
            raise ValueError("PLAT02 debugger response exceeded its packet bound")

    def _take(self, length):
        while len(self.buffer) < length:
            self._receive()
        result = bytes(self.buffer[:length])
        del self.buffer[:length]
        return result

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
            if not 1 <= size <= 4096:
                raise ValueError("PLAT02 debugger read exceeds its bounded chunk")
            if self.stopped_at == "kernel_main":
                if self.objects.get(SELECTOR) == (address, size):
                    return
            elif self.stopped_at in ("armed", "reset", "overrun"):
                if any(start <= address and address + size <= start + extent for start, extent in self.objects.values()):
                    return
                if self.scenario in (3, 6) and (address, size) == (0x10000001, 1):
                    return
        if SELECTOR in self.objects:
            address, size = self.objects[SELECTOR]
            expected = f"M{address:x},4:{self.scenario.to_bytes(4, 'little').hex()}".encode()
            if self.stopped_at == "kernel_main" and not self.selected and size == 4 and payload == expected:
                return
        raise ValueError("PLAT02 debugger request exceeds its fixed register/object/selector authority")

    def exchange(self, payload, *, reply=True):
        packet = encode_packet(payload)
        self._require_request(payload)
        self.connection.settimeout(self.remaining())
        self.connection.sendall(packet)
        if payload.startswith(b"M"):
            self.selected = True
        if self._take(1) != b"+":
            raise ValueError("PLAT02 debugger did not acknowledge the exact request")
        if not reply:
            self.packets.append((payload.decode(), None))
            return None
        if self._take(1) != b"$":
            raise ValueError("PLAT02 debugger did not frame a response")
        while b"#" not in self.buffer:
            self._receive()
        size = self.buffer.index(b"#") + 3
        frame = b"$" + self._take(size)
        response = decode_packet(frame)
        if self.buffer:
            raise ValueError("PLAT02 debugger supplied an unsolicited extra response")
        self.connection.settimeout(self.remaining())
        self.connection.sendall(b"+")
        self.packets.append((payload.decode(), response.decode("ascii")))
        return response

    def handshake(self):
        response = self.exchange(SUPPORTED)
        if not response.startswith(b"PacketSize=") or b"qXfer:features:read+" not in response.split(b";"):
            raise ValueError("PLAT02 debugger lacks the fixed QEMU protocol")
        size = response.split(b";", 1)[0].partition(b"=")[2]
        if not re.fullmatch(rb"[0-9a-f]+", size) or int(size, 16) < 4096:
            raise ValueError("PLAT02 debugger packet capacity is unsupported")
        require_stop(self.exchange(b"?"))

    def breakpoint(self, name, enabled):
        if type(enabled) is not bool or name not in self.stops:
            raise ValueError("PLAT02 breakpoint does not name a bound checkpoint")
        address, width = self.stops[name]
        request = f"{'Z' if enabled else 'z'}1,{address:x},{width}".encode()
        if self.exchange(request) != b"OK":
            raise ValueError("PLAT02 debugger rejected a checkpoint breakpoint")

    def register(self, number):
        if type(number) is not int or number not in REGISTERS:
            raise ValueError("PLAT02 debugger register is outside its integer witness")
        result = self.exchange(f"p{number:x}".encode())
        if re.fullmatch(rb"[0-9a-f]{16}", result) is None:
            raise ValueError("PLAT02 debugger returned an incomplete RV64 register")
        return int.from_bytes(bytes.fromhex(result.decode()), "little")

    def resume_to(self, expected):
        if type(expected) is not tuple or not expected or any(name not in self.stops for name in expected):
            raise ValueError("PLAT02 has no such checkpoint continuation")
        require_stop(self.exchange(b"c"))
        pc = self.register(32)
        names = tuple(name for name, (address, _) in self.stops.items() if address == pc)
        if len(names) != 1 or names[0] not in expected:
            raise ValueError(f"PLAT02 reached an unexpected debugger stop at {pc:#x}")
        self.stopped_at = names[0]
        return names[0]

    def select_scenario(self):
        if self.stopped_at != "kernel_main" or self.selected or self.objects.get(SELECTOR, (0, 0))[1] != 4:
            raise ValueError("PLAT02 may write its four-byte selector only once at kernel_main")
        address, _ = self.objects[SELECTOR]
        payload = f"M{address:x},4:{self.scenario.to_bytes(4, 'little').hex()}".encode()
        if self.exchange(payload) != b"OK":
            raise ValueError("PLAT02 debugger did not write the exact scenario selector")

    def _memory(self, address, size):
        data = self.exchange(f"m{address:x},{size:x}".encode())
        if re.fullmatch(rb"[0-9a-f]{" + str(2 * size).encode() + rb"}", data) is None:
            raise ValueError("PLAT02 debugger returned incomplete bounded memory")
        return bytes.fromhex(data.decode())

    def read_object(self, name):
        if name not in self.objects:
            raise ValueError("PLAT02 debugger object has no fixed ELF binding")
        address, size = self.objects[name]
        return b"".join(self._memory(address + offset, min(1024, size - offset)) for offset in range(0, size, 1024))

    def read_ier(self):
        if self.scenario not in (3, 6):
            raise ValueError("PLAT02 permits IER observation only in its handoff cases")
        return self._memory(0x10000001, 1)[0]


def _symbol(image, name, *, symbol_type=None):
    matches = tuple(symbol for symbol in image.symbols if (
        symbol.name == name and symbol.section_index != 0
        and (symbol_type is None or symbol.symbol_type == symbol_type)
    ))
    if len(matches) != 1:
        raise ValueError("PLAT02 ELF has no unique defined " + name)
    return matches[0]


def _data_symbol(image, name, size, alignment, section_type):
    symbol = _symbol(image, name, symbol_type=1)
    if (
        symbol.symbol_type != 1 or symbol.size != size or symbol.value % alignment
        or not 0 < symbol.section_index < len(image.sections)
    ):
        raise ValueError("PLAT02 data symbol has an invalid type, size or alignment: " + name)
    section = image.sections[symbol.section_index]
    if (
        section.section_type != section_type or section.flags != 3
        or not section.address <= symbol.value <= symbol.value + size <= section.address + section.size
    ):
        raise ValueError("PLAT02 data symbol leaves its ordinary writable section: " + name)
    owners = tuple(program for program in image.programs if (
        program.program_type == 1 and program.flags == 6
        and program.virtual_address == program.physical_address
        and program.virtual_address <= symbol.value
        and symbol.value + size <= program.virtual_address + program.memory_size
    ))
    if len(owners) != 1:
        raise ValueError("PLAT02 data symbol lacks one writable, non-executable load owner")
    return symbol


def _function_instructions(image, name):
    symbol = _symbol(image, name, symbol_type=2)
    if symbol.symbol_type != 2 or not symbol.size or not 0 < symbol.section_index < len(image.sections):
        raise ValueError("PLAT02 checkpoint lacks a complete ELF function")
    section = image.sections[symbol.section_index]
    if section.section_type != 1 or section.flags != 6 or not (
        section.address <= symbol.value < symbol.value + symbol.size <= section.address + section.size
    ):
        raise ValueError("PLAT02 checkpoint is outside its executable section")
    owners = tuple(program for program in image.programs if (
        program.program_type == 1 and program.flags == 5
        and program.virtual_address == program.physical_address
        and program.virtual_address <= symbol.value
        and symbol.value + symbol.size <= program.virtual_address + program.file_size
    ))
    if len(owners) != 1:
        raise ValueError("PLAT02 checkpoint lacks one executable load owner")
    offset = section.offset + symbol.value - section.address
    return tuple(instructions(image.data[offset:offset + symbol.size], symbol.value))


def bind_image(path):
    image = load_elf(path)
    if (image.header.elf_type, image.header.machine, image.header.version, image.header.flags) != (2, 243, 1, 1):
        raise ValueError("PLAT02 requires its ordinary RV64 little-endian kernel ELF")
    objects = []
    for name, size, alignment, section_type in (
        (SELECTOR, 4, 4, 8),
        ("micros_terminal_test_observation", 104, 8, 8),
        ("micros_terminal_test_work_snapshot", 3056, 8, 8),
        ("uart_console_state", 16, 8, 1),
        ("diagnostic_history", 12012, 4, 8),
        ("diagnostic_routing", 8, 4, 8),
    ):
        symbol = _data_symbol(image, name, size, alignment, section_type)
        objects.append((name, symbol.value, symbol.size))
    control = _data_symbol(image, "panic_control", 4, 4, 8)
    objects.append(("panic_control", control.value, control.size))
    stack = tuple(_symbol(image, name).value for name in ("__boot_stack_bottom", "__boot_stack_top"))
    if stack[1] - stack[0] != 16384 or any(value % 16 for value in stack):
        raise ValueError("PLAT02 fixture does not use the exact linker-owned boot stack")
    stack_sections = tuple(section for section in image.sections if section.name == ".stack")
    if len(stack_sections) != 1 or (
        stack_sections[0].section_type, stack_sections[0].flags, stack_sections[0].address, stack_sections[0].size
    ) != (8, 3, stack[0], 16384) or any(
        _symbol(image, name).section_index != stack_sections[0].index
        for name in ("__boot_stack_bottom", "__boot_stack_top")
    ):
        raise ValueError("PLAT02 boot stack symbols do not bind their exact ordinary stack section")
    for index, (name, address, size) in enumerate(objects):
        if address < stack[1] and stack[0] < address + size:
            raise ValueError("PLAT02 observation/control object overlaps its execution stack")
        for other, start, extent in objects[index + 1:]:
            if address < start + extent and start < address + size:
                raise ValueError("PLAT02 data objects overlap: " + name + "/" + other)
    for other in image.symbols:
        if other != control and other.symbol_type == 1 and other.size and other.section_index != 0:
            if control.value < other.value + other.size and other.value < control.value + control.size:
                raise ValueError("PLAT02 terminal ownership aliases another ELF object")
    stops = []
    for name, function in (("kernel_main", "kernel_main"), ("armed", "micros_terminal_test_armed")):
        address, width, _ = _function_instructions(image, function)[0]
        stops.append((name, address, width))
    for name, function, instruction in (
        ("reset", "sbi_system_reset", 0x73),
        ("overrun", "micros_terminal_test_overrun", 0x10500073),
        ("unexpected", "micros_terminal_test_unexpected_return", 0x10500073),
        ("error", "micros_terminal_test_error", 0x10500073),
    ):
        matches = tuple((address, width) for address, width, word in _function_instructions(image, function)
                        if width == 4 and word == instruction)
        if len(matches) != 1:
            raise ValueError("PLAT02 checkpoint lacks one actual required machine instruction: " + name)
        stops.append((name, *matches[0]))
    if len({address for _, address, _ in stops}) != len(stops):
        raise ValueError("PLAT02 checkpoint addresses are ambiguous")
    return ImageBinding(hashlib.sha256(image.data).hexdigest(), tuple(objects), tuple(stops), stack, True)


def capture_state(remote):
    return GuestState(
        remote.read_object("micros_terminal_test_observation"),
        remote.read_object("diagnostic_history"),
        remote.read_object("uart_console_state"),
        remote.read_object("diagnostic_routing"),
        remote.read_object("panic_control"),
        remote.read_object("micros_terminal_test_work_snapshot"),
        remote.read_ier() if remote.scenario in (3, 6) else None,
        remote.register(2),
    )


def _cleanup_process(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate(timeout=2)


def run_case(image, binding, scenario, qemu):
    require_case(scenario)
    if type(binding) is not ImageBinding or binding.control_present is not True:
        raise ValueError("PLAT02 requires the current image's panic control")
    if read_regular(image)[1]["sha256"] != binding.image_sha256:
        raise ValueError("PLAT02 kernel image changed before its actual VM")
    process = None
    with tempfile.TemporaryDirectory(prefix="pt2-", dir=ROOT / "build") as temporary:
        address = str(Path(temporary) / "gdb")
        command = build_qemu_command(qemu=qemu, kernel=str(image)) + ["-S", "-gdb", "unix:" + address + ",server=off"]
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
            listener.bind(address)
            listener.listen(1)
            deadline = time.monotonic() + 8
            try:
                process = subprocess.Popen(
                    command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                )
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("PLAT02 VM startup exhausted its controller deadline")
                listener.settimeout(remaining)
                connection, _ = listener.accept()
                with connection:
                    remote = Remote(connection, binding, scenario, deadline)
                    remote.handshake()
                    if process.poll() is not None:
                        raise ValueError("PLAT02 owned QEMU exited before its live debugger handshake")
                    for name in remote.stops:
                        remote.breakpoint(name, True)
                    remote.resume_to(("kernel_main",))
                    if remote.read_object(SELECTOR) != bytes(4):
                        raise ValueError("PLAT02 selector is not BSS-cleared at kernel_main")
                    remote.select_scenario()
                    remote.breakpoint("kernel_main", False)
                    remote.resume_to(("armed",))
                    armed = capture_state(remote)
                    remote.breakpoint("armed", False)
                    stopped = remote.resume_to(tuple(name for name in remote.stops if name not in ("kernel_main", "armed")))
                    final = capture_state(remote)
                    registers = tuple((number, remote.register(number)) for number in REGISTERS)
                    capture = TerminalCapture(
                        scenario, stopped, armed, final, registers, binding.stack, remote.stops[stopped][0],
                    )
                    validate_capture(capture)
                    remote.breakpoint("reset", False)
                    remote.exchange(b"c", reply=False)
                    output, diagnostic = process.communicate(timeout=remote.remaining())
                    remote.remaining()
                if len(output) > 65536 or len(diagnostic) > 65536:
                    raise ValueError("PLAT02 VM produced an oversized transcript")
                case = TerminalCase(capture, output.decode("ascii"), process.returncode, False)
                validate_case(case)
                if read_regular(image)[1]["sha256"] != binding.image_sha256:
                    raise ValueError("PLAT02 kernel image changed during its actual VM")
                return case, {"argv": command, "packets": remote.packets, "stderr": diagnostic.decode("utf-8")}
            finally:
                _cleanup_process(process)


def run_cases(image):
    binding = bind_image(image)
    qemu = shutil.which("qemu-system-riscv64")
    if qemu is None:
        raise ValueError("PLAT02 requires the configured real QEMU")
    cases, records = [], []
    for scenario in range(1, 13):
        print(f"MICROS_PLAT02_CASE_BEGIN id={scenario}", flush=True)
        case, record = run_case(image, binding, scenario, qemu)
        cases.append(case)
        records.append(record)
        print(f"MICROS_PLAT02_CASE_COMPLETE id={scenario} stop={case.capture.checkpoint} exit={case.exit_code}", flush=True)
    return binding, tuple(cases), tuple(records)










def source_manifest():
    from tests.host.diagnostic_history_evidence import current_target_manifest

    return current_target_manifest(ROOT)




def build_target(source, workspace):
    if (source / "CMakeUserPresets.json").exists() or (source / "CMakeUserPresets.json").is_symlink():
        raise ValueError("PLAT02 does not admit caller-owned preset overrides")
    directory = workspace / "target"
    commands = (
        ("cmake", "--fresh", "--preset", RECIPE_ADMISSION["target_configure"], "-S", str(source), "-B", str(directory),
         "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"),
        ("cmake", "--build", str(directory), "--clean-first", "--target", "micros"),
    )
    records = []
    for command in commands:
        print("MICROS_PLAT02_EXECUTE " + repr(command), flush=True)
        result = subprocess.run(
            command, cwd=source, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        record = {"argv": command, "cwd": str(source), "exit_code": result.returncode, "output": result.stdout}
        records.append(record)
        if result.returncode:
            print(result.stdout, flush=True)
            raise ValueError("PLAT02 configure/compile/link failure is an execution error")
    return directory, tuple(records)


def case_record(case):
    value = asdict(case)
    for state in ("armed", "final"):
        value["capture"][state] = {
            name: item.hex() if type(item) is bytes else item
            for name, item in value["capture"][state].items()
        }
    return value


def execute():
    from tests.host.diagnostic_history_evidence import build_artifacts
    from tools.python_host_runtime import require_current_runtime

    require_current_runtime()
    before = source_manifest()
    root = owned_artifact(ROOT, "build/terminal-failure")
    root.mkdir(parents=True, exist_ok=True)
    workspace = owned_artifact(root, uuid.uuid4().hex)
    workspace.mkdir(mode=0o700)
    directory, commands = build_target(ROOT, workspace)
    artifacts = snapshot_files(directory, build_artifacts(directory, configure=RECIPE_ADMISSION["target_configure"]))
    binding, cases, records = run_cases(directory / "kernel/micros.elf")
    results, output = run_oracles(cases)
    print(output, flush=True)
    if source_manifest() != before or snapshot_files(directory, tuple(artifacts)) != artifacts:
        raise ValueError("PLAT02 source, tools, firmware or actual artifacts changed during execution")
    record = {
        "version": 1, "input_manifest": before,
        "source_reference": before["candidate"]["source"]["head"],
        "source_directory": str(ROOT), "build_directory": str(directory),
        "commands": commands, "artifacts": artifacts, "binding": asdict(binding),
        "cases": tuple(case_record(case) for case in cases), "debugger": records,
        "oracles": results, "oracle_output": output,
    }
    publish_json(workspace / "result.json", record)
    report = owned_artifact(root, "current.json")
    publish_json(report, record)
    if read_regular(report)[0] != encoded(record) or read_regular(workspace / "result.json")[0] != encoded(record):
        raise ValueError("PLAT02 published evidence does not match the complete actual execution")
    print("MICROS_PLAT02_TARGET_PASS cases=12", flush=True)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.parse_args(argv)
    try:
        return execute()
    except Exception:
        traceback.print_exc()
        print("MICROS_PLAT02_EXECUTION_ERROR", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
