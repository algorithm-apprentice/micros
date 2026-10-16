import subprocess


def parse_nm_symbol_address(output, symbol):
    addresses = []

    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 3 or fields[-1] != symbol:
            continue
        try:
            addresses.append(int(fields[0], 16))
        except ValueError:
            continue
    if len(addresses) != 1:
        raise ValueError(
            f"expected exactly one defined symbol {symbol!r}, "
            f"found {len(addresses)}"
        )
    return addresses[0]


def resolve_symbol_address(nm, kernel, symbol):
    completed = subprocess.run(
        [nm, "-n", "--defined-only", str(kernel)],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or "no diagnostic"
        raise RuntimeError(f"symbol inspection failed: {detail}")
    return parse_nm_symbol_address(completed.stdout, symbol)


def build_qemu_command(*, qemu, kernel, memory="128M"):
    return [
        qemu,
        "-machine",
        "virt,aia=none",
        "-cpu",
        "rv64",
        "-smp",
        "1",
        "-m",
        memory,
        "-display",
        "none",
        "-monitor",
        "none",
        "-serial",
        "stdio",
        "-nic",
        "none",
        "-bios",
        "default",
        "-kernel",
        kernel,
        "-no-reboot",
    ]
