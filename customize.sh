#!/system/bin/sh

ui_print "- PD2055 Charge"

chmod 0755 "$MODPATH/bin/pdcharge"
chmod 0755 "$MODPATH"/*.sh

CONF_DIR=/data/adb/pd2055-charge
if [ ! -f "$CONF_DIR/config.conf" ]; then
	mkdir -p "$CONF_DIR"
	cp "$MODPATH/config.conf" "$CONF_DIR/config.conf"
	chmod 0600 "$CONF_DIR/config.conf"
fi
