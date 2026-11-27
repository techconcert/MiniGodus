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
    # The release body is only the first section (build.yml: awk '/^What (is|was) new in MiniGodus r/{n++} n==1').
    first, count = [], 0
    for line in notes.splitlines():
        if re.match(r"What (is|was) new in MiniGodus r", line):
            count += 1
        if count == 1:
            first.append(line)
    heads = [l for l in first if re.match(r"What (is|was) new in", l)]
    if len(heads) != 1 or heads[0] != "What is new in MiniGodus r%s:" % version:
        print("FAIL: the first release-notes section must be exactly r%s, got %r" % (version, heads))
        return 1
    wf = open(os.path.join(ROOT, ".github", "workflows", "build.yml"), encoding="utf-8").read()
    if "cat RELEASE-NOTES.md" in wf or "n==1' RELEASE-NOTES.md" not in wf:
        print("FAIL: build.yml must put only the first RELEASE-NOTES.md section in the release body")
        return 1
    print("test_version: VERSION %s matches RELEASE-NOTES.md" % version)
    return 0


if __name__ == "__main__":
    sys.exit(main())
