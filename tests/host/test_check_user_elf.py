import unittest

from tests.host.user_elf_test_fixture import (
    align,
    make_string_table,
    build_elf,
    validation_errors,
)
from tests.host.user_elf_parse_cases import UserElfParseCases
from tests.host.user_elf_structure_cases import UserElfStructureCases
from tests.host.user_elf_policy_cases import UserElfPolicyCases
from tests.host.user_elf_cli_cases import UserElfCliCases


class UserElfCheckerTest(
    UserElfParseCases,
    UserElfStructureCases,
    UserElfPolicyCases,
    UserElfCliCases,
    unittest.TestCase,
):
    pass


if __name__ == "__main__":
    unittest.main()
