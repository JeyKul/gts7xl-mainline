#!/bin/bash
# Build the RAM installer tarball. Usage: build.sh <s1ui binary> <modules.tar.gz> <output.tar.gz>
set -euo pipefail
UI=$(realpath "$1"); MODS=$(realpath "$2"); OUT=$(realpath -m "$3")
HERE=$(dirname "$(realpath "$0")"); ALPINE=https://dl-cdn.alpinelinux.org/alpine/v3.22
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
R=$T/root; mkdir -p "$R/gts7xl"
F=$(curl -fsSL "$ALPINE/releases/aarch64/latest-releases.yaml" | grep -oE 'alpine-minirootfs-[0-9.]+-aarch64\.tar\.gz' | head -1)
curl -fsSL -o "$T/$F" "$ALPINE/releases/aarch64/$F"
(cd "$T" && curl -fsSL "$ALPINE/releases/aarch64/$F.sha256" | sha256sum -c - >/dev/null)
tar -xzf "$T/$F" -C "$R" --no-same-owner --exclude='dev/*'
curl -fsSL "$ALPINE/main/aarch64/APKINDEX.tar.gz" | tar -xz -C "$T" APKINDEX
python3 -I - "$T/APKINDEX" > "$T/pkgs" <<'P'
import re,sys
pk={};prov={};cur={}
for l in open(sys.argv[1],errors='ignore'):
    l=l.rstrip('\n')
    if not l:
        if 'P' in cur:
            pk[cur['P']]=cur
            for x in cur.get('p','').split(): prov.setdefault(re.split('[=<>~]',x)[0],cur['P'])
        cur={};continue
    if l[1:2]==':': cur[l[0]]=l[2:]
seen=[]
def add(n):
    n=prov.get(n,n)
    if n in seen or n not in pk: return
    seen.append(n)
    for d in pk[n].get('D','').split():
        if not d.startswith('!'): add(re.split('[=<>~]',d)[0])
for w in ('curl','e2fsprogs','libarchive-tools','ca-certificates-bundle','bash'): add(w)
for n in seen: print(f"{n}-{pk[n]['V']}.apk")
P
while read -r p; do
	curl -fsSL -o "$T/$p" "$ALPINE/main/aarch64/$p"
	tar -xzf "$T/$p" -C "$R" --no-same-owner --exclude='.SIGN*' --exclude='.PKGINFO' \
		--exclude='.pre-*' --exclude='.post-*' --exclude='.trigger' 2>/dev/null
done < "$T/pkgs"
# GPU firmware from linux-firmware: Arch's packages don't provide these two here
LFW=https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/qcom
mkdir -p "$R/gts7xl/firmware/qcom"
for f in a650_sqe.fw a650_gmu.bin; do curl -fsSL -o "$R/gts7xl/firmware/qcom/$f" "$LFW/$f"; done
install -m755 "$UI" "$R/s1ui"
install -m755 "$HERE/install.sh" "$R/install.sh"
cp -a "$HERE/../overlay" "$R/gts7xl/overlay"
install -m755 "$HERE/../arch-setup.sh" "$R/gts7xl/arch-setup.sh"
cp -a "$HERE/../../sensord" "$R/gts7xl/sensord"
cp "$MODS" "$R/gts7xl/modules.tar.gz"
# Whoever builds this, everything in it belongs to root
tar -C "$R" --owner=0 --group=0 --numeric-owner -czf "$OUT" .
du -h "$OUT" | cut -f1
