from dataclasses import replace
import unittest

from tests.host.test_generate_bootstrap_fixture import bootstrap_image
from tests.host.test_generate_vm_handoff_fixture import vm_image
from tools import check_user_elf
from tools import generate_vm_scratch_preemption_fixture


def with_symbols(image, names, start_offset=52):
    symbols = tuple(
        check_user_elf.ElfSymbol(
            name=name,
            binding=check_user_elf.SYMBOL_BIND_GLOBAL,
            symbol_type=0,
            section_index=1,
            value=check_user_elf.USER_BASE
            + start_offset
            + index * 4,
            size=4,
        )
        for index, name in enumerate(names)
    )
    return replace(image, symbols=image.symbols + symbols)


class VmScratchPreemptionFixtureGeneratorTest(unittest.TestCase):
    def test_renders_topology_and_all_trap_addresses(self):
        vm = with_symbols(
            vm_image(),
            generate_vm_scratch_preemption_fixture.VM_SYMBOLS,
            52,
        )
        pm = with_symbols(
            bootstrap_image(),
            generate_vm_scratch_preemption_fixture.PEER_SYMBOLS,
            256,
        )

        output = generate_vm_scratch_preemption_fixture.render_fixture(
            bootstrap_image(),
            vm,
            pm,
            bootstrap_image(),
        )

        self.assertIn(
            "micros_vm_scratch_preemption_test_images",
            output,
        )
        self.assertIn("MICROS_BOOTSTRAP_ROLE_PM", output)
        self.assertIn(
            '                .service_name = "vm-scratch-driver",',
            output,
        )
        for names, start_offset in (
            (generate_vm_scratch_preemption_fixture.VM_SYMBOLS, 52),
            (generate_vm_scratch_preemption_fixture.PEER_SYMBOLS, 256),
        ):
            for index, name in enumerate(names):
                value = (
                    check_user_elf.USER_BASE
                    + start_offset
                    + index * 4
                )
                self.assertIn(
                    f"const uint64_t {name}_address =\n"
                    f"    UINT64_C(0x{value:016x});",
                    output,
                )

    def test_rejects_missing_vm_hook_symbol(self):
        with self.assertRaises(check_user_elf.ElfFormatError):
            generate_vm_scratch_preemption_fixture.render_fixture(
                bootstrap_image(),
                vm_image(),
                with_symbols(
                    bootstrap_image(),
                    generate_vm_scratch_preemption_fixture.PEER_SYMBOLS,
                ),
                bootstrap_image(),
            )

    def test_rejects_missing_peer_report_symbol(self):
        with self.assertRaises(check_user_elf.ElfFormatError):
            generate_vm_scratch_preemption_fixture.render_fixture(
                bootstrap_image(),
                with_symbols(
                    vm_image(),
                    generate_vm_scratch_preemption_fixture.VM_SYMBOLS,
                ),
                bootstrap_image(),
                bootstrap_image(),
            )


if __name__ == "__main__":
    unittest.main()
