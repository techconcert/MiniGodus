"""A release must not keep the previous VERSION: the number in VERSION has to match the release at the top of RELEASE-NOTES.md
("What is new in MiniGodus rN:")."""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def main():
    version = open(os.path.join(ROOT, "VERSION"), encoding="utf-8").read().strip()
    notes = open(os.path.join(ROOT, "RELEASE-NOTES.md"), encoding="utf-8").read()
    m = re.match(r"\s*What is new in MiniGodus r(\d+)\b", notes)
    if not re.fullmatch(r"\d+", version):
        print("FAIL: VERSION is not a number: %r" % version)
        return 1
    if not m:
        print("FAIL: RELEASE-NOTES.md must start with 'What is new in MiniGodus rN:'")
        return 1
    if int(m.group(1)) != int(version):
        print("FAIL: VERSION is %s but RELEASE-NOTES.md starts with r%s: bump VERSION in the release PR" % (version, m.group(1)))
        return 1
    print("test_version: VERSION %s matches RELEASE-NOTES.md" % version)
    return 0


if __name__ == "__main__":
    sys.exit(main())
