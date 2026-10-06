# ADR-0023: Canonical User Status Summary Bits

- Status: Accepted
- Date: 2026-10-06
- Supersedes: ADR-0022 user-status preservation formula

## Context

ADR-0022 defines a canonical U-mode return status by preserving fields outside
`USER_CONTROL_MASK`, clearing privileged control fields, and setting `SPIE`.

The first hardware execution exposed one incorrect assumption in that formula.
On RV64, `sstatus.SD` at bit 63 is a read-only summary of extension state. When
`FS`, `VS`, and `XS` are all off, hardware reports `SD = 0` even if the live
supervisor value used during preparation previously had `SD = 1`. Software
cannot preserve that derived bit independently from the fields it summarizes.

Keeping the original formula makes the stored context disagree with the exact
status observed on the first user trap, despite correct hardware behavior.

## Decision

ADR-0022's status formula is replaced only as follows:

```text
USER_CONTROL_MASK =
    SIE | SPIE | SPP | UBE | VS | FS | XS | SUM | MXR
USER_DERIVED_MASK = SD
USER_REQUIRED_SET = SPIE
USER_REQUIRED_CLEAR =
    SIE | SPP | UBE | VS | FS | XS | SUM | MXR | SD
UXL_MASK = 3 << 32
UXL_REQUIRED = 2 << 32
```

The prepared value is:

```text
(live_sstatus & ~(USER_CONTROL_MASK | USER_DERIVED_MASK))
    | USER_REQUIRED_SET
```

`UXL` remains preserved and must equal RV64. WPRI and other non-derived fields
outside the masks remain equal to the live CSR value. `SD` is canonicalized to
zero because every summarized extension field is off.

Ordinary U-mode return validation requires the exact same canonical value.
The first U-origin trap must capture that exact value, including `SD = 0`.

The test-only supervisor escape also clears `SD` in its selected frame; after
`sret`, hardware remains authoritative for the summary value derived from the
restored supervisor extension state.

## Tests

The U-mode component test first writes the implemented QEMU RV64 `FS` WARL
field to Dirty and requires readback `FS = Dirty` plus `SD = 1`. It then
prepares the context and requires:

- prepared context `FS = Off`;
- prepared context `SD = 0`;
- the first U-origin trap's saved status equals the complete canonical
  prepared value;
- return validation rejects a selected frame with SD set;
- all existing FS/VS/XS, UXL, UBE, SPP, SIE, SPIE, SUM, MXR, and preserved-bit
  mutation cases remain enforced.

## Consequences

- Saved user status matches the value hardware can actually restore and later
  report.
- Extension state remains off and no false dirty summary is persisted.
- ADR-0022's context layout, stack ownership, entry sequence, and all other
  decisions remain unchanged.

## Specification basis

- [RISC-V Privileged Architecture, Supervisor-Level ISA](https://docs.riscv.org/reference/isa/v20260120/priv/supervisor.html)
- [ADR-0015: Supervisor Trap Entry](0015-supervisor-trap-entry.md)
- [ADR-0022: User Execution Contexts and U-Mode Entry](0022-user-execution-contexts-and-u-mode-entry.md)
