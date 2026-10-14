"""ELF structural validation owner assigned by ADR-0063."""

from __future__ import annotations



from tools.user_elf_format import (
    ELF_HEADER,
    ELF_VERSION_CURRENT,
    ELF_TYPE_EXECUTABLE,
    ELF_MACHINE_RISCV,
    ELF_FLAG_RISCV_RVC,
    PROGRAM_LOAD,
    PROGRAM_DYNAMIC,
    PROGRAM_INTERP,
    PROGRAM_TLS,
    PROGRAM_EXECUTE,
    PROGRAM_WRITE,
    PROGRAM_READ,
    SECTION_PROGBITS,
    SECTION_RELA,
    SECTION_NOBITS,
    SECTION_REL,
    SECTION_WRITE,
    SECTION_ALLOC,
    SECTION_EXECUTE,
    PAGE_SIZE,
    USER_BASE,
    USER_END,
    ElfImage,
    _section_by_name,
)


FORBIDDEN_PROGRAM_TYPES = {
    PROGRAM_INTERP: "PT_INTERP",
    PROGRAM_DYNAMIC: "PT_DYNAMIC",
    PROGRAM_TLS: "PT_TLS",
}


FORBIDDEN_SECTION_NAMES = frozenset(
    (
        ".dynamic",
        ".dynsym",
        ".dynstr",
        ".hash",
        ".gnu.hash",
        ".plt",
        ".plt.got",
        ".got",
        ".got.plt",
        ".preinit_array",
        ".init_array",
        ".fini_array",
        ".ctors",
        ".dtors",
        ".tdata",
        ".tbss",
        ".sdata",
        ".sbss",
        ".stack",
        ".eh_frame",
        ".eh_frame_hdr",
        ".gcc_except_table",
        ".exception_ranges",
    )
)


FORBIDDEN_SECTION_PREFIXES = (
    ".rel",
    ".rela",
    ".gnu.version",
    ".llvm_prf",
    ".llvm_cov",
    ".asan",
    ".ubsan",
    ".sancov",
    ".prof",
)


def _validate_header(image: ElfImage):
    header = image.header
    errors = []
    if header.elf_type != ELF_TYPE_EXECUTABLE:
        errors.append("ELF type must be ET_EXEC")
    if header.machine != ELF_MACHINE_RISCV:
        errors.append("ELF machine must be EM_RISCV")
    if header.version != ELF_VERSION_CURRENT:
        errors.append("ELF version is invalid")
    if header.header_size != ELF_HEADER.size:
        errors.append("ELF header size is invalid")
    if header.entry != USER_BASE:
        errors.append("ELF entry must equal MICROS_USER_VIRTUAL_BASE")
    if header.flags & ~ELF_FLAG_RISCV_RVC:
        errors.append("ELF flags claim an unsupported RISC-V ABI or extension")
    return errors


def _validate_programs(image: ElfImage):
    errors = []
    for program in image.programs:
        forbidden = FORBIDDEN_PROGRAM_TYPES.get(program.program_type)
        if forbidden is not None:
            errors.append(f"forbidden program header {forbidden}")
    loads = sorted(
        (
            program
            for program in image.programs
            if program.program_type == PROGRAM_LOAD
        ),
        key=lambda program: program.virtual_address,
    )
    if len(loads) != 3:
        errors.append("ELF must contain exactly three PT_LOAD segments")
        return errors, loads
    expected_flags = (
        PROGRAM_READ | PROGRAM_EXECUTE,
        PROGRAM_READ,
        PROGRAM_READ | PROGRAM_WRITE,
    )
    previous_end = None
    for index, (program, flags) in enumerate(zip(loads, expected_flags)):
        if program.flags != flags:
            errors.append(
                f"PT_LOAD {index} has the wrong permission class"
            )
        if program.alignment != PAGE_SIZE:
            errors.append(f"PT_LOAD {index} alignment must be 4096")
        if (
            program.virtual_address % PAGE_SIZE != 0
            or program.offset % PAGE_SIZE != 0
            or program.offset % PAGE_SIZE
                != program.virtual_address % PAGE_SIZE
        ):
            errors.append(f"PT_LOAD {index} has invalid page congruence")
        if program.physical_address != program.virtual_address:
            errors.append(f"PT_LOAD {index} p_paddr must equal p_vaddr")
        if program.file_size > program.memory_size:
            errors.append(f"PT_LOAD {index} has p_filesz greater than p_memsz")
        if (
            program.offset > len(image.data)
            or program.file_size > len(image.data) - program.offset
        ):
            errors.append(f"PT_LOAD {index} file range is truncated")
        if program.memory_size == 0:
            errors.append(f"PT_LOAD {index} is empty")
        end = program.virtual_address + program.memory_size
        if (
            end < program.virtual_address
            or program.virtual_address < USER_BASE
            or end > USER_END
        ):
            errors.append(f"PT_LOAD {index} lies outside the user window")
        if previous_end is not None and program.virtual_address < previous_end:
            errors.append("PT_LOAD segments overlap")
        previous_end = end
        if (
            program.flags & PROGRAM_WRITE
            and program.flags & PROGRAM_EXECUTE
        ):
            errors.append(f"PT_LOAD {index} is writable and executable")
    return errors, loads


def _validate_sections(image: ElfImage, loads):
    errors = []
    names = set()
    for section in image.sections:
        if not section.name:
            continue
        if section.name in names:
            errors.append(f"duplicate section name {section.name}")
        names.add(section.name)
        if (
            section.name in FORBIDDEN_SECTION_NAMES
            or section.name.startswith(FORBIDDEN_SECTION_PREFIXES)
        ):
            errors.append(f"forbidden section {section.name}")
        if section.section_type in (SECTION_REL, SECTION_RELA):
            errors.append(f"relocation section {section.name} is forbidden")
        if section.flags & SECTION_ALLOC:
            section_end = section.address + section.size
            compatible = []
            for program in loads:
                program_end = program.virtual_address + program.memory_size
                if (
                    section.address >= program.virtual_address
                    and section_end <= program_end
                ):
                    compatible.append(program)
            if len(compatible) != 1:
                errors.append(
                    f"allocatable section {section.name} is not closed "
                    "inside one PT_LOAD"
                )
                continue
            program = compatible[0]
            expected_write = bool(section.flags & SECTION_WRITE)
            expected_execute = bool(section.flags & SECTION_EXECUTE)
            if (
                expected_write != bool(program.flags & PROGRAM_WRITE)
                or expected_execute != bool(program.flags & PROGRAM_EXECUTE)
            ):
                errors.append(
                    f"allocatable section {section.name} has incompatible "
                    "segment permissions"
                )
            if section.section_type != SECTION_NOBITS:
                file_end = program.virtual_address + program.file_size
                if section_end > file_end:
                    errors.append(
                        f"file-backed section {section.name} exceeds p_filesz"
                    )
                expected_offset = (
                    program.offset
                    + section.address
                    - program.virtual_address
                )
                if section.offset != expected_offset:
                    errors.append(
                        f"section {section.name} file offset is not congruent"
                    )
    required = {
        ".text": (SECTION_PROGBITS, SECTION_ALLOC | SECTION_EXECUTE),
        ".rodata": (SECTION_PROGBITS, SECTION_ALLOC),
        ".data": (SECTION_PROGBITS, SECTION_ALLOC | SECTION_WRITE),
        ".bss": (SECTION_NOBITS, SECTION_ALLOC | SECTION_WRITE),
    }
    for name, (section_type, flags) in required.items():
        section = _section_by_name(image, name)
        if section is None:
            errors.append(f"missing required section {name}")
        elif (
            section.section_type != section_type
            or section.flags
                & (SECTION_ALLOC | SECTION_WRITE | SECTION_EXECUTE)
                != flags
        ):
            errors.append(f"required section {name} has the wrong shape")
    for section in image.sections:
        if (
            section.flags & SECTION_ALLOC
            and section.name not in required
        ):
            errors.append(
                f"unexpected allocatable section {section.name}"
            )
    return errors
