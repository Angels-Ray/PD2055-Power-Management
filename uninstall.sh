#!/system/bin/sh

MODDIR=${0%/*}
"$MODDIR/bin/pdcharge" shutdown >/dev/null 2>&1
echo 0 > /sys/class/stm32_debug/cable_mohm 2>/dev/null
if [ -d /sys/module/psset ]; then
	echo 0 > /sys/module/psset/parameters/set_ma 2>/dev/null
	rmmod psset
fi
if [ -d /sys/module/pd2055_cpufreq ]; then
	echo 0 > /sys/module/pd2055_cpufreq/parameters/takeover 2>/dev/null
	echo 1 > /sys/module/pd2055_cpufreq/parameters/apply 2>/dev/null
	rmmod pd2055_cpufreq
fi
