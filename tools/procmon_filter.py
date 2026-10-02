#!/usr/bin/env python3
"""Filter a Process Monitor CSV export to one process and summarize file access.

Usage: procmon_filter.py <procmon.csv> <process.exe> <out_dir> [--root <install_root>]
Writes:
  <out_dir>/<process>.events.csv   all rows of that process (same columns)
  <out_dir>/<process>.files.tsv    per path: first time, first op, #opens, #reads, bytes read, result
Prints a summary; with --root, paths under the install root are shown relative to it.
"""
import collections
import csv
import os
import re
import sys


def main(argv):
    src, pname, out = argv[1], argv[2].lower(), argv[3]
    root = None
    if "--root" in argv:
        root = os.path.normcase(os.path.abspath(argv[argv.index("--root") + 1]))
    os.makedirs(out, exist_ok=True)
    per = collections.OrderedDict()
    ops = collections.Counter()
    n = 0
    with open(src, encoding="utf-8-sig", newline="") as f, \
            open(os.path.join(out, f"{pname}.events.csv"), "w", encoding="utf-8", newline="") as g:
        r = csv.reader(f)
        hdr = next(r)
        w = csv.writer(g)
        w.writerow(hdr)
        ix = {h: i for i, h in enumerate(hdr)}
        for row in r:
            if row[ix["Process Name"]].lower() != pname:
                continue
            n += 1
            w.writerow(row)
            op, path, res = row[ix["Operation"]], row[ix["Path"]], row[ix["Result"]]
            ops[op] += 1
            if not op.startswith(("CreateFile", "ReadFile", "QueryOpen", "Load Image", "WriteFile")):
                continue
            e = per.setdefault(path, {"t": row[ix["Time of Day"]], "op": op, "opens": 0, "reads": 0,
                                      "bytes": 0, "writes": 0, "result": res})
            if op == "CreateFile":
                e["opens"] += 1
                if res == "SUCCESS":
                    e["result"] = res
            elif op == "ReadFile":
                e["reads"] += 1
                m = re.search(r"Length: ([\d.,]+)", row[ix["Detail"]])
                if m:
                    e["bytes"] += int(re.sub(r"[.,]", "", m.group(1)))
            elif op == "WriteFile":
                e["writes"] += 1
    with open(os.path.join(out, f"{pname}.files.tsv"), "w", encoding="utf-8", newline="\n") as h:
        h.write("# first_time\tfirst_op\topens\treads\tbytes_read\twrites\tresult\tpath\n")
        for p, e in per.items():
            h.write(f"{e['t']}\t{e['op']}\t{e['opens']}\t{e['reads']}\t{e['bytes']}\t{e['writes']}\t{e['result']}\t{p}\n")
    print(f"{n} events for {pname}; top operations: {ops.most_common(12)}")
    if root:
        inside = [(p, e) for p, e in per.items() if os.path.normcase(p).startswith(root)]
        ok = [(p, e) for p, e in inside if e["result"] == "SUCCESS" or e["reads"]]
        print(f"{len(inside)} distinct paths under root, {len(ok)} successfully opened/read")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
