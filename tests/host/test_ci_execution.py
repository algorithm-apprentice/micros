"""Live CI output is visible before exit and cannot hide failed commands."""

import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


class CiExecutionTest(unittest.TestCase):
    def test_output_is_streamed_and_retained_before_waiting_for_the_exit(self):
        from tools.ci_execution import stream_command

        output = io.BytesIO()
        with tempfile.TemporaryDirectory(prefix="micros-ci-stream-") as temporary:
            path = Path(temporary) / "command.log"

            class Process:
                stdout = io.BytesIO(b"live first record\nlive second record\n")

                def wait(self):
                    self_test.assertEqual(self.stdout.getvalue(), output.getvalue())
                    self_test.assertEqual(output.getvalue(), path.read_bytes())
                    return 7

            self_test = self
            with patch("tools.ci_execution.subprocess.Popen", return_value=Process()):
                result = stream_command(("cmake", "--workflow", "--preset", "test-unit"), Path(temporary), path, output=output)
            self.assertEqual(7, result["exit_code"])
            self.assertGreaterEqual(result["duration_ns"], 0)
            self.assertEqual(output.getvalue(), path.read_bytes())


if __name__ == "__main__":
    unittest.main()
