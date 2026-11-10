#!/system/bin/sh

MODDIR=${0%/*}
exec "$MODDIR/bin/pdcharge" daemon
