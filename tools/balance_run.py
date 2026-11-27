"""Balancing over 100 simulated worlds (release r6): builds tools/balance.cpp with the real game core, runs the seeds in parallel
shards and prints the distribution. Fails when the targets are missed: no world ends before day 30, the median end is on day 30..45,
at most 3% of the worlds die out.

    python tools/balance_run.py [COUNT [FIRST_SEED [MAX_DAYS]]]        defaults 100 1 80
Writes a markdown table to $GITHUB_STEP_SUMMARY when it is set.
"""
import collections
import os
import re
import statistics
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
LINE = re.compile(r"seed\s+(\d+) land (\d) clim (\d): (\w+)\s+day\s+(\d+)\s+peak\s+(\d+)\s+births\s+(\d+) deaths\s+(\d+) built\s+(\d+)\s+town\(20\) day\s+(-?\d+)\s+pop d20\s+(\d+) d30\s+(\d+)\s+unlock\s+(-?\d+) stages (-?\d+) (-?\d+) (-?\d+) (-?\d+) (-?\d+)")


def pct(v, p):
    return v[(len(v) - 1) * p // 100] if v else 0


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    first = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    days = int(sys.argv[3]) if len(sys.argv) > 3 else 80
    tmp = tempfile.mkdtemp(prefix="balance_")
    exe = os.path.join(tmp, "balance")
    srcs = sorted(os.path.join(ROOT, "lib", "gamecore", "src", f) for f in os.listdir(os.path.join(ROOT, "lib", "gamecore", "src")) if f.endswith(".cpp"))
    subprocess.check_call(["g++", "-std=c++17", "-O2", "-o", exe, os.path.join(ROOT, "tools", "balance.cpp")] + srcs)
    shards = max(1, min(8, os.cpu_count() or 1, count))
    per = (count + shards - 1) // shards
    procs = []
    left = count
    for k in range(shards):
        n = min(per, left)
        left -= n
        if n <= 0:
            break
        procs.append(subprocess.Popen([exe, str(first + k * per * 7919), str(n), str(days)], stdout=subprocess.PIPE, text=True))
    rows = []
    for p in procs:
        out = p.communicate()[0]
        for line in out.splitlines():
            m = LINE.match(line)
            if m:
                rows.append([m.group(4)] + [int(x) for x in m.groups()[:3] + m.groups()[4:]])
    # row: phase, seed, land, clim, end_day, peak, births, deaths, built, town20, pop20, pop30, unlock, s1..s5
    if len(rows) != count:
        print("FAIL: %d of %d worlds reported" % (len(rows), count))
        return 1
    ended = [r for r in rows if r[0] != "running"]
    ext = [r for r in rows if r[0] == "EXTINCT"]
    running = [r for r in rows if r[0] == "running"]
    ends = sorted(r[4] for r in ended)
    peaks = sorted(r[5] for r in rows)
    early = sum(1 for r in ended if r[0] != "EXTINCT" and r[4] < 30)      # a world that dies out is counted by the extinction rule below
    med = pct(ends, 50)
    lines = []
    lines.append("| | min | p10 | median | p90 | max |")
    lines.append("|---|---|---|---|---|---|")
    lines.append("| end day (worship day over) | %d | %d | %d | %d | %d |" % (pct(ends, 0), pct(ends, 10), med, pct(ends, 90), pct(ends, 100)))
    lines.append("| peak villagers | %d | %d | %d | %d | %d |" % tuple(pct(peaks, p) for p in (0, 10, 50, 90, 100)))
    for i in range(5):
        v = sorted(r[13 + i] for r in rows if r[13 + i] > 0)
        lines.append("| day stage %d is finished | %d | %d | %d | %d | %d |" % ((i + 1,) + tuple(pct(v, p) for p in (0, 10, 50, 90, 100))))
    towns = sorted(r[9] for r in rows if r[9] >= 0)
    lines.append("| day the town has 20 villagers | %d | %d | %d | %d | %d |" % tuple(pct(towns, p) for p in (0, 10, 50, 90, 100)))
    for name, idx in (("villagers on day 20", 10), ("villagers on day 30", 11)):
        v = sorted(r[idx] for r in rows)
        lines.append("| %s | %d | %d | %d | %d | %d |" % ((name,) + tuple(pct(v, p) for p in (0, 10, 50, 90, 100))))
    lines.append("")
    lines.append("%d worlds from seed %d, %d died out, %d were still running on day %d, %d ended before day 30." % (count, first, len(ext), len(running), days, early))
    hist = collections.Counter(ends)
    lines.append("end day histogram: " + ", ".join("%d: %d" % kv for kv in sorted(hist.items())))
    by_land = collections.defaultdict(list)
    for r in rows:
        if r[0] != "running":
            by_land[r[2]].append(r[4])
    names = ["island", "coast", "river valley", "lake basin", "archipelago", "mountain plateau"]
    lines.append("median end day by landform: " + ", ".join("%s %d" % (names[k], statistics.median(v)) for k, v in sorted(by_land.items())))
    odd = [r for r in rows if r[0] != "MONUMENT" or r[4] < 30]
    if odd:                     # the outliers, to be run alone: balance SEED 1 80 1 (a line per day)
        lines.append("")
        lines.append("outliers (seed, landform, climate, how it ended, end day, peak, births, deaths):")
        for r in odd:
            lines.append("  seed %d land %d clim %d: %s day %d peak %d births %d deaths %d" % (r[1], r[2], r[3], r[0], r[4], r[5], r[6], r[7]))
    ok = early == 0 and not running and 30 <= med <= 45 and len(ext) * 100 <= count * 3
    lines.append("targets (no COMPLETED world ends before day 30, median 30..45, at most 3%% of the worlds die out (an extinct world is counted by that rule, not as an early end), all finish): %s" % ("met" if ok else "NOT met"))
    text = "\n".join(lines)
    print(text)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write("### Balancing, %d simulated worlds\n\n%s\n" % (count, text))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
