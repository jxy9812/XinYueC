"""Print the last N lines of a file.

Usage: x_tail.py <file> [lines]
"""
import sys


def main():
    path = sys.argv[1]
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    with open(path, "rb") as fh:
        data = fh.read()
    text = data.decode("utf-8", errors="replace")
    lines = text.splitlines()
    for line in lines[-n:]:
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
