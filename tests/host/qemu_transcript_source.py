"""Finite ADR-0108 reporting additions; historical expectations stay active."""


OPTION = '''    parser.add_argument(
        "--report-transcript",
        action="store_true",
        help="Print the accepted single-run decoded transcript as TAP diagnostics",
    )
'''
VALIDATION = '''    if arguments.report_transcript and len(arguments.memory) != 1:
        parser.error("--report-transcript requires exactly one memory size")
'''
PRINTER = '''

def print_transcript(output):
    for line in output.splitlines(keepends=True):
        print(f"# {line}", end="")
'''
EXPECT_END = '''        help="Observed outcome required for a successful test",
    )
'''
NEXT_OPTIONS = "\n\ndef _add_platform_report_arguments(\n"
FAULT_VALIDATION = '''    _validate_fault_arguments(
        parser=parser,
        arguments=arguments,
    )
'''
OUTCOME_IMPORT = '''from tools.qemu_smoke_outcome import (
    matches_expected_result,
    classify_smoke,
    print_tap_result,
)
'''
SINGLE_RESULT = '''    _, result, outcome, command = successful_runs[0]
    return print_tap_result(
        outcome,
        result,
        command,
        expected_outcome=expected_outcome,
        accepted=True,
    )
'''
REPORTED_RESULT = '''    _, result, outcome, command = successful_runs[0]
    status = print_tap_result(
        outcome, result, command,
        expected_outcome=expected_outcome, accepted=True,
    )
    if arguments.report_transcript:
        print_transcript(result.output)
    return status
'''
SOURCE_CHANGES = {
    "tools/qemu_smoke_arguments.py": (
        (EXPECT_END + OPTION + NEXT_OPTIONS, EXPECT_END + NEXT_OPTIONS),
        (
            FAULT_VALIDATION + VALIDATION + "    return arguments\n",
            FAULT_VALIDATION + "    return arguments\n",
        ),
    ),
    "tools/qemu_smoke_outcome.py": (
        ("    }.get(outcome, 3)\n" + PRINTER, "    }.get(outcome, 3)\n"),
    ),
    "tools/qemu_smoke_cli.py": (
        (
            OUTCOME_IMPORT.replace("    print_tap_result,\n", "    print_tap_result,\n    print_transcript,\n", 1),
            OUTCOME_IMPORT,
        ),
        (REPORTED_RESULT, SINGLE_RESULT),
    ),
}


def project_transcript_source(relative, source):
    changes = SOURCE_CHANGES.get(relative)
    markers = ("--report-transcript", "report_transcript", "print_transcript")
    if changes is None or not any(marker in source for marker in markers):
        return source
    if relative != "tools/qemu_smoke_arguments.py" and not source.endswith(changes[-1][0]):
        raise AssertionError("accepted-transcript reporting moved from its exact final owner")
    if any(source.count(current) != 1 for current, _ in changes):
        raise AssertionError("accepted-transcript source extension is incomplete, repeated or changed")
    for current, previous in changes:
        source = source.replace(current, previous, 1)
    if any(marker in source for marker in markers):
        raise AssertionError("accepted-transcript source has an undeclared extra use")
    return source


def project_transcript_options(observed):
    if any(
        type(row) not in (tuple, list) or len(row) != 2
        or type(row[0]) is not int or type(row[1]) is not str
        for row in observed
    ):
        raise AssertionError("accepted-transcript options require actual parser observations")
    names = tuple(name for _, name in observed)
    if "--report-transcript" not in names:
        return observed
    if names.count("--report-transcript") != 1 or names.count("--expect") != 1:
        raise AssertionError("accepted-transcript option is duplicated or lacks its anchor")
    index = names.index("--report-transcript")
    if (
        index != names.index("--expect") + 1
        or names[index + 1:index + 2] != ("--require-fdt-events",)
        or observed[index - 1][0] != observed[index][0]
        or observed[index + 1][0] != observed[index][0]
    ):
        raise AssertionError("accepted-transcript option moved or borrowed another parser")
    return (*observed[:index], *observed[index + 1:])


def project_transcript_cmake(source):
    if "--report-transcript" not in source:
        return source
    if source.count("--report-transcript") != 2:
        raise ValueError("accepted-transcript recipes require exactly two option tokens")
    runner = '''        COMMAND
            "${MICROS_PYTHON}"
            ${MICROS_PYTHON_ARGS}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu_smoke.py"
'''
    for target in ("test-qemu-user-address-space", "test-qemu-address-space-handoff"):
        previous = "    add_custom_target(\n        " + target + "\n" + runner
        current = previous + "            --report-transcript\n"
        if source.count(current) != 1:
            raise ValueError("accepted-transcript recipe has a changed or misplaced runner option")
        source = source.replace(current, previous, 1)
    return source
