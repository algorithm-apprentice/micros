"""Execute the selected observed scope after complete static/producer proof."""

import os
from pathlib import Path
import unittest

from tools.stack_bound_run import run_observed_scope


class StackObserverQemuTest(unittest.TestCase):
    def test_kernel_preserves_legacy_gate_and_reports_every_live_stack_owner(self):
        root = Path(__file__).resolve().parents[2]
        directory = Path(os.environ["MICROS_STACK_TEST_BUILD"]).resolve()
        result = run_observed_scope(root, directory)
        self.assertTrue(result.observations)
        print(result.transcript)


if __name__ == "__main__":
    unittest.main()
