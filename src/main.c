#include "pdcharge.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop;
static volatile sig_atomic_t g_reload;
static volatile sig_atomic_t g_wake;

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
}

static void print_status(const struct context *ctx)
{
	struct telemetry t;
	char full[PD_PATH_MAX];
	const char *reason = "-";
	int node = -1;
	int target = 0;

	telemetry_read(ctx, &t);
	path_join(full, sizeof(full), ctx->root, PD_CTM_NODE);
	if (read_int_file(full, &node) == 0 && node > 0)
		node /= 1000;
	else
		node = -1;

	if (t.online)
		target = policy_target(&ctx->cfg, &t, &reason);

	printf("online=%d fast=%d soc=%d batt=%.1fC board=%.1fC vbus=%.2fV "
	       "imax=%.2fA ibat=%.2fA status=%s type=%s ctm=%dmA target=%dmA reason=%s module=%s\n",
	       t.online, t.fast, t.soc, t.batt_temp_dc / 10.0,
	       t.board_temp_dc / 10.0, t.vbus_uv / 1000000.0,
	       t.current_max_ua / 1000000.0, abs(t.ibat_ua) / 1000000.0,
	       t.status, t.charge_type, node, target, reason,
	       module_inactive(ctx) ? "off" : "on");
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

	telemetry_read(ctx, &t);
	if (!t.online) {
		printf("offline\n");
		return 0;
	}

	target = policy_target(&ctx->cfg, &t, &reason);
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
		for (i = 0; i < 50; i++) {
			if (!pid_alive(pid))
				break;
			usleep(100 * 1000);
		}
		if (pid_alive(pid))
			kill(pid, SIGKILL);
	}

	unlink(ctx->pid_path);
	control_restore_state(ctx);
	log_msg("stopped");
	return 0;
}

static int daemon_loop(struct context *ctx)
{
	struct telemetry t;
	const char *reason;
	int module_off = 0;
	int loaded;
	int target;

	while (!g_stop) {
		if (g_reload || config_changed(ctx)) {
			g_reload = 0;
			if (ctx->controlling)
				control_release(ctx);
			loaded = config_load(ctx);
			if (loaded == 0)
				log_msg("config reloaded max=%d soc_full=%d temp_stop=%d",
					ctx->cfg.max_input_ma, ctx->cfg.soc_full,
					ctx->cfg.temp_stop_dc);
			else if (loaded > 0)
				log_msg("config unstable");
		}
		g_wake = 0;

		if (module_inactive(ctx)) {
			if (!module_off) {
				log_msg("module off");
				module_off = 1;
			}
			if (ctx->controlling)
				control_release(ctx);
			sleep_ms(ctx->cfg.poll_idle_ms, &g_stop, &g_wake);
			continue;
		}
		if (module_off) {
			log_msg("module on");
			module_off = 0;
		}

		if (file_exists(ctx->paused_path) || !ctx->cfg.enabled) {
			if (ctx->controlling)
				control_release(ctx);
			sleep_ms(ctx->cfg.poll_idle_ms, &g_stop, &g_wake);
			continue;
		}

		if (!charger_online(ctx)) {
			if (ctx->controlling) {
				control_release(ctx);
				log_msg("charger offline");
			}
			sleep_ms(ctx->cfg.poll_idle_ms, &g_stop, &g_wake);
			continue;
		}

		telemetry_read(ctx, &t);
		if (!t.online) {
			if (ctx->controlling) {
				control_release(ctx);
				log_msg("charger offline");
			}
			sleep_ms(ctx->cfg.poll_idle_ms, &g_stop, &g_wake);
			continue;
		}

		target = policy_target(&ctx->cfg, &t, &reason);
		if (target <= 0) {
			if (ctx->controlling) {
				control_release(ctx);
				log_msg("hold reason=%s soc=%d batt=%d board=%d",
					reason, t.soc, t.batt_temp_dc, t.board_temp_dc);
			}
			if (!strcmp(reason, "soc-unknown"))
				sleep_ms(ctx->cfg.poll_active_ms, &g_stop, &g_wake);
			else
				sleep_ms(ctx->cfg.poll_idle_ms, &g_stop, &g_wake);
			continue;
		}

		if (!ctx->controlling)
			log_msg("session soc=%d batt=%d board=%d vbus=%d target=%d",
				t.soc, t.batt_temp_dc, t.board_temp_dc,
				t.vbus_uv, target);

		if (control_apply(ctx, target) < 0)
			sleep_ms(ctx->cfg.poll_idle_ms, &g_stop, &g_wake);
		else
			sleep_ms(ctx->cfg.poll_active_ms, &g_stop, &g_wake);
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
	install_signals();
	log_msg("start version=%s root=%s", PD_VERSION, ctx->root);

	daemon_loop(ctx);

	control_release(ctx);
	unlink(ctx->pid_path);
	log_msg("stop");
	log_close();
	return 0;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s [--root DIR] [--config FILE] [--state DIR] [--log FILE] CMD\n"
		"commands: daemon once status pause resume reload shutdown version\n",
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
