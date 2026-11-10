#include "pdcharge.h"

#include <stdio.h>
#include <string.h>

static int read_first_int(const struct context *ctx, const char *const *paths,
			  int count, int *value)
{
	char full[PD_PATH_MAX];
	int i;

	for (i = 0; i < count; i++) {
		path_join(full, sizeof(full), ctx->root, paths[i]);
		if (read_int_file(full, value) == 0)
			return 0;
	}

	return -1;
}

static int read_max_int(const struct context *ctx, const char *const *paths,
			int count, int *value)
{
	char full[PD_PATH_MAX];
	int best = 0;
	int v;
	int i;

	for (i = 0; i < count; i++) {
		path_join(full, sizeof(full), ctx->root, paths[i]);
		if (read_int_file(full, &v) == 0 && v > best)
			best = v;
	}

	*value = best;
	return 0;
}

static int read_first_text(const struct context *ctx, const char *const *paths,
			   int count, char *buf, size_t size)
{
	char full[PD_PATH_MAX];
	int i;

	for (i = 0; i < count; i++) {
		path_join(full, sizeof(full), ctx->root, paths[i]);
		if (read_text_file(full, buf, size) >= 0)
			return 0;
	}

	return -1;
}

int charger_online(const struct context *ctx)
{
	char full[PD_PATH_MAX];
	char status[32];
	int value;

	path_join(full, sizeof(full), ctx->root,
		  "/sys/class/power_supply/battery/status");
	if (read_text_file(full, status, sizeof(status)) >= 0)
		return !strcmp(status, "Charging") || !strcmp(status, "Full");

	path_join(full, sizeof(full), ctx->root,
		  "/sys/class/power_supply/usb/online");
	if (read_int_file(full, &value) == 0)
		return value > 0;

	path_join(full, sizeof(full), ctx->root,
		  "/sys/class/power_supply/pc_port/online");
	if (read_int_file(full, &value) == 0)
		return value > 0;

	return 0;
}

int telemetry_read(const struct context *ctx, struct telemetry *t)
{
	static const char *const soc_paths[] = {
		"/sys/class/power_supply/battery/capacity",
	};
	static const char *const batt_temp_paths[] = {
		"/sys/class/power_supply/battery/temp",
	};
	static const char *const board_temp_paths[] = {
		"/sys/class/power_supply/battery/primary_board_temp",
		"/sys/class/cms_class/primary_board_temp",
	};
	static const char *const vbus_paths[] = {
		"/sys/class/power_supply/usb/voltage_now",
	};
	static const char *const current_max_paths[] = {
		"/sys/class/power_supply/usb/current_max",
		"/sys/class/power_supply/pc_port/current_max",
		"/sys/class/power_supply/usb/pd_current_max",
	};
	static const char *const ibat_paths[] = {
		"/sys/class/power_supply/battery/current_now",
	};
	static const char *const online_paths[] = {
		"/sys/class/power_supply/usb/online",
		"/sys/class/power_supply/pc_port/online",
	};
	static const char *const status_paths[] = {
		"/sys/class/power_supply/battery/status",
	};
	static const char *const type_paths[] = {
		"/sys/class/power_supply/battery/charge_type",
	};
	int value;
	int status_known;

	memset(t, 0, sizeof(*t));
	t->soc = -1;

	if (read_first_int(ctx, soc_paths, 1, &t->soc) < 0 || t->soc > 100)
		t->soc = -1;

	read_first_int(ctx, batt_temp_paths, 1, &t->batt_temp_dc);
	read_first_int(ctx, board_temp_paths, 2, &t->board_temp_dc);
	read_first_int(ctx, ibat_paths, 1, &t->ibat_ua);
	read_first_int(ctx, vbus_paths, 1, &t->vbus_uv);
	read_max_int(ctx, current_max_paths, 3, &t->current_max_ua);

	status_known = read_first_text(ctx, status_paths, 1,
				       t->status, sizeof(t->status)) == 0;
	if (!status_known) {
		snprintf(t->status, sizeof(t->status), "Unknown");
		if (read_first_int(ctx, online_paths, 2, &value) == 0)
			t->online = value > 0;
	} else if (!strcmp(t->status, "Charging") || !strcmp(t->status, "Full")) {
		t->online = 1;
	}

	if (read_first_text(ctx, type_paths, 1, t->charge_type,
			    sizeof(t->charge_type)) < 0)
		snprintf(t->charge_type, sizeof(t->charge_type), "Unknown");

	if (!strcmp(t->charge_type, "Fast") || !strcmp(t->charge_type, "Turbo")) {
		if (t->current_max_ua >= 1000000 || t->vbus_uv >= PD_VBUS_FAST_UV)
			t->fast = 1;
	} else if (t->current_max_ua >= 1000000 && t->vbus_uv >= PD_VBUS_FAST_UV) {
		t->fast = 1;
	}

	return 0;
}

int policy_target(const struct config *cfg, const struct telemetry *t, const char **reason)
{
	int ma;

	*reason = "active";

	if (!cfg->enabled) {
		*reason = "disabled";
		return 0;
	}
	if (t->soc < 0) {
		*reason = "soc-unknown";
		return 0;
	}
	if (t->soc >= cfg->soc_full) {
		*reason = "soc";
		return 0;
	}
	if (t->batt_temp_dc > 0 && t->batt_temp_dc >= cfg->temp_stop_dc) {
		*reason = "battery-temp";
		return cfg->min_input_ma;
	}
	if (t->board_temp_dc > 0 && t->board_temp_dc >= cfg->board_temp_stop_dc) {
		*reason = "board-temp";
		return cfg->min_input_ma;
	}
	if (!t->fast) {
		*reason = "protocol";
		return 0;
	}

	ma = cfg->max_input_ma;
	if (t->soc > cfg->soc_mid) {
		ma = ma * cfg->soc_mid_scale / 100;
		*reason = "soc-derate";
	}
	if (t->batt_temp_dc > 0 && t->batt_temp_dc >= cfg->temp_derate_dc) {
		ma = ma * 60 / 100;
		*reason = "battery-derate";
	}
	if (t->board_temp_dc > 0 && t->board_temp_dc >= cfg->board_temp_derate_dc) {
		ma = ma * 60 / 100;
		*reason = "board-derate";
	}

	if (ma < cfg->min_input_ma)
		ma = cfg->min_input_ma;
	if (ma > PD_HARD_MAX_INPUT_MA)
		ma = PD_HARD_MAX_INPUT_MA;

	return ma;
}
