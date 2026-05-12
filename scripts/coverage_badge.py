#!/usr/bin/env python3
"""Convert a gcovr --json-summary file into a shields.io endpoint badge."""
import json
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: coverage_badge.py SUMMARY.json BADGE.json", file=sys.stderr)
        return 2
    with open(sys.argv[1], encoding="utf-8") as f:
        percent = float(json.load(f)["line_percent"])
    color = "brightgreen" if percent >= 90 else "green" if percent >= 80 else "yellow" if percent >= 70 else "red"
    badge = {"schemaVersion": 1, "label": "coverage", "message": f"{percent:.0f}%", "color": color}
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        json.dump(badge, f)
    print(badge)
    return 0


if __name__ == "__main__":
    sys.exit(main())
