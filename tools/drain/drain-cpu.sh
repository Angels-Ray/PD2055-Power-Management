#!/system/bin/sh
# PD2055 CPU drain. Runs as shell; no root.
# usage: sh drain-cpu.sh [seconds]

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

pids=
cleaned=0

cleanup() {
	[ "$cleaned" -eq 1 ] && return
	cleaned=1

	[ -n "$pids" ] && kill $pids 2>/dev/null
	[ -n "$pids" ] && wait $pids 2>/dev/null
	echo "cpu drain: stopped"
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

count=0
for c in $cpus; do
	yes >/dev/null 2>&1 &
	p=$!
	# Pin when the cgroup allows the CPU.
	taskset -p "$(printf '%x' $((1 << c)))" "$p" >/dev/null 2>&1
	pids="$pids $p"
	count=$((count + 1))
done

echo "cpu drain: start; $count cpus; ctrl-c to stop"

while :; do
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
