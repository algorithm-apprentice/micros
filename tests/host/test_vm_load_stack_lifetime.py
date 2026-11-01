"""Exact ADR-0087 lifetime separation without changing the pinned load work."""

import hashlib
from pathlib import Path
import unittest
import tempfile

from tests.host.c_source_records import function_records, tokens


ROOT = Path(__file__).resolve().parents[2]
PREPARE_NAME = "micros_vm_load_prepare"
WORKER_NAME = "vm_load_prepare_transaction"
PREPARE_SIGNATURE = "526406c00e19f6f504182da11528894ec2178545ae293e0b43f09bd61af7a639"
PREPARE_BODY = "e6ba7e680e08ac67e61835fb9c8022f418b031be2a59d440cacfefce44695036"
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
FINAL = tokens("return vm_core_validate_state(state, false);")


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
        or "vm_core_validate_state" in work
    ):
        raise AssertionError("VM temporary ownership or validation lifetime is not exact")
    positions = tuple(index for index in range(len(body) - len(WORKER_CALL) + 1)
                      if body[index:index + len(WORKER_CALL)] == WORKER_CALL)
    if len(positions) != 1 or tokens(source).count(WORKER_NAME) != 2:
        raise AssertionError("VM helper has an omitted, duplicated or escaping invocation")
    position, = positions
    restored = (
        work[:len(DECLARATIONS)] + body[len(SCALAR):position]
        + work[len(DECLARATIONS):-len(endings)] + body[position + len(WORKER_CALL):]
    )
    if token_digest(restored) != PREPARE_BODY:
        raise AssertionError("VM lifetime split changed pinned statement, argument or failure ordering")
    return facade, worker


class VmLoadStackLifetimeTest(unittest.TestCase):
    def test_actual_fixed_frames_release_the_worker_before_final_validation(self):
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
                self.assertLess(proof.worker_call, proof.final_validation)
                self.assertGreater(len(proof.return_sites), 0)

    def test_preparation_temporaries_end_before_final_complete_validation(self):
        facade, worker = verify_prepare_lifetime((ROOT / "servers/vm/vm_load.c").read_text())
        self.assertNotIn("struct micros_vm_service_load_transaction candidate", facade.body)
        self.assertNotIn("vm_core_validate_state", worker.body)

    def test_reordered_validation_changed_error_or_escaping_workspace_fails(self):
        source = (ROOT / "servers/vm/vm_load.c").read_text()
        verify_prepare_lifetime(source)
        for old, new in (
            ("return vm_core_validate_state(state, false);", "return MICROS_VM_CORE_OK;"),
            ("state->phase = MICROS_VM_LOADER_INPUT_COPIED;", "state->phase = MICROS_VM_LOADER_IDLE;"),
            ("candidate.semantic_pid = semantic_pid;", "candidate.semantic_pid = 0;"),
            ("state->transaction = candidate;", "state->transaction = candidate; leak(&candidate);"),
            ("return vm_core_prepare_failure(state, MICROS_VM_CORE_ERROR_NO_SPACE);",
             "return MICROS_VM_CORE_ERROR_NO_SPACE;"),
        ):
            self.assertIn(old, source)
            with self.subTest(mutation=new), self.assertRaises(AssertionError):
                verify_prepare_lifetime(source.replace(old, new, 1))


if __name__ == "__main__":
    unittest.main()
