#!/system/bin/sh

ui_print "- PD2055 Power Management"

chmod 0755 "$MODPATH/bin/pdcharge"
chmod 0755 "$MODPATH"/*.sh

CONF_DIR=/data/adb/pd2055-power-management
if [ ! -f "$CONF_DIR/config.conf" ]; then
	mkdir -p "$CONF_DIR"
	cp "$MODPATH/config.conf" "$CONF_DIR/config.conf"
	chmod 0600 "$CONF_DIR/config.conf"
fi

APP="$MODPATH/app/PD2055PowerManagement.apk"
if [ -f "$APP" ]; then
	if /system/bin/pm install -r "$APP" >/dev/null 2>&1; then
		ui_print "- 已安装管理 app"
	else
		ui_print "- 管理 app 未自动安装"
		if cp "$APP" /data/local/tmp/PD2055PowerManagement.apk 2>/dev/null; then
			chmod 0644 /data/local/tmp/PD2055PowerManagement.apk
			ui_print "  手动安装：adb shell pm install -r /data/local/tmp/PD2055PowerManagement.apk"
		else
			ui_print "  手动安装：adb shell pm install -r $APP"
		fi
	fi
fi
