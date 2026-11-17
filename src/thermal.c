#include "pdcharge.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

static const char *const zone_names[] = {
	"skin-therm",
	"case_therm",
	"board_therm",
	"quiet_therm",
	"ap_ntc",
	"quiet-therm-adc",
	"mtktsAP",
};

static int join_entry(char *dst, size_t size, const char *dir,
		      const char *name, const char *suffix)
{
	size_t dlen = strlen(dir);
	size_t nlen = strlen(name);
	size_t slen = strlen(suffix);

	if (dlen + nlen + slen + 2 > size)
		return -1;

	memcpy(dst, dir, dlen);
	dst[dlen] = '/';
	memcpy(dst + dlen + 1, name, nlen);
	memcpy(dst + dlen + 1 + nlen, suffix, slen + 1);
	return 0;
}

static int zone_priority(const char *type)
{
	size_t i;

	for (i = 0; i < sizeof(zone_names) / sizeof(zone_names[0]); i++) {
		if (!strcmp(type, zone_names[i]))
			return (int)i;
	}
	return -1;
}

static int find_zone(struct context *ctx)
{
	char dir_path[PD_PATH_MAX];
	char path[PD_PATH_MAX];
	char type[64];
	char found[PD_PATH_MAX] = "";
	struct dirent *entry;
	int best = 0;
	int used = 0;
	DIR *dir;

	ctx->thermal.zone_path[0] = '\0';
	path_join(dir_path, sizeof(dir_path), ctx->root, PD_THERMAL_ZONE_DIR);
	dir = opendir(dir_path);
	if (!dir)
		return -1;

	while ((entry = readdir(dir))) {
		int priority;

		if (strncmp(entry->d_name, "thermal_zone", 12))
			continue;
		if (join_entry(path, sizeof(path), dir_path, entry->d_name,
			       "/type") < 0)
			continue;
		if (read_text_file(path, type, sizeof(type)) < 0)
			continue;
		priority = zone_priority(type);
		if (priority < 0 || (used && priority >= best))
			continue;
		if (join_entry(found, sizeof(found), dir_path, entry->d_name,
			       "/temp") < 0)
			continue;
		best = priority;
		used = 1;
	}
	closedir(dir);

	if (!used)
		return -1;
	snprintf(ctx->thermal.zone_path, sizeof(ctx->thermal.zone_path), "%s",
		 found);
	return 0;
}

static int thermal_temperature(struct context *ctx, int *temp_dc)
{
	char path[PD_PATH_MAX];
	int value;

	if (ctx->thermal.zone_path[0] &&
	    read_int_file(ctx->thermal.zone_path, &value) == 0) {
		*temp_dc = value / 100;
		return 0;
	}

	if (find_zone(ctx) == 0 &&
	    read_int_file(ctx->thermal.zone_path, &value) == 0) {
		*temp_dc = value / 100;
		return 0;
	}

	path_join(path, sizeof(path), ctx->root, PD_BATTERY_TEMP_NODE);
	if (read_int_file(path, &value) == 0) {
		*temp_dc = value;
		return 0;
	}

	return -1;
}

static int select_rule(const struct config *cfg,
		       const struct thermal_state *state, int temp_dc)
{
	int candidate = -1;
	int hold;
	int i;

	for (i = 0; i < cfg->thermal_rule_count; i++) {
		if (temp_dc >= cfg->thermal_rules[i].temp_dc)
			candidate = i;
		else
			break;
	}

	if (state->rule_index >= 0 && candidate < state->rule_index) {
		if (candidate < 0)
			hold = cfg->thermal_rules[0].temp_dc - PD_THERMAL_HYST_DC;
		else
			hold = cfg->thermal_rules[state->rule_index].temp_dc -
			       PD_THERMAL_HYST_DC;
		if (temp_dc > hold)
			candidate = state->rule_index;
	}

	return candidate;
}

void thermal_release(struct context *ctx, const char *reason)
{
	int released = 0;

	if (ctx->thermal.cpu_applied) {
		cpu_release_targets(ctx);
		released = 1;
	}
	if (ctx->thermal.online_applied) {
		if (cpu_online_restore(ctx) == 0) {
			released = 1;
		} else if (!ctx->thermal.online_error) {
			log_msg("thermal online release failed %s", reason);
			ctx->thermal.online_error = 1;
		}
	}
	if (released)
		log_msg("thermal release %s", reason);
	ctx->thermal.rule_index = -1;
	ctx->thermal.charge_ma = -1;
	ctx->thermal.cpu_error = 0;
	if (!ctx->thermal.online_applied)
		ctx->thermal.online_error = 0;
}

int thermal_tick(struct context *ctx)
{
	const struct thermal_rule *rule;
	int temp_dc;
	int little;
	int big;
	int prime;
	int changed;
	int online_changed;
	int index;
	int previous_index;
	int ret = 0;

	previous_index = ctx->thermal.rule_index;

	if (!ctx->cfg.thermal_cpu && !ctx->cfg.thermal_charge &&
	    !ctx->cfg.thermal_online) {
		thermal_release(ctx, "off");
		return 0;
	}
	if (ctx->cfg.thermal_rule_count == 0) {
		thermal_release(ctx, "no-rule");
		return 0;
	}
	if (thermal_temperature(ctx, &temp_dc) < 0)
		return -1;

	index = select_rule(&ctx->cfg, &ctx->thermal, temp_dc);
	if (index != ctx->thermal.rule_index) {
		log_msg("thermal temp=%d rule=%d", temp_dc,
			index >= 0 ? ctx->cfg.thermal_rules[index].temp_dc : -1);
		ctx->thermal.rule_index = index;
	}
	ctx->thermal.temp_dc = temp_dc;

	if (index < 0) {
		thermal_release(ctx, "low-temp");
		return 0;
	}

	rule = &ctx->cfg.thermal_rules[index];
	ctx->thermal.charge_ma = ctx->cfg.thermal_charge ? rule->charge_ma : -1;

	if (ctx->cfg.thermal_cpu) {
		if (index == previous_index && ctx->thermal.cpu_applied &&
		    !ctx->thermal.cpu_error) {
			little = ctx->thermal.little_khz;
			big = ctx->thermal.big_khz;
			prime = ctx->thermal.prime_khz;
		} else {
			little = cpu_nearest_khz(ctx, 0, rule->little_mhz * 1000);
			big = cpu_nearest_khz(ctx, 4, rule->big_mhz * 1000);
			prime = cpu_nearest_khz(ctx, 7, rule->prime_mhz * 1000);
		}

		changed = !ctx->thermal.cpu_applied ||
			  ctx->thermal.little_khz != little ||
			  ctx->thermal.big_khz != big ||
			  ctx->thermal.prime_khz != prime;

		if (cpu_apply_targets(ctx, little, big, prime) < 0) {
			if (!ctx->thermal.cpu_error)
				log_msg("thermal cpu apply failed temp=%d rule=%d",
					temp_dc, rule->temp_dc);
			ctx->thermal.cpu_error = 1;
			ret = -1;
		} else {
			ctx->thermal.cpu_error = 0;
			if (changed)
				log_msg("thermal cpu %d/%d/%d kHz temp=%d rule=%d",
					little, big, prime, temp_dc,
					rule->temp_dc);
		}
	} else if (ctx->thermal.cpu_applied) {
		cpu_release_targets(ctx);
		log_msg("thermal cpu release");
	}

	if (ctx->cfg.thermal_online) {
		online_changed = !ctx->thermal.online_applied ||
				 ctx->thermal.big_online != rule->big_online ||
				 ctx->thermal.prime_online != rule->prime_online;
		if (cpu_online_apply(ctx, rule->big_online,
				     rule->prime_online) < 0) {
			if (!ctx->thermal.online_error)
				log_msg("thermal online apply failed temp=%d rule=%d",
					temp_dc, rule->temp_dc);
			ctx->thermal.online_error = 1;
			ret = -1;
		} else {
			ctx->thermal.online_error = 0;
			if (online_changed)
				log_msg("thermal online big=%d prime=%d temp=%d rule=%d",
					rule->big_online, rule->prime_online,
					temp_dc, rule->temp_dc);
		}
	} else if (ctx->thermal.online_applied) {
		if (cpu_online_restore(ctx) == 0) {
			log_msg("thermal online release");
		} else if (!ctx->thermal.online_error) {
			log_msg("thermal online release failed temp=%d", temp_dc);
			ctx->thermal.online_error = 1;
		}
	}

	return ret;
}

int thermal_cap_ma(struct context *ctx)
{
	struct thermal_state state = ctx->thermal;
	int temp_dc;
	int index;

	if (!ctx->cfg.thermal_charge || ctx->cfg.thermal_rule_count == 0)
		return -1;
	if (thermal_temperature(ctx, &temp_dc) < 0)
		return -1;

	state.rule_index = -1;
	index = select_rule(&ctx->cfg, &state, temp_dc);
	if (index < 0)
		return -1;

	return ctx->cfg.thermal_rules[index].charge_ma;
}

void thermal_status_line(struct context *ctx)
{
	const struct thermal_rule *rule = NULL;
	struct thermal_state state = ctx->thermal;
	char temp_text[16] = "-";
	char rule_text[80] = "-";
	char cpu_text[32] = "-";
	char charge_text[16] = "-";
	char online_text[32] = "-";
	int little_online = 0;
	int big_online = 0;
	int prime_online = 0;
	int temp_dc;
	int index = -1;

	if (!ctx->cfg.thermal_cpu && !ctx->cfg.thermal_charge &&
	    !ctx->cfg.thermal_online)
		return;

	state.rule_index = -1;
	if (thermal_temperature(ctx, &temp_dc) == 0) {
		snprintf(temp_text, sizeof(temp_text), "%.1fC", temp_dc / 10.0);
		if (ctx->cfg.thermal_rule_count > 0)
			index = select_rule(&ctx->cfg, &state, temp_dc);
	}
	if (index >= 0) {
		rule = &ctx->cfg.thermal_rules[index];
		snprintf(rule_text, sizeof(rule_text),
			 "%d,%d,%d,%d,%d,%d,%d", rule->temp_dc,
			 rule->little_mhz, rule->big_mhz, rule->prime_mhz,
			 rule->charge_ma, rule->big_online,
			 rule->prime_online);
		if (ctx->cfg.thermal_cpu)
			snprintf(cpu_text, sizeof(cpu_text), "%d/%d/%dMHz",
				 rule->little_mhz, rule->big_mhz,
				 rule->prime_mhz);
		if (ctx->cfg.thermal_charge)
			snprintf(charge_text, sizeof(charge_text), "%dmA",
				 rule->charge_ma);
	}
	if (ctx->cfg.thermal_online) {
		cpu_online_counts(ctx, &little_online, &big_online,
				  &prime_online);
		snprintf(online_text, sizeof(online_text), "%d/%d/%d",
			 little_online, big_online, prime_online);
	}

	printf("thermal temp=%s rule=%s cpu=%s charge=%s online=%s\n",
	       temp_text, rule_text, cpu_text, charge_text, online_text);
}
