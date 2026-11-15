#!/vendor/bin/sh
# The DSPs' firmware lives on apnhlos, which is mounted after their drivers
# probe: start them now. Then the speaker route (same as Linux's audio-up).
for r in /sys/class/remoteproc/remoteproc*; do
	[ "$(cat $r/state)" = offline ] && echo start > $r/state
done
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
