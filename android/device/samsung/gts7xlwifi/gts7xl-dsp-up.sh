#!/vendor/bin/sh
# The DSPs' firmware lives on apnhlos, which is mounted after their drivers
# probe: start them now. Then the speaker route (same as Linux's audio-up).
for r in /sys/class/remoteproc/remoteproc*; do
	[ "$(cat $r/state)" = offline ] && echo start > $r/state
done
# Speaker calibration: this tablet's own factory values from sec_efs (the copies in
# /vendor are from one tablet). The amps read them at the first playback.
# rdc, temp, vsc, isc as 4 x u32 LE; amp 40=_r 41= 42=_br 43=_b. If sec_efs can't
# be read, an empty file: the driver then skips calibration.
C=/dev/gts7xl-cal; E=$C/efs
mkdir -p $E
u32() { n=$1; printf "\\$(printf %o $((n & 255)))\\$(printf %o $((n >> 8 & 255)))\\$(printf %o $((n >> 16 & 255)))\\$(printf %o $((n >> 24 & 255)))"; }
num() { tr -d '\0\n ' < $E/cirrus/$1; }
mount -t ext4 -o ro,noload,nosuid,nodev /dev/block/by-name/sec_efs $E
for a in 0040:_r 0041: 0042:_br 0043:_b; do
	s=${a#*:}; a=${a%%:*}; f=$C/cs35l41-cal-11-$a.bin
	r=$(num rdc_cal$s) t=$(num temp_cal) v=$(num vsc_cal$s) i=$(num isc_cal$s)
	if [ "${r:-0}" -gt 5000 ] 2>/dev/null && [ "$r" -lt 15000 ] && [ -n "$t" ] && [ -n "$v" ] && [ -n "$i" ]; then
		{ u32 $r; u32 $t; u32 $v; u32 $i; } > $f
	else
		echo "gts7xl-dsp-up: no calibration for amp $a"; : > $f
	fi
	mount -o bind $f /vendor/firmware/cirrus/cs35l41-cal-11-$a.bin
done
umount $E
for i in $(seq 1 30); do [ -e /dev/snd/controlC0 ] && break; sleep 1; done
# Stock amp firmware cannot wake from hibernate: keep the amps awake
for d in /sys/bus/i2c/devices/11-004[0-3]; do echo on > $d/power/control; done
m() { /system/bin/tinymix "$@" >/dev/null; }
for a in FL FR RL RR; do
	m "$a Analog PCM Volume" 17
	m "$a Digital PCM Volume" 817
	m "$a DSP1 Preload Switch" 1
	m "$a DSP RX2 Source" ASPRX1
	m "$a PCM Source" DSP
done
m "PRIMARY_TDM_RX_0 Audio Mixer MultiMedia1" 1
# Microphone: the sub mic, DMIC3, through the VA macro to MultiMedia2 (hw:0,1),
# +24 dB like on Linux; the main mic (DMIC1) reads zero so far
m "VA DEC0 MUX" VA_DMIC
m "VA DMIC MUX0" DMIC3
m "VA_AIF1_CAP Mixer DEC0" 1
m "VA_DEC0 Volume" 108
m "MultiMedia2 Mixer VA_CODEC_DMA_TX_0" 1
