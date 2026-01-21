#!/usr/bin/env python3
"""
Parse Unity test framework output from QEMU serial logs.

This script searches for Unity test summary lines and determines if tests passed.
Expected format: "X Tests Y Failures Z Ignored"

Exit codes:
  0 - All tests passed (0 failures)
  1 - Test failures detected or summary not found
  2 - Invalid arguments
"""

import sys
import re
from pathlib import Path


def parse_unity_output(output_file: Path) -> tuple[int, int, int]:
    """
    Parse Unity test output and extract test summary.

    Args:
        output_file: Path to QEMU output log file

    Returns:
        Tuple of (tests, failures, ignored) counts

    Raises:
        ValueError: If Unity summary not found in output
    """
    if not output_file.exists():
        raise FileNotFoundError(f"Output file not found: {output_file}")

    content = output_file.read_text()

    # Look for Unity test summary: "X Tests Y Failures Z Ignored"
    # This appears at the end of test execution
    pattern = r"(\d+)\s+Tests\s+(\d+)\s+Failures\s+(\d+)\s+Ignored"
    matches = re.findall(pattern, content)

    if not matches:
        raise ValueError("Unity test summary not found in output")

    # Take the last match (final summary)
    tests, failures, ignored = matches[-1]
    return int(tests), int(failures), int(ignored)


def main():
    if len(sys.argv) != 2:
        print("Usage: parse_unity_output.py <qemu_output.log>", file=sys.stderr)
        sys.exit(2)

    output_file = Path(sys.argv[1])

    try:
        tests, failures, ignored = parse_unity_output(output_file)

        print(f"Test Results:")
        print(f"  Total Tests: {tests}")
        print(f"  Failures: {failures}")
        print(f"  Ignored: {ignored}")
        print()

        if failures == 0:
            print("✅ All tests passed!")
            sys.exit(0)
        else:
            print(f"❌ {failures} test(s) failed")
            sys.exit(1)

    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(2)
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        print("\nThis usually means:", file=sys.stderr)
        print("  - Tests didn't complete (QEMU timeout/crash)", file=sys.stderr)
        print("  - Unity framework wasn't properly initialized", file=sys.stderr)
        print("  - Output was truncated", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
