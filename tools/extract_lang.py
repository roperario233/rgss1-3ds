#!/usr/bin/env python3
"""Extract the per-language .rxdata files from lang.dat (a zip inside the zip)."""
import io
import os
import zipfile

OUTER = "To.the.Moon.v2023.07.03.Incl.ALL.DLC.zip"
OUT = os.path.join("work", "raw_lang")

outer = zipfile.ZipFile(OUTER)
entry = [i.filename for i in outer.infolist() if i.filename.endswith("lang.dat")][0]
inner = zipfile.ZipFile(io.BytesIO(outer.read(entry)))

os.makedirs(OUT, exist_ok=True)
n = 0
for name in inner.namelist():
    if name.endswith(".rxdata"):
        parts = name.split("/")
        code = parts[1] if len(parts) > 1 else "xx"
        dest = os.path.join(OUT, "%s_%s" % (code, os.path.basename(name)))
        with open(dest, "wb") as fh:
            fh.write(inner.read(name))
        n += 1
print("[+] extracted %d language .rxdata files to %s" % (n, OUT))
