import unittest

from tests.host.c_source_records import function_records
from tests.host.native_observations import (
    bind_sites, direct_calls, function_contract, render_body, validate_function,
)


SOURCE = """
static void clear(struct ledger *ledger)
{
    for (size_t index = 0; index < 2; ++index) {
        ledger->owners[index] = 0;
    }
}
"""
REGIONS = (("for ( size_t index = 0 ; index < 2 ; ++ index ) {", 0),)
SITES = (
    ("probe", "wrap", "KERNEL_MEMORY", "INDEX_PROBE", 1, "value", "index", REGIONS, 0, 1),
    ("store", "insert", "KERNEL_MEMORY", "CLEAR_UNIT", 1, "after",
     "ledger -> owners [ index ] = 0 ;", REGIONS, 0, 1),
)


class NativeObservationTest(unittest.TestCase):
    def test_pointer_casts_do_not_authorize_indirect_calls(self):
        self.assertEqual((), direct_calls("return (uint64_t *)(uintptr_t)physical_address;"))
        for body in ("return (*callback)(value);", "return (callback)(value);", "return ((void (*)(int))pointer)(value);"):
            with self.assertRaises(ValueError):
                direct_calls(body)

    def test_current_contract_allows_comments_and_unmeasured_scalar_statements(self):
        original = function_records(SOURCE)["clear"]
        expected = function_contract(original)
        changed = function_records(SOURCE.replace(
            "    for", "    /* No measured work. */\n    int status = 0;\n    (void)status;\n    for",
        ))["clear"]
        validate_function(changed, expected)
        sites = bind_sites(changed.body, SITES)
        observed = render_body(changed.body, sites)
        self.assertIn("ledger->owners[MICROS_MEM05_OBSERVE_VALUE", observed)
        self.assertIn(" = 0;MICROS_TEST_WORK_HIT", observed)

    def test_added_loop_alias_callee_and_index_work_are_not_unobserved(self):
        original = function_records(SOURCE)["clear"]
        expected = function_contract(original)
        for changed in (
            SOURCE.replace("    for", "    for (size_t hidden = 0; hidden < 8; ++hidden) {}\n    for"),
            SOURCE.replace("    for", "    uint64_t *alias = ledger->owners;\n    for"),
            SOURCE.replace("    for", "    extra_work();\n    for"),
            SOURCE.replace("ledger->owners[index] = 0;", "ledger->owners[index] = ledger->owners[index + 1];"),
            SOURCE.replace("    for", "    #if 0\n    for").replace("    }\n", "    }\n    #endif\n"),
            SOURCE.replace("static void", "static int"),
        ):
            with self.subTest(source=changed), self.assertRaises(ValueError):
                validate_function(function_records(changed)["clear"], expected)

    def test_primitive_locations_cannot_be_missing_ambiguous_or_moved(self):
        body = function_records(SOURCE)["clear"].body
        bind_sites(body, SITES)
        for changed in (
            body.replace("owners[index]", "owners[index + 1]"),
            body.replace("ledger->owners[index] = 0;", "ledger->owners[index] = 0; ledger->owners[index] = 0;"),
            body.replace("ledger->owners[index] = 0;", "") + "\nledger->owners[index] = 0;\n",
        ):
            with self.assertRaises(ValueError):
                bind_sites(changed, SITES)
        for site in (
            (*SITES[0][:4], True, *SITES[0][5:]),
            (*SITES[0][:3], "UNKNOWN", *SITES[0][4:]),
            (*SITES[0][:5], "unknown", *SITES[0][6:]),
        ):
            with self.assertRaises(ValueError):
                bind_sites(body, (site,))

    def test_consumed_record_cannot_move_ahead_of_its_measured_bounds_guard(self):
        body = (
            "if (index >= ledger->count) { return 0; }\n"
            "unsigned value = ledger->owners[index];\n"
            "return value;\n"
        )
        sites = (
            ("guard", "insert", "KERNEL_MEMORY", "INDEX_PROBE", 1, "before",
             "if ( index >= ledger -> count )", (), 0, 1),
            ("owner", "wrap", "KERNEL_MEMORY", "RECORD_VISIT", 1, "value",
             "ledger -> owners [ index ]", (), 0, 1),
        )
        bind_sites(body, sites)
        statement = "unsigned value = ledger->owners[index];\n"
        moved = statement + body.replace(statement, "")
        with self.assertRaises(ValueError):
            bind_sites(moved, sites)


if __name__ == "__main__":
    unittest.main()
