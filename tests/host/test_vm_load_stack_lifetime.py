"""Current ADR-0087/0135 VM preparation storage and normal-return boundary."""

import hashlib
from pathlib import Path
import unittest
import tempfile

from tests.host.c_source_records import function_records, tokens
from tests.host.native_observations import direct_calls


ROOT = Path(__file__).resolve().parents[2]
PREPARE_NAME = "micros_vm_load_prepare"
WORKER_NAME = "vm_load_prepare_transaction"
PREPARE_SIGNATURE = "526406c00e19f6f504182da11528894ec2178545ae293e0b43f09bd61af7a639"
DECLARATIONS = tokens("""
    struct micros_vm_service_load_transaction candidate;
    struct micros_vm_elf_plan elf_plan;
    struct micros_application_stack_plan stack_plan;
    uint64_t table_frames[MICROS_VM_PAGE_TABLE_FRAME_COUNT] = {0};
    uint64_t data_frames[MICROS_APPLICATION_RESIDENT_PAGE_MAX] = {0};
    enum micros_vm_core_error error;
    size_t page_count;
""")
SCALAR = tokens("enum micros_vm_core_error error;")
WORKER_CALL = tokens("""
    error = vm_load_prepare_transaction(
        state, process_reservation_token, semantic_pid,
        executable_bytes, executable_size, request_bytes, request_size, stack_values
    );
    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
""")
INITIAL = tokens("""
    error = vm_core_validate_selected_state(state);
    if (error != MICROS_VM_CORE_OK) {
        return error;
    }
""")
FINAL = tokens("return MICROS_VM_CORE_OK;")
WORKER_CALLEES = frozenset((
    "prepare_binary_plans", "vm_core_select_lowest_frames", "vm_core_prepare_failure",
    "vm_core_zero_bytes", "vm_core_copy_bytes", "publish_load_input",
))


def token_digest(values):
    return hashlib.sha256(" ".join(values).encode()).hexdigest()


def verify_prepare_lifetime(source):
    records = function_records(source)
    if PREPARE_NAME not in records or WORKER_NAME not in records:
        raise AssertionError("missing phase-bounded preparation helper")
    facade, worker = records[PREPARE_NAME], records[WORKER_NAME]
    if token_digest(tokens(facade.signature)) != PREPARE_SIGNATURE:
        raise AssertionError("VM preparation changed its public signature")
    expected_signature = ("static", *(
        WORKER_NAME if token == PREPARE_NAME else token for token in tokens(facade.signature)
    ))
    if tokens(worker.signature) != expected_signature or worker.guards != facade.guards or facade.guards:
        raise AssertionError("VM preparation helper changed its exact private argument/scope contract")
    body, work = tokens(facade.body), tokens(worker.body)
    endings = tokens("return MICROS_VM_CORE_OK;")
    if (
        body[:len(SCALAR)] != SCALAR or body[-len(FINAL):] != FINAL
        or work[:len(DECLARATIONS)] != DECLARATIONS or work[-len(endings):] != endings
        or {"vm_core_validate_state", "vm_core_validate_selected_state", "static", "_Thread_local"} & set(work)
        or frozenset(direct_calls(worker.body)) != WORKER_CALLEES
        or any(name in body for name in ("candidate", "elf_plan", "stack_plan", "table_frames", "data_frames"))
    ):
        raise AssertionError("VM temporary ownership or validation lifetime is not exact")
    positions = tuple(index for index in range(len(body) - len(WORKER_CALL) + 1)
                      if body[index:index + len(WORKER_CALL)] == WORKER_CALL)
    if len(positions) != 1 or tokens(source).count(WORKER_NAME) != 2:
        raise AssertionError("VM helper has an omitted, duplicated or escaping invocation")
    initial = tuple(index for index in range(len(body) - len(INITIAL) + 1)
                    if body[index:index + len(INITIAL)] == INITIAL)
    if len(initial) != 1 or initial[0] >= positions[0] or body.count("vm_core_validate_selected_state") != 1:
        raise AssertionError("VM selected preflight does not precede the sole preparation call")
    if "vm_core_validate_state" in body:
        raise AssertionError("VM ordinary preparation regained a complete audit")
    return facade, worker


class VmLoadStackLifetimeTest(unittest.TestCase):
    def test_actual_fixed_frames_release_the_worker_before_the_caller_continues(self):
        from tests.host.test_stack_bound_evidence import compile_fixture
        from tools.stack_bound_objects import load_object
        from tools.stack_bound_vm_lifetime import require_vm_prepare_lifetime

        source = (ROOT / "servers/vm/vm_load.c").read_text()
        with tempfile.TemporaryDirectory(prefix="micros-vm-prepare-frames-") as temporary:
            for observed in (False, True):
                directory = Path(temporary) / ("observed" if observed else "metadata")
                directory.mkdir()
                _, output = compile_fixture(directory, source, observed=observed)
                proof = require_vm_prepare_lifetime(ROOT, load_object(output))
                self.assertGreater(proof.worker_frame, proof.facade_frame)
                self.assertLess(proof.initial_validation, proof.worker_call)
                self.assertLess(proof.worker_call, proof.worker_continuation)
                self.assertGreater(len(proof.return_sites), 0)

    def test_preparation_temporaries_have_only_the_private_worker_lifetime(self):
        facade, worker = verify_prepare_lifetime((ROOT / "servers/vm/vm_load.c").read_text())
        self.assertNotIn("struct micros_vm_service_load_transaction candidate", facade.body)
        self.assertNotIn("vm_core_validate_state", worker.body)

    def test_reordered_validation_changed_error_or_escaping_workspace_fails(self):
        source = (ROOT / "servers/vm/vm_load.c").read_text()
        verify_prepare_lifetime(source)
        for old, new in (
            ("error = vm_core_validate_selected_state(state);", "error = MICROS_VM_CORE_OK;"),
            ("struct micros_vm_service_load_transaction candidate;", "static struct micros_vm_service_load_transaction candidate;"),
            ("state->transaction = candidate;", "state->transaction = candidate; leak(&candidate);"),
            ("return MICROS_VM_CORE_OK;\n}\n\nstatic bool load_record_identity_is_available",
             "return vm_core_validate_state(state, false);\n}\n\nstatic bool load_record_identity_is_available"),
        ):
            self.assertIn(old, source)
            with self.subTest(mutation=new), self.assertRaises(AssertionError):
                verify_prepare_lifetime(source.replace(old, new, 1))
        preflight = "    error = vm_core_validate_selected_state(state);"
        moved = source.replace(preflight, "    error = MICROS_VM_CORE_OK;", 1)
        moved = moved.replace("    return MICROS_VM_CORE_OK;\n}\n\nstatic bool load_record_identity_is_available",
                              preflight + "\n    return MICROS_VM_CORE_OK;\n}\n\nstatic bool load_record_identity_is_available", 1)
        with self.assertRaises(AssertionError):
            verify_prepare_lifetime(moved)


if __name__ == "__main__":
    unittest.main()
