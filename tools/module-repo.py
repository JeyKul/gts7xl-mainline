#!/usr/bin/env python3
"""Move optional kernel modules out of a modules_install tree into a pacman repo.

Usage: module-repo.py <modules_install root> <output dir> <package release>
Needs only Python, so it also runs on the CI machine, which has no pacman.
"""
import gzip
import hashlib
import io
import os
import sys
import tarfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
LIST = os.path.join(HERE, "..", "modules-repo", "packages.txt")
REPO = "gts7xl"
ARCH = "aarch64"
PACKAGER = "gts7xl-mainline"
URL = "https://github.com/JeyKul/gts7xl-mainline"


def add(tar, name, data, mode=0o644):
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = mode
    info.mtime = int(time.time())
    tar.addfile(info, io.BytesIO(data))


def add_dir(tar, name):
    info = tarfile.TarInfo(name)
    info.type = tarfile.DIRTYPE
    info.mode = 0o755
    info.mtime = int(time.time())
    tar.addfile(info)


def packages():
    with open(LIST) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                name, desc, files = [p.strip() for p in line.split("|")]
                yield name, desc, files.split()


def build(root, out, release, kernel, name, desc, files):
    base = os.path.join("usr/lib/modules", kernel, "kernel")
    version = kernel.split("-")[0] + "-" + release
    blobs = []
    for rel in files:
        path = os.path.join(root, "lib/modules", kernel, "kernel", rel)
        with open(path, "rb") as f:
            blobs.append((os.path.join(base, rel), f.read()))
        os.remove(path)

    size = sum(len(b) for _, b in blobs)
    now = int(time.time())
    pkginfo = (f"pkgname = {name}\npkgbase = {name}\npkgver = {version}\npkgdesc = {desc}\n"
               f"url = {URL}\nbuilddate = {now}\npackager = {PACKAGER}\nsize = {size}\n"
               f"arch = {ARCH}\nlicense = GPL-2.0-only\n")
    install = (f"post_install() {{\n  depmod {kernel}\n}}\n"
               f"post_upgrade() {{\n  depmod {kernel}\n}}\n"
               f"post_remove() {{\n  depmod {kernel}\n}}\n")

    filename = f"{name}-{version}-{ARCH}.pkg.tar.gz"
    pkg = os.path.join(out, filename)
    with tarfile.open(pkg, "w:gz") as tar:
        add(tar, ".PKGINFO", pkginfo.encode())
        add(tar, ".INSTALL", install.encode())
        made = set()
        for path, data in blobs:
            parts = os.path.dirname(path).split("/")
            for i in range(1, len(parts) + 1):
                d = "/".join(parts[:i])
                if d not in made:
                    made.add(d)
                    add_dir(tar, d + "/")
            add(tar, path, data)

    with open(pkg, "rb") as f:
        blob = f.read()
    desc_file = (f"%FILENAME%\n{filename}\n\n%NAME%\n{name}\n\n%BASE%\n{name}\n\n"
                 f"%VERSION%\n{version}\n\n%DESC%\n{desc}\n\n%CSIZE%\n{len(blob)}\n\n"
                 f"%ISIZE%\n{size}\n\n%SHA256SUM%\n{hashlib.sha256(blob).hexdigest()}\n\n"
                 f"%URL%\n{URL}\n\n%ARCH%\n{ARCH}\n\n%BUILDDATE%\n{now}\n\n"
                 f"%PACKAGER%\n{PACKAGER}\n\n")
    return f"{name}-{version}", desc_file


def main():
    root, out, release = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    kernels = os.listdir(os.path.join(root, "lib/modules"))
    if len(kernels) != 1:
        sys.exit(f"expected one kernel in {root}, found {kernels}")
    kernel = kernels[0]

    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w") as db:
        for name, desc, files in packages():
            entry, desc_file = build(root, out, release, kernel, name, desc, files)
            add_dir(db, entry + "/")
            add(db, entry + "/desc", desc_file.encode())
            print("packaged", entry)
    with open(os.path.join(out, REPO + ".db"), "wb") as f:
        f.write(gzip.compress(raw.getvalue()))


main()
