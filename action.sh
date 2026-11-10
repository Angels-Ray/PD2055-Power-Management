#!/system/bin/sh

MODDIR=${0%/*}
BIN="$MODDIR/bin/pdcharge"

if [ -f /data/adb/pd2055-charge/state/paused ]; then
	"$BIN" resume
else
	"$BIN" pause
fi

"$BIN" status
