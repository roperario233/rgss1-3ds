#!/usr/bin/env python3
"""Find which fonts the game references (System.rxdata and the scripts)."""
import glob
import os

FONTS = [b"Open Sans", b"OpenSans", b"Nanum", b"WenQuanYi", b"Arial",
         b"Times New Roman", b"Verdana", b"Tahoma", b"Gulim", b"MS PGothic"]

print("=== Data/*.rxdata ===")
for f in sorted(glob.glob(os.path.join("work", "raw", "Data", "*.rxdata"))):
    data = open(f, "rb").read()
    found = [n.decode() for n in FONTS if data.find(n) >= 0]
    if found:
        print("%-26s %s" % (os.path.basename(f), found))

print("\n=== work/scripts/*.rb ===")
for f in sorted(glob.glob(os.path.join("work", "scripts", "*.rb"))):
    data = open(f, "rb").read()
    found = [n.decode() for n in FONTS if data.find(n) >= 0]
    if found:
        print("%-42s %s" % (os.path.basename(f), found))

print("\n=== Fonts/ folder shipped with the game ===")
import zipfile
zf = zipfile.ZipFile("To.the.Moon.v2023.07.03.Incl.ALL.DLC.zip")
for i in zf.infolist():
    if "/Fonts/" in i.filename and not i.filename.endswith("/"):
        print("  %9d  %s" % (i.file_size, i.filename.split("/Fonts/")[-1]))
