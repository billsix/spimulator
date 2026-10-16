#!/usr/bin/env python3
"""Parse a clang -Wconversion/-Wsign-conversion log into a catalog for the
integer-conversion-hardening task. Reads the log path as argv[1], writes TSV +
summaries into the sibling data/ directory. Paths are repo-root-relative so the
output is portable. Run via discover.sh (which regenerates the build + log)."""
import re, sys, pathlib, collections

log = pathlib.Path(sys.argv[1]).read_text(errors="replace")
out = pathlib.Path(__file__).resolve().parent / "data"
out.mkdir(exist_ok=True)

# Match:  <path>:L:C: warning: <msg> 'FROM'( (aka 'X'))? to 'TO'( (aka 'Y'))? [-Wflag]
line_re = re.compile(
    r"^.*?/(src|include)/([^:]+):(\d+):(\d+): warning: (.+?) "
    r"'([^']+)'(?: \(aka '([^']+)'\))? to '([^']+)'(?: \(aka '([^']+)'\))? \[-W(sign-conversion|conversion)\]",
)
rows = []
for ln in log.splitlines():
    m = line_re.match(ln)
    if not m:
        continue
    d, fn, line, col, msg, frm, frmaka, to, toaka, flag = m.groups()
    rows.append({
        "file": f"{d}/{fn}", "line": int(line), "col": int(col),
        "flag": flag,
        "from": frmaka or frm, "to": toaka or to,
        "from_named": frm, "to_named": to,
    })

# full worklist TSV
with (out / "conversions.tsv").open("w") as f:
    f.write("file\tline\tcol\tflag\tfrom\tto\tfrom_named\tto_named\n")
    for r in sorted(rows, key=lambda r: (r["file"], r["line"], r["col"])):
        f.write("\t".join(str(r[k]) for k in
                ("file","line","col","flag","from","to","from_named","to_named")) + "\n")

# summary: per file x flag
by_file = collections.Counter((r["file"], r["flag"]) for r in rows)
# summary: from->to type-pair (the "shape" of the work)
by_pair = collections.Counter((r["from"], r["to"], r["flag"]) for r in rows)

with (out / "summary.txt").open("w") as f:
    f.write(f"Total flagged integer conversions in src/+include/: {len(rows)}\n")
    f.write(f"  (clang {sys.argv[2] if len(sys.argv)>2 else '?'}, -Wconversion -Wsign-conversion, spimulator target)\n\n")
    f.write("== By file x flag ==\n")
    for (fname, flag), n in sorted(by_file.items(), key=lambda x: (-x[1], x[0])):
        f.write(f"  {n:4d}  {fname}  [{flag}]\n")
    f.write("\n== By resolved type-pair (from -> to) -- the SHAPE of the work ==\n")
    for (frm, to, flag), n in sorted(by_pair.items(), key=lambda x: (-x[1], x[0])):
        f.write(f"  {n:4d}  {frm:>12s} -> {to:<12s}  [{flag}]\n")

print(f"rows={len(rows)} files={len({r['file'] for r in rows})} "
      f"pairs={len(by_pair)} -> {out}")
