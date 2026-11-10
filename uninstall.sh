#!/system/bin/sh

MODDIR=${0%/*}
"$MODDIR/bin/pdcharge" shutdown >/dev/null 2>&1
