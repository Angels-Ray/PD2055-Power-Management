#include "pdcharge.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop;
static volatile sig_atomic_t g_reload;
static volatile sig_atomic_t g_wake;

static int parse_mhz(const char *s, int *out)
{
	char *end;
	long value;

	errno = 0;
	value = strtol(s, &end, 10);
	if (errno || end == s || *end || value < 100 || value > 4000)
		return -1;
	*out = (int)value;
	return 0;
}

static int cap_target(int target, int thermal, const char **reason)
{
	if (target > 0 && thermal > 0 && target > thermal) {
		*reason = "thermal";
		return thermal;
	}
	return target;
}

static void on_signal(int sig)
{
	if (sig == SIGHUP) {
		g_reload = 1;
		g_wake = 1;
	} else if (sig == SIGUSR1) {
		g_wake = 1;
	} else {
		g_stop = 1;
	}
}

static void install_signals(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);
}

static void append_path(char *dst, size_t size, const char *base, const char *suffix)
{
	size_t blen = strlen(base);
	size_t slen = strlen(suffix);

	if (blen >= size)
		blen = size - 1;
	if (blen + slen >= size)
		slen = size - blen - 1;
	memcpy(dst, base, blen);
	memcpy(dst + blen, suffix, slen);
	dst[blen + slen] = '\0';
}

static void ensure_parent(const char *path)
{
	char tmp[PD_PATH_MAX];
	char *slash;

	snprintf(tmp, sizeof(tmp), "%s", path);
	slash = strrchr(tmp, '/');
	if (!slash || slash == tmp)
		return;
	*slash = '\0';
	ensure_dir(tmp);
}

static int read_pid(const char *path, pid_t *pid)
{
	char buf[32];
	char *end;
	long value;

	if (read_text_file(path, buf, sizeof(buf)) < 0)
		return -1;
	errno = 0;
	value = strtol(buf, &end, 10);
	if (errno || end == buf || value <= 0 || value > 2147483647L)
		return -1;
	*pid = (pid_t)value;
	return 0;
}

static int pid_alive(pid_t pid)
{
	char path[64];
	char cmd[256];
	ssize_t n;
	int fd;

	if (kill(pid, 0) < 0 && errno != EPERM)
		return 0;

	snprintf(path, sizeof(path), "/proc/%d/cmdline", (int)pid);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	n = read(fd, cmd, sizeof(cmd) - 1);
	close(fd);
	if (n <= 0)
		return 0;
	cmd[n] = '\0';
	return strstr(cmd, "pdcharge") != NULL;
}

static int module_inactive(const struct context *ctx)
{
	char path[PD_PATH_MAX];

	path_join(path, sizeof(path), ctx->root, PD_DISABLE_FILE);
	if (file_exists(path))
		return 1;
	path_join(path, sizeof(path), ctx->root, PD_REMOVE_FILE);
	return file_exists(path);
}

static void wake_daemon(struct context *ctx)
{
	pid_t pid;

	if (read_pid(ctx->pid_path, &pid) == 0 && pid_alive(pid))
		kill(pid, SIGUSR1);
}

static void init_context(struct context *ctx, const char *root,
			 const char *config, const char *state, const char *log)
{
	memset(ctx, 0, sizeof(*ctx));
	snprintf(ctx->root, sizeof(ctx->root), "%s", root);
	append_path(ctx->data_dir, sizeof(ctx->data_dir),
		    strcmp(root, "/") ? root : "", PD_DATA_DIR);

	if (config)
		snprintf(ctx->config_path, sizeof(ctx->config_path), "%s", config);
	else
		append_path(ctx->config_path, sizeof(ctx->config_path),
			    ctx->data_dir, "/config.conf");

	if (state)
		snprintf(ctx->state_dir, sizeof(ctx->state_dir), "%s", state);
	else
		append_path(ctx->state_dir, sizeof(ctx->state_dir),
			    ctx->data_dir, "/state");

	if (log)
		snprintf(ctx->log_path, sizeof(ctx->log_path), "%s", log);
	else
		append_path(ctx->log_path, sizeof(ctx->log_path),
			    ctx->data_dir, "/logs/pdcharge.log");

	append_path(ctx->pid_path, sizeof(ctx->pid_path),
		    ctx->state_dir, "/pdcharge.pid");
	append_path(ctx->paused_path, sizeof(ctx->paused_path),
		    ctx->state_dir, "/paused");
	append_path(ctx->active_path, sizeof(ctx->active_path),
		    ctx->state_dir, "/active");
	append_path(ctx->orig_path, sizeof(ctx->orig_path),
		    ctx->state_dir, "/ctm_orig");
	append_path(ctx->applied_path, sizeof(ctx->applied_path),
		    ctx->state_dir, "/ctm_applied");
	append_path(ctx->keep_soc_orig_path, sizeof(ctx->keep_soc_orig_path),
		    ctx->state_dir, "/keep_soc_orig");
	ctx->thermal.rule_index = -1;
	ctx->thermal.charge_ma = -1;
}

static long long mono_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void print_status(struct context *ctx)
{
	struct telemetry t;
	char full[PD_PATH_MAX];
	const char *reason = "-";
	pid_t pid;
	int daemon;
	int keep;
	int module_off;
	int node = -1;
	int target = 0;
	int set_ma = 0;

	daemon = read_pid(ctx->pid_path, &pid) == 0 && pid_alive(pid);
	module_off = module_inactive(ctx);
	keep = keep_soc_effective(ctx);
	telemetry_read(ctx, &t);
	path_join(full, sizeof(full), ctx->root, PD_CTM_NODE);
	if (read_int_file(full, &node) == 0 && node > 0)
		node /= 1000;
	else
		node = -1;

	path_join(full, sizeof(full), ctx->root, PD_SET_MA_NODE);
	if (read_int_file(full, &set_ma) < 0)
		set_ma = 0;

	if (module_off) {
		reason = "module-off";
	} else if (t.online) {
		target = policy_target(&ctx->cfg, &t, &reason);
		target = cap_target(target, thermal_cap_ma(ctx), &reason);
	}

	printf("online=%d fast=%d soc=%d batt=%.1fC board=%.1fC vbus=%.2fV "
	       "imax=%.2fA ibat=%.2fA status=%s type=%s ctm=%dmA target=%dmA reason=%s module=%s daemon=%s dchg=%d boost=%s limit=%d keep=%d\n",
	       t.online, t.fast, t.soc, t.batt_temp_dc / 10.0,
	       t.board_temp_dc / 10.0, t.vbus_uv / 1000000.0,
	       t.current_max_ua / 1000000.0, abs(t.ibat_ua) / 1000000.0,
	       t.status, t.charge_type, node, target, reason,
	       module_off ? "off" : "on",
	       daemon ? "running" : "stopped", t.dchg,
	       set_ma > 0 ? "on" : "off", ctx->cfg.charge_limit_soc, keep);
	thermal_status_line(ctx);
}

int cli_status(struct context *ctx)
{
	print_status(ctx);
	return 0;
}

int cli_once(struct context *ctx)
{
	struct telemetry t;
	const char *reason = "-";
	pid_t pid;
	int target;

	if (read_pid(ctx->pid_path, &pid) == 0 && pid_alive(pid)) {
		fprintf(stderr, "daemon running\n");
		return 1;
	}

	if (module_inactive(ctx)) {
		fprintf(stderr, "module off\n");
		return 1;
	}

	control_restore_state(ctx);
	keep_soc_restore_state(ctx);

	telemetry_read(ctx, &t);
	if (!t.online) {
		printf("offline\n");
		return 0;
	}

	keep_soc_sync(ctx);
	target = policy_target(&ctx->cfg, &t, &reason);
	target = cap_target(target, thermal_cap_ma(ctx), &reason);
	printf("soc=%d batt=%.1fC board=%.1fC vbus=%.2fV target=%dmA reason=%s\n",
	       t.soc, t.batt_temp_dc / 10.0, t.board_temp_dc / 10.0,
	       t.vbus_uv / 1000000.0, target, reason);

	if (target > 0) {
		if (control_apply(ctx, target) < 0)
			fprintf(stderr, "apply failed\n");
	} else {
		control_release(ctx);
	}

	return 0;
}

int cli_pause(struct context *ctx)
{
	FILE *fp;

	ensure_dir(ctx->state_dir);
	fp = fopen(ctx->paused_path, "w");
	if (!fp)
		return 1;
	fclose(fp);
	wake_daemon(ctx);
	log_msg("paused");
	return 0;
}

int cli_resume(struct context *ctx)
{
	unlink(ctx->paused_path);
	wake_daemon(ctx);
	log_msg("resumed");
	return 0;
}

int cli_shutdown(struct context *ctx)
{
	pid_t pid;
	int i;

	if (read_pid(ctx->pid_path, &pid) == 0 && pid_alive(pid)) {
		kill(pid, SIGTERM);
		for (i = 0; i < 250; i++) {
			if (!pid_alive(pid))
				break;
			usleep(100 * 1000);
		}
		if (pid_alive(pid))
			kill(pid, SIGKILL);
	}

	unlink(ctx->pid_path);
	control_restore_state(ctx);
	keep_soc_restore_state(ctx);
	boost_restore_state(ctx);
	cpu_restore_state(ctx);
	log_msg("stopped");
	return 0;
}

static int cli_config_get(struct context *ctx)
{
	if (config_load(ctx) < 0) {
		fprintf(stderr, "config load failed\n");
		return 1;
	}
	config_print(&ctx->cfg);
	return 0;
}

static int cli_config_set(struct context *ctx)
{
	char buf[8192];
	char tmp[PD_PATH_MAX];
	size_t len = 0;
	size_t off;
	ssize_t n;
	pid_t pid;
	int fd;

	while (len < sizeof(buf) - 1) {
		n = read(STDIN_FILENO, buf + len, sizeof(buf) - 1 - len);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "read stdin failed: %s\n",
				strerror(errno));
			return 1;
		}
		if (n == 0)
			break;
		len += (size_t)n;
	}
	if (len == sizeof(buf) - 1) {
		n = read(STDIN_FILENO, buf + len, 1);
		if (n > 0) {
			fprintf(stderr, "config too large\n");
			return 1;
		}
	}
	if (len == 0) {
		fprintf(stderr, "empty config\n");
		return 1;
	}

	ensure_dir(ctx->data_dir);
	ensure_parent(ctx->config_path);
	append_path(tmp, sizeof(tmp), ctx->config_path, ".tmp");
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0) {
		fprintf(stderr, "open %s failed: %s\n", tmp, strerror(errno));
		return 1;
	}

	off = 0;
	while (off < len) {
		n = write(fd, buf + off, len - off);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "write %s failed: %s\n", tmp,
				strerror(errno));
			close(fd);
			unlink(tmp);
			return 1;
		}
		off += (size_t)n;
	}
	if (fsync(fd) < 0 || close(fd) < 0) {
		fprintf(stderr, "sync %s failed: %s\n", tmp, strerror(errno));
		unlink(tmp);
		return 1;
	}
	if (rename(tmp, ctx->config_path) < 0) {
		fprintf(stderr, "rename %s failed: %s\n", tmp,
			strerror(errno));
		unlink(tmp);
		return 1;
	}

	if (read_pid(ctx->pid_path, &pid) == 0 && pid_alive(pid)) {
		kill(pid, SIGHUP);
		printf("ok daemon=running\n");
	} else {
		printf("ok daemon=stopped\n");
	}
	return 0;
}

static int boost_poll_ms(const struct context *ctx, int ms)
{
	if (ctx->boost_active && ms > PD_BOOST_POLL_MAX_MS)
		return PD_BOOST_POLL_MAX_MS;
	return ms;
}

static int thermal_on(const struct config *cfg)
{
	return cfg->thermal_cpu || cfg->thermal_charge || cfg->thermal_online;
}

static int thermal_period_ms(const struct context *ctx, int charging)
{
	if (charging)
		return ctx->cfg.thermal_interval_ms;
	if (ctx->thermal.rule_index >= 0)
		return ctx->cfg.thermal_interval_ms;
	if (ctx->cfg.thermal_rule_count > 0 &&
	    ctx->thermal.temp_dc >= 0 &&
	    ctx->cfg.thermal_rules[0].temp_dc - ctx->thermal.temp_dc <=
		    PD_THERMAL_HYST_DC)
		return ctx->cfg.thermal_interval_ms;
	return ctx->cfg.thermal_idle_interval_ms;
}

static int daemon_loop(struct context *ctx)
{
	struct telemetry t;
	const char *reason;
	const char *boost_reason;
	int module_off = 0;
	int loaded;
	int online;
	int charging;
	int last_online = 0;
	int target;
	int boost_target;
	int thermal;
	int last_thermal = -1;
	long long next_charge = 0;
	long long next_thermal = 0;
	long long now;
	long long wait;

	while (!g_stop) {
		now = mono_ms();

		if (g_reload || config_changed(ctx)) {
			g_reload = 0;
			if (ctx->controlling)
				control_release(ctx);
			boost_release(ctx, "reload");
			thermal_release(ctx, "reload");
			loaded = config_load(ctx);
			if (loaded == 0)
				log_msg("config reloaded max=%d charge_limit=%d temp_stop=%d boost=%d/%d/%d thermal=%d/%d/%d/%d",
					ctx->cfg.max_input_ma,
					ctx->cfg.charge_limit_soc,
					ctx->cfg.temp_stop_dc, ctx->cfg.boost_enabled,
					ctx->cfg.boost_soc_max, ctx->cfg.boost_ma,
					ctx->cfg.thermal_cpu,
					ctx->cfg.thermal_charge,
					ctx->cfg.thermal_online,
					ctx->cfg.thermal_interval_ms);
			else if (loaded > 0)
				log_msg("config unstable");
			next_charge = now;
			next_thermal = now;
		}
		g_wake = 0;

		if (module_inactive(ctx)) {
			if (!module_off) {
				log_msg("module off");
				module_off = 1;
			}
			if (ctx->controlling)
				control_release(ctx);
			boost_release(ctx, "module-off");
			thermal_release(ctx, "module-off");
			keep_soc_release(ctx, "module-off");
			next_charge = now + ctx->cfg.poll_idle_ms;
			next_thermal = LLONG_MAX;
			goto wait;
		}
		if (module_off) {
			log_msg("module on");
			module_off = 0;
		}

		if (file_exists(ctx->paused_path) || !ctx->cfg.enabled) {
			if (ctx->controlling)
				control_release(ctx);
			boost_release(ctx, "paused");
			thermal_release(ctx, "paused");
			keep_soc_release(ctx, "paused");
			next_charge = now + ctx->cfg.poll_idle_ms;
			next_thermal = LLONG_MAX;
			goto wait;
		}

		charger_state(ctx, &online, &charging);
		if (online && !last_online)
			next_charge = now;
		last_online = online;

		if (thermal_on(&ctx->cfg)) {
			if (now >= next_thermal) {
				if (thermal_tick(ctx) < 0)
					next_thermal = mono_ms() +
						       ctx->cfg.thermal_interval_ms;
				else
					next_thermal = mono_ms() +
						       thermal_period_ms(ctx, charging);
			}
		} else {
			thermal_release(ctx, "off");
			next_thermal = LLONG_MAX;
		}

		if (!online) {
			if (ctx->controlling) {
				control_release(ctx);
				log_msg("charger offline");
			}
			boost_release(ctx, "offline");
			keep_soc_release(ctx, "offline");
			next_charge = mono_ms() + ctx->cfg.poll_idle_ms;
			goto wait;
		}

		if (now >= next_charge) {
			telemetry_read(ctx, &t);
			if (!t.online) {
				if (ctx->controlling) {
					control_release(ctx);
					log_msg("charger offline");
				}
				boost_release(ctx, "offline");
				keep_soc_release(ctx, "offline");
				next_charge = mono_ms() + ctx->cfg.poll_idle_ms;
				goto wait;
			}

			thermal = ctx->thermal.charge_ma;
			if (thermal != last_thermal) {
				log_msg("thermal cap %d", thermal);
				last_thermal = thermal;
			}

			keep_soc_sync(ctx);

			boost_target = boost_target_ma(&ctx->cfg, &t, &boost_reason);
			boost_target = cap_target(boost_target, thermal,
						  &boost_reason);
			if (boost_target > 0) {
				if (boost_refresh(ctx, boost_target,
						  ctx->cfg.boost_cable_mohm) < 0)
					log_msg("boost refresh failed reason=%s",
						boost_reason);
			} else if (ctx->boost_active || ctx->kmod_loaded) {
				boost_release(ctx, boost_reason);
			}

			target = policy_target(&ctx->cfg, &t, &reason);
			target = cap_target(target, thermal, &reason);
			if (target <= 0) {
				if (ctx->controlling) {
					control_release(ctx);
					log_msg("hold reason=%s soc=%d batt=%d board=%d",
						reason, t.soc, t.batt_temp_dc,
						t.board_temp_dc);
				}
				if (!strcmp(reason, "soc-unknown"))
					next_charge = mono_ms() +
						boost_poll_ms(ctx, ctx->cfg.poll_active_ms);
				else
					next_charge = mono_ms() +
						boost_poll_ms(ctx, ctx->cfg.poll_idle_ms);
				goto wait;
			}

			if (!ctx->controlling)
				log_msg("session soc=%d batt=%d board=%d vbus=%d target=%d",
					t.soc, t.batt_temp_dc, t.board_temp_dc,
					t.vbus_uv, target);

			if (control_apply(ctx, target) < 0)
				next_charge = mono_ms() +
					boost_poll_ms(ctx, ctx->cfg.poll_idle_ms);
			else
				next_charge = mono_ms() +
					boost_poll_ms(ctx, ctx->cfg.poll_active_ms);
		}

wait:
		now = mono_ms();
		wait = next_charge < next_thermal ? next_charge : next_thermal;
		wait -= now;
		if (wait < 0)
			wait = 0;
		if (wait > 60000)
			wait = 60000;
		sleep_ms((int)wait, &g_stop, &g_wake);
		if (g_wake) {
			next_charge = 0;
			next_thermal = 0;
		}
	}

	return 0;
}

int daemon_run(struct context *ctx)
{
	char pidbuf[32];
	pid_t pid;
	FILE *fp;

	ensure_dir(ctx->data_dir);
	ensure_dir(ctx->state_dir);
	ensure_parent(ctx->log_path);
	log_init(ctx->log_path);

	if (read_pid(ctx->pid_path, &pid) == 0 && pid_alive(pid)) {
		log_msg("already running pid=%d", (int)pid);
		return 0;
	}

	fp = fopen(ctx->pid_path, "w");
	if (!fp) {
		log_msg("pidfile create failed");
		return 1;
	}
	snprintf(pidbuf, sizeof(pidbuf), "%d\n", (int)getpid());
	fputs(pidbuf, fp);
	fclose(fp);

	control_restore_state(ctx);
	keep_soc_restore_state(ctx);
	boost_restore_state(ctx);
	cpu_restore_state(ctx);
	cpu_online_recover(ctx);
	install_signals();
	log_msg("start version=%s root=%s limit=%d thermal=%d/%d/%d/%d", PD_VERSION,
		ctx->root, ctx->cfg.charge_limit_soc, ctx->cfg.thermal_cpu,
		ctx->cfg.thermal_charge, ctx->cfg.thermal_online,
		ctx->cfg.thermal_interval_ms);

	daemon_loop(ctx);

	thermal_release(ctx, "stop");
	control_release(ctx);
	boost_release(ctx, "stop");
	keep_soc_release(ctx, "stop");
	cpu_restore_state(ctx);
	unlink(ctx->pid_path);
	log_msg("stop");
	log_close();
	return 0;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s [--root DIR] [--config FILE] [--state DIR] [--log FILE] CMD\n"
		"commands: daemon once status pause resume reload shutdown version\n"
		"          config-get | config-set\n"
		"          cpu-probe | cpu-on LITTLE_MHZ BIG_MHZ PRIME_MHZ | cpu-off\n"
		"          cpu-status | cpu-unload\n",
		argv0);
}

int main(int argc, char **argv)
{
	struct context ctx;
	const char *root = "/";
	const char *config = NULL;
	const char *state = NULL;
	const char *log = NULL;
	const char *cmd = NULL;
	int cmd_index = -1;
	int log_explicit = 0;
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--root") && i + 1 < argc) {
			root = argv[++i];
		} else if (!strcmp(argv[i], "--config") && i + 1 < argc) {
			config = argv[++i];
		} else if (!strcmp(argv[i], "--state") && i + 1 < argc) {
			state = argv[++i];
		} else if (!strcmp(argv[i], "--log") && i + 1 < argc) {
			log = argv[++i];
			log_explicit = 1;
		} else if (argv[i][0] == '-') {
			usage(argv[0]);
			return 2;
		} else {
			cmd = argv[i];
			cmd_index = i;
			break;
		}
	}

	if (!cmd) {
		usage(argv[0]);
		return 2;
	}

	init_context(&ctx, root, config, state, log);

	if (!strcmp(cmd, "version")) {
		printf("pdcharge %s\n", PD_VERSION);
		return 0;
	}

	if (!strcmp(cmd, "status")) {
		config_load(&ctx);
		return cli_status(&ctx);
	}

	ensure_dir(ctx.data_dir);
	ensure_dir(ctx.state_dir);
	ensure_parent(ctx.config_path);
	config_load(&ctx);

	if (!strncmp(cmd, "cpu-", 4))
		log_init(ctx.log_path);

	if (!strcmp(cmd, "config-get"))
		return cli_config_get(&ctx);
	if (!strcmp(cmd, "config-set"))
		return cli_config_set(&ctx);

	if (!strcmp(cmd, "cpu-probe"))
		return cpu_probe(&ctx);
	if (!strcmp(cmd, "cpu-on")) {
		int little;
		int big;
		int prime;

		if (cmd_index + 3 >= argc ||
		    parse_mhz(argv[cmd_index + 1], &little) < 0 ||
		    parse_mhz(argv[cmd_index + 2], &big) < 0 ||
		    parse_mhz(argv[cmd_index + 3], &prime) < 0) {
			fprintf(stderr,
				"usage: %s cpu-on LITTLE_MHZ BIG_MHZ PRIME_MHZ\n",
				argv[0]);
			return 2;
		}
		return cpu_on(&ctx, little, big, prime);
	}
	if (!strcmp(cmd, "cpu-off"))
		return cpu_off(&ctx);
	if (!strcmp(cmd, "cpu-status"))
		return cpu_status(&ctx);
	if (!strcmp(cmd, "cpu-unload"))
		return cpu_unload(&ctx);

	if (!strcmp(cmd, "once")) {
		if (!log_explicit)
			log_init("-");
		else
			log_init(ctx.log_path);
		return cli_once(&ctx);
	}
	if (!strcmp(cmd, "daemon"))
		return daemon_run(&ctx);
	if (!strcmp(cmd, "pause"))
		return cli_pause(&ctx);
	if (!strcmp(cmd, "resume"))
		return cli_resume(&ctx);
	if (!strcmp(cmd, "shutdown"))
		return cli_shutdown(&ctx);
	if (!strcmp(cmd, "reload")) {
		pid_t pid;

		if (read_pid(ctx.pid_path, &pid) == 0 && pid_alive(pid)) {
			kill(pid, SIGHUP);
			return 0;
		}
		fprintf(stderr, "not running\n");
		return 1;
	}

	usage(argv[0]);
	return 2;
}
