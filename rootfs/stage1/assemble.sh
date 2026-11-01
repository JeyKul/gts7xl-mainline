#!/bin/bash
# Add the stage 1 installer to an initramfs tree.
# Usage: assemble.sh <kernel build dir> <initramfs dir> [strip tool]
# The kernel's modules must already be built. Everything here is redistributable:
# Alpine packages, this project's code, and linux-firmware's Wi-Fi files.
set -euo pipefail
BUILD=$(realpath "$1"); ROOT=$(realpath "$2"); STRIP=${3:-strip}; CC=${CC:-gcc}
HERE=$(dirname "$(realpath "$0")")
ALPINE=https://dl-cdn.alpinelinux.org/alpine/v3.22
FW=https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/ath11k/QCA6390/hw2.0
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT

rm -rf "$ROOT/s1" "$ROOT/mods" "$ROOT/lib/firmware/ath11k"
mkdir -p "$ROOT/s1" "$ROOT/mods" "$ROOT/lib/firmware/ath11k/QCA6390/hw2.0"

# Alpine base
F=$(curl -fsSL "$ALPINE/releases/aarch64/latest-releases.yaml" | grep -oE 'alpine-minirootfs-[0-9.]+-aarch64\.tar\.gz' | head -1)
curl -fsSL -o "$T/$F" "$ALPINE/releases/aarch64/$F"
(cd "$T" && curl -fsSL "$ALPINE/releases/aarch64/$F.sha256" | sha256sum -c - >/dev/null)
tar -xzf "$T/$F" -C "$ROOT/s1" --no-same-owner --exclude='dev/*'

# Wi-Fi tools and what they depend on
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
for w in ('wpa_supplicant','iw','ca-certificates-bundle'): add(w)
for n in seen: print(f"{n}-{pk[n]['V']}.apk")
P
while read -r p; do
	curl -fsSL -o "$T/$p" "$ALPINE/main/aarch64/$p"
	tar -xzf "$T/$p" -C "$ROOT/s1" --no-same-owner --exclude='.SIGN*' --exclude='.PKGINFO' \
		--exclude='.pre-*' --exclude='.post-*' --exclude='.trigger' 2>/dev/null
done < "$T/pkgs"
rm -rf "$ROOT/s1/usr/share/apk" "$ROOT/s1/usr/lib/engines-3"

# Touch program
"$CC" -static -Os -o "$ROOT/s1/s1ui" "$HERE/s1ui.c"
"$STRIP" "$ROOT/s1/s1ui"

# Wi-Fi firmware
for f in amss.bin board-2.bin m3.bin; do
	curl -fsSL -o "$ROOT/lib/firmware/ath11k/QCA6390/hw2.0/$f" "$FW/$f"
done

# Display and Wi-Fi modules, dependencies first
cd "$BUILD"
python3 -I - > "$T/order" <<'P'
import os,subprocess
idx={}
for r,_,fs in os.walk('.'):
    for f in fs:
        if f.endswith('.ko'): idx[f[:-3].replace('-','_')]=os.path.join(r,f)
seen=[]
def add(n):
    n=n.replace('-','_')
    if n in seen: return
    if n not in idx: raise SystemExit('missing module '+n)
    o=subprocess.run(['modinfo','-F','depends',idx[n]],capture_output=True,text=True,check=True).stdout.strip()
    for d in filter(None,o.split(',')): add(d)
    seen.append(n)
for n in ('msm','panel_samsung_s6tuum3','pwrseq_qcom_wcn','pci_pwrctrl_pwrseq','qrtr_mhi','ath11k_pci'): add(n)
for n in seen: print(idx[n])
P
: > "$ROOT/mods/load.sh"
while read -r k; do
	b=$(basename "$k")
	"$STRIP" --strip-debug -o "$ROOT/mods/$b" "$k"
	case "$b" in
	msm.ko) echo "insmod /mods/$b separate_gpu_kms=1 r8q_zap_dyn=1 r8q_zap_secvid=0" >> "$ROOT/mods/load.sh" ;;
	*) echo "insmod /mods/$b" >> "$ROOT/mods/load.sh" ;;
	esac
done < "$T/order"
echo "stage 1: $(du -sh "$ROOT/s1" | cut -f1) Alpine, $(du -sh "$ROOT/mods" | cut -f1) modules"
