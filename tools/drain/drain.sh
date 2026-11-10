#!/system/bin/sh
# PD2055 battery drain. Run as root.
# usage: sh drain.sh [seconds]

if [ "$(id -u)" -ne 0 ]; then
	echo "drain: root required" >&2
	exit 1
fi

duration=${1:-0}
case $duration in
	''|*[!0-9]*)
		echo "usage: sh $0 [seconds]" >&2
		exit 2
		;;
esac

interval=10
end=0
[ "$duration" -gt 0 ] && end=$(( $(date +%s) + duration ))

battery=/sys/class/power_supply/battery
wake_lock=/sys/power/wake_lock
wake_unlock=/sys/power/wake_unlock
torch=/sys/class/leds/led:torch_0/brightness
torch_max_path=/sys/class/leds/led:torch_0/max_brightness
switch=/sys/class/leds/led:switch_0/brightness
switch_max_path=/sys/class/leds/led:switch_0/max_brightness

pids=
saved_govs=
torch_orig=
switch_orig=
scr_bright=
scr_mode=
scr_timeout=
wifi_was=
bt_was=
cleaned=0

cleanup() {
	[ "$cleaned" -eq 1 ] && return
	cleaned=1

	[ -n "$pids" ] && kill $pids 2>/dev/null
	[ -n "$pids" ] && wait $pids 2>/dev/null

	# Disable the flash regulator before restoring the torch current.
	if [ -n "$switch_orig" ] && [ -w "$switch" ]; then
		echo 0 > "$switch" 2>/dev/null
	fi
	if [ -n "$torch_orig" ] && [ -w "$torch" ]; then
		echo "$torch_orig" > "$torch" 2>/dev/null
	fi

	for item in $saved_govs; do
		cpu=${item%%:*}
		gov=${item#*:}
		echo "$gov" > "/sys/devices/system/cpu/cpu$cpu/cpufreq/scaling_governor" 2>/dev/null
	done

	[ -n "$scr_bright" ] && settings put system screen_brightness "$scr_bright" >/dev/null 2>&1
	[ -n "$scr_mode" ] && settings put system screen_brightness_mode "$scr_mode" >/dev/null 2>&1
	[ -n "$scr_timeout" ] && settings put system screen_off_timeout "$scr_timeout" >/dev/null 2>&1

	[ "$wifi_was" = 0 ] && svc wifi disable >/dev/null 2>&1
	[ "$bt_was" = 0 ] && svc bluetooth disable >/dev/null 2>&1

	echo pd2055-drain > "$wake_unlock" 2>/dev/null
	echo "drain: stopped"
}

trap 'cleanup; exit 0' INT TERM HUP
trap cleanup EXIT

cpu_list=$(cat /sys/devices/system/cpu/online 2>/dev/null)
[ -n "$cpu_list" ] || cpu_list=0
cpus=
for part in $(echo "$cpu_list" | tr ',' ' '); do
	case $part in
		*-*)
			first=${part%-*}
			last=${part#*-}
			c=$first
			while [ "$c" -le "$last" ]; do
				cpus="$cpus $c"
				c=$((c + 1))
			done
			;;
		*)
			cpus="$cpus $part"
			;;
	esac
done

# Read every governor before starting load. The performance HAL switches
# CPUs to performance as soon as load appears.
for c in $cpus; do
	governor=/sys/devices/system/cpu/cpu$c/cpufreq/scaling_governor
	[ -w "$governor" ] || continue
	gov=$(cat "$governor" 2>/dev/null) || continue
	saved_govs="$saved_govs $c:$gov"
done

for c in $cpus; do
	governor=/sys/devices/system/cpu/cpu$c/cpufreq/scaling_governor
	[ -w "$governor" ] && echo performance > "$governor" 2>/dev/null
	yes >/dev/null 2>&1 &
	p=$!
	taskset -p "$(printf '%x' $((1 << c)))" "$p" >/dev/null 2>&1
	pids="$pids $p"
done

scr_bright=$(settings get system screen_brightness 2>/dev/null)
scr_mode=$(settings get system screen_brightness_mode 2>/dev/null)
scr_timeout=$(settings get system screen_off_timeout 2>/dev/null)
max_bright=$(cat /sys/class/backlight/panel0-backlight/max_brightness 2>/dev/null)
[ -n "$max_bright" ] || max_bright=2047
settings put system screen_brightness_mode 0 >/dev/null 2>&1
settings put system screen_brightness "$max_bright" >/dev/null 2>&1
settings put system screen_off_timeout 2147483647 >/dev/null 2>&1
input keyevent 224 >/dev/null 2>&1

echo pd2055-drain > "$wake_lock" 2>/dev/null

if [ "${TORCH:-1}" = 1 ] && [ -w "$torch" ]; then
	torch_orig=$(cat "$torch" 2>/dev/null)
	torch_max=$(cat "$torch_max_path" 2>/dev/null)
	[ -n "$torch_max" ] || torch_max=500
	if [ -w "$switch" ]; then
		switch_orig=$(cat "$switch" 2>/dev/null)
		switch_max=$(cat "$switch_max_path" 2>/dev/null)
		[ -n "$switch_max" ] || switch_max=255
	fi
	echo "$torch_max" > "$torch" 2>/dev/null
	[ -n "$switch_orig" ] && echo "$switch_max" > "$switch" 2>/dev/null
fi

wifi_was=$(settings get global wifi_on 2>/dev/null)
bt_was=$(settings get global bluetooth_on 2>/dev/null)
[ "$wifi_was" = 1 ] || svc wifi enable >/dev/null 2>&1
[ "$bt_was" = 1 ] || svc bluetooth enable >/dev/null 2>&1
ping -i 0.2 -s 1400 1.1.1.1 >/dev/null 2>&1 &
pids="$pids $!"

if [ "$(cat /sys/class/power_supply/usb/online 2>/dev/null)" = 1 ]; then
	echo "drain: usb online; unplug to drain" >&2
fi

echo "drain: start; ctrl-c to stop"

while :; do
	batt_status=$(cat "$battery/status" 2>/dev/null)
	capacity=$(cat "$battery/capacity" 2>/dev/null)
	temp=$(cat "$battery/temp" 2>/dev/null)
	volt=$(cat "$battery/voltage_now" 2>/dev/null)
	curr=$(cat "$battery/current_now" 2>/dev/null)

	batt_status=${batt_status:-unknown}
	capacity=${capacity:-0}
	temp=${temp:-0}
	volt=${volt:-0}
	curr=${curr:-0}
	curr_abs=${curr#-}
	mw=$(( (volt / 1000) * (curr_abs / 1000) / 1000 ))

	printf 'drain: %s %s%% %d.%dC %d.%02dV %dmA %dmW\n' \
		"$batt_status" "$capacity" \
		"$((temp / 10))" "$((temp % 10))" \
		"$((volt / 1000000))" "$(((volt % 1000000) / 10000))" \
		"$((curr_abs / 1000))" "$mw"

	if [ "$duration" -gt 0 ]; then
		left=$((end - $(date +%s)))
		[ "$left" -le 0 ] && break
		if [ "$left" -lt "$interval" ]; then
			sleep "$left"
		else
			sleep "$interval"
		fi
	else
		sleep "$interval"
	fi
done
