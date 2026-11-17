/* PD2055 battery drain.
 * Runs as shell: CPU and GPU load.
 * Runs as root: also performance governor, display, radios, torch, wakelock.
 * Stops when the battery temperature reaches BATTERY_TEMP_STOP.
 * The value is in 0.1 C units and defaults to 460.
 * usage: drain [seconds]
 */

typedef unsigned long u64;

#define AT_FDCWD (-100L)

#define O_RDONLY 0
#define O_WRONLY 1
#define O_CLOEXEC 0x80000
#define O_NONBLOCK 0x800

#define SYS_DUP3 24
#define SYS_OPENAT 56
#define SYS_CLOSE 57
#define SYS_PIPE2 59
#define SYS_READ 63
#define SYS_WRITE 64
#define SYS_SIGNALFD4 74
#define SYS_EXIT_GROUP 94
#define SYS_NANOSLEEP 101
#define SYS_CLOCK_GETTIME 113
#define SYS_SCHED_SETAFFINITY 122
#define SYS_KILL 129
#define SYS_RT_SIGPROCMASK 135
#define SYS_PRCTL 167
#define SYS_GETUID 174
#define SYS_CLONE 220
#define SYS_EXECVE 221
#define SYS_WAIT4 260

#define SIG_BLOCK 0
#define SIGKILL 9
#define SIGCHLD 17
#define WNOHANG 1
#define CLOCK_MONOTONIC 1
#define PR_SET_PDEATHSIG 1

#define BATTERY "/sys/class/power_supply/battery/"
#define USB_ONLINE "/sys/class/power_supply/usb/online"
#define WAKE_LOCK "/sys/power/wake_lock"
#define WAKE_UNLOCK "/sys/power/wake_unlock"
#define TORCH "/sys/class/leds/led:torch_0/brightness"
#define TORCH_MAX "/sys/class/leds/led:torch_0/max_brightness"
#define SWITCH "/sys/class/leds/led:switch_0/brightness"
#define SWITCH_MAX "/sys/class/leds/led:switch_0/max_brightness"
#define BACKLIGHT_MAX "/sys/class/backlight/panel0-backlight/max_brightness"
#define CPU_ONLINE "/sys/devices/system/cpu/online"

#define MAX_CPU 16

/* Battery temperature stop in 0.1 C. */
#define BATTERY_TEMP_STOP_DC 460

struct ts {
	long sec;
	long nsec;
};

static inline long sys6(long n, long a, long b, long c, long d, long e, long f)
{
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	register long x5 __asm__("x5") = f;
	register long x8 __asm__("x8") = n;

	__asm__ volatile("svc #0"
			 : "+r"(x0)
			 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5),
			   "r"(x8)
			 : "memory");
	return x0;
}

static void x_exit(int code)
{
	sys6(SYS_EXIT_GROUP, code, 0, 0, 0, 0, 0);
	for (;;)
		;
}

static int s_len(const char *s)
{
	int n = 0;

	while (s[n])
		n++;
	return n;
}

static int s_eq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static char *s_app(char *d, const char *s)
{
	while (*s)
		*d++ = *s++;
	return d;
}

static char *s_cpy(char *d, const char *s)
{
	while (*s)
		*d++ = *s++;
	*d = 0;
	return d;
}

static char *u_app(char *d, unsigned long v)
{
	char t[24];
	int i = 0;

	if (v == 0) {
		*d++ = '0';
		return d;
	}
	while (v) {
		t[i++] = '0' + (v % 10);
		v /= 10;
	}
	while (i)
		*d++ = t[--i];
	return d;
}

static char *u_app2(char *d, unsigned long v)
{
	*d++ = '0' + (v / 10) % 10;
	*d++ = '0' + v % 10;
	return d;
}

static long s_num(const char *s, int *ok)
{
	long v = 0;
	int neg = 0;

	*ok = 1;
	if (*s == '-') {
		neg = 1;
		s++;
	}
	if (*s < '0' || *s > '9') {
		*ok = 0;
		return 0;
	}
	while (*s >= '0' && *s <= '9')
		v = v * 10 + (*s++ - '0');
	return neg ? -v : v;
}

static void s_trim(char *s)
{
	int n = s_len(s);

	while (n > 0) {
		char c = s[n - 1];

		if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
			s[--n] = 0;
		else
			break;
	}
}

static int read_file(const char *path, char *buf, int cap)
{
	long fd;
	int n = 0;

	fd = sys6(SYS_OPENAT, AT_FDCWD, (long)path, O_RDONLY, 0, 0, 0);
	if (fd < 0) {
		if (cap > 0)
			buf[0] = 0;
		return -1;
	}
	while (n < cap - 1) {
		long r = sys6(SYS_READ, fd, (long)(buf + n), cap - 1 - n,
			      0, 0, 0);

		if (r <= 0)
			break;
		n += (int)r;
	}
	sys6(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
	buf[n] = 0;
	s_trim(buf);
	return n;
}

static long read_battery_temp_dc(void)
{
	char buf[16];
	int ok;

	if (read_file(BATTERY "temp", buf, sizeof buf) < 0)
		return 0;
	return s_num(buf, &ok);
}

static int write_file(const char *path, const char *val)
{
	long fd, r;
	int len;

	fd = sys6(SYS_OPENAT, AT_FDCWD, (long)path, O_WRONLY, 0, 0, 0);
	if (fd < 0)
		return -1;
	len = s_len(val);
	r = sys6(SYS_WRITE, fd, (long)val, len, 0, 0, 0);
	sys6(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
	return r == len ? 0 : -1;
}

static void out(const char *s)
{
	sys6(SYS_WRITE, 1, (long)s, s_len(s), 0, 0, 0);
}

static void err(const char *s)
{
	sys6(SYS_WRITE, 2, (long)s, s_len(s), 0, 0, 0);
}

static char **g_envp;

static int sig_fd = -1;
static unsigned long sig_mask = (1UL << 0) | (1UL << 1) | (1UL << 14);

static int is_root;
static int cleaned;

static int cpus[MAX_CPU];
static int ncpu;

static char gov_path[MAX_CPU][80];
static char gov_val[MAX_CPU][32];
static int ngov;

static long load_pid[MAX_CPU];
static int nload;
static long gpu_pid;
static long ping_pid;
static long cmd_pid;

static int have_torch;
static int have_switch;
static char torch_orig[16];
static char switch_orig[16];

static char scr_bright[24];
static char scr_mode[8];
static char scr_timeout[24];
static char wifi_was[16];
static char bt_was[16];

static const char *env_get(const char *name)
{
	int n = s_len(name);
	char **e;

	if (!g_envp)
		return 0;
	for (e = g_envp; *e; e++) {
		int i = 0;

		while (i < n && (*e)[i] == name[i])
			i++;
		if (i == n && (*e)[n] == '=')
			return *e + n + 1;
	}
	return 0;
}

static long do_fork(void)
{
	return sys6(SYS_CLONE, SIGCHLD, 0, 0, 0, 0, 0);
}

static long spawn_cmd(char *const *argv, int out_fd, int quiet)
{
	char pathbuf[64];
	const char *path = argv[0];
	long pid;

	if (path[0] != '/') {
		char *p = s_app(pathbuf, "/system/bin/");

		s_cpy(p, path);
		path = pathbuf;
	}
	pid = do_fork();
	if (pid == 0) {
		sys6(SYS_PRCTL, PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0, 0);
		if (out_fd >= 0)
			sys6(SYS_DUP3, out_fd, 1, 0, 0, 0, 0);
		if (quiet) {
			long fd = sys6(SYS_OPENAT, AT_FDCWD, (long)"/dev/null",
				       O_WRONLY, 0, 0, 0);

			if (fd >= 0) {
				sys6(SYS_DUP3, fd, 1, 0, 0, 0, 0);
				sys6(SYS_DUP3, fd, 2, 0, 0, 0, 0);
			}
		}
		sys6(SYS_EXECVE, (long)path, (long)argv, (long)g_envp,
		     0, 0, 0);
		x_exit(127);
	}
	return pid;
}

static void reap(long pid)
{
	int st;

	sys6(SYS_WAIT4, pid, (long)&st, 0, 0, 0, 0);
}

static int run_cmd(char *const *argv)
{
	long pid = spawn_cmd(argv, -1, 1);

	if (pid <= 0)
		return -1;
	cmd_pid = pid;
	reap(pid);
	cmd_pid = 0;
	return 0;
}

static int run_capture(char *const *argv, char *outbuf, int cap)
{
	int fds[2];
	long pid, r;
	int n = 0;

	if (sys6(SYS_PIPE2, (long)fds, O_CLOEXEC, 0, 0, 0, 0) < 0)
		return -1;
	pid = spawn_cmd(argv, fds[1], 0);
	if (pid <= 0) {
		sys6(SYS_CLOSE, fds[0], 0, 0, 0, 0, 0);
		sys6(SYS_CLOSE, fds[1], 0, 0, 0, 0, 0);
		return -1;
	}
	sys6(SYS_CLOSE, fds[1], 0, 0, 0, 0, 0);
	while (n < cap - 1) {
		r = sys6(SYS_READ, fds[0], (long)(outbuf + n), cap - 1 - n,
			 0, 0, 0);
		if (r <= 0)
			break;
		n += (int)r;
	}
	sys6(SYS_CLOSE, fds[0], 0, 0, 0, 0, 0);
	outbuf[n] = 0;
	cmd_pid = pid;
	reap(pid);
	cmd_pid = 0;
	s_trim(outbuf);
	return 0;
}

static int setup_signals(void)
{
	long r;

	r = sys6(SYS_RT_SIGPROCMASK, SIG_BLOCK, (long)&sig_mask, 0, 8,
		 0, 0);
	if (r < 0)
		return -1;
	sig_fd = (int)sys6(SYS_SIGNALFD4, -1, (long)&sig_mask, 8,
			   O_NONBLOCK | O_CLOEXEC, 0, 0);
	return sig_fd < 0 ? -1 : 0;
}

static int stop_requested(void)
{
	unsigned char buf[128];
	long r;

	if (sig_fd < 0)
		return 0;
	r = sys6(SYS_READ, sig_fd, (long)buf, sizeof buf, 0, 0, 0);
	return r >= 128;
}

static unsigned long now_ms(void)
{
	struct ts t;

	sys6(SYS_CLOCK_GETTIME, CLOCK_MONOTONIC, (long)&t, 0, 0, 0, 0);
	return (unsigned long)t.sec * 1000UL +
	       (unsigned long)(t.nsec / 1000000L);
}

static void sleep_ms(long ms)
{
	struct ts t;

	t.sec = ms / 1000;
	t.nsec = (ms % 1000) * 1000000L;
	sys6(SYS_NANOSLEEP, (long)&t, 0, 0, 0, 0, 0);
}

static void read_cpus(void)
{
	char buf[64];
	const char *p;

	if (read_file(CPU_ONLINE, buf, sizeof buf) < 0)
		s_cpy(buf, "0");
	p = buf;
	while (*p) {
		int a = 0, b, c;

		while (*p == ',' || *p == ' ')
			p++;
		if (*p < '0' || *p > '9')
			break;
		while (*p >= '0' && *p <= '9')
			a = a * 10 + (*p++ - '0');
		b = a;
		if (*p == '-') {
			p++;
			b = 0;
			while (*p >= '0' && *p <= '9')
				b = b * 10 + (*p++ - '0');
		}
		for (c = a; c <= b && ncpu < MAX_CPU; c++)
			cpus[ncpu++] = c;
	}
	if (ncpu == 0)
		cpus[ncpu++] = 0;
}

static void cpu_path(char *buf, int cpu, const char *suffix)
{
	char *p = buf;

	p = s_app(p, "/sys/devices/system/cpu/cpu");
	p = u_app(p, (unsigned long)cpu);
	p = s_app(p, suffix);
	*p = 0;
}

static void save_governors(void)
{
	int i;

	for (i = 0; i < ncpu; i++) {
		char path[80];

		cpu_path(path, cpus[i], "/cpufreq/scaling_governor");
		if (read_file(path, gov_val[ngov], sizeof gov_val[ngov]) < 0)
			continue;
		s_cpy(gov_path[ngov], path);
		ngov++;
	}
}

static void restore_governors(void)
{
	int i;

	for (i = 0; i < ngov; i++)
		write_file(gov_path[i], gov_val[i]);
}

/* EGL and GLES from the device. */
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLContext;
typedef void *EGLSurface;
typedef unsigned int GLuint;
typedef int GLint;
typedef unsigned int GLenum;

extern EGLDisplay eglGetDisplay(unsigned);
extern int eglInitialize(EGLDisplay, int *, int *);
extern int eglChooseConfig(EGLDisplay, const int *, EGLConfig *, int, int *);
extern EGLSurface eglCreatePbufferSurface(EGLDisplay, EGLConfig,
					  const int *);
extern EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext,
				   const int *);
extern int eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext);

extern void glViewport(int, int, int, int);
extern void glClearColor(float, float, float, float);
extern void glClear(unsigned);
extern void glFinish(void);
extern GLuint glCreateShader(GLenum);
extern void glShaderSource(GLuint, int, const char *const *, const int *);
extern void glCompileShader(GLuint);
extern void glGetShaderiv(GLuint, GLenum, GLint *);
extern GLuint glCreateProgram(void);
extern void glAttachShader(GLuint, GLuint);
extern void glLinkProgram(GLuint);
extern void glGetProgramiv(GLuint, GLenum, GLint *);
extern void glUseProgram(GLuint);
extern GLint glGetAttribLocation(GLuint, const char *);
extern void glEnableVertexAttribArray(GLuint);
extern void glVertexAttribPointer(GLuint, int, GLenum, unsigned char, int,
				  const void *);
extern void glDrawArrays(GLenum, int, int);

#define EGL_SURFACE_TYPE 0x3033
#define EGL_PBUFFER_BIT 0x0001
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_NONE 0x3038
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_CONTEXT_CLIENT_VERSION 0x3098

#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_TRIANGLE_STRIP 0x0005
#define GL_FLOAT 0x1406
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82

static const char vs_src[] =
	"attribute vec2 a;\n"
	"varying vec2 v;\n"
	"void main(){ v = a; gl_Position = vec4(a, 0.0, 1.0); }\n";

static const char fs_src[] =
	"precision highp float;\n"
	"varying vec2 v;\n"
	"void main(){\n"
	"  float x = v.x * 10.0;\n"
	"  float y = v.y * 10.0;\n"
	"  for (int i = 0; i < 512; i++) {\n"
	"    x = sin(x * 1.0001 + y * 0.5);\n"
	"    y = cos(y * 1.0001 + x * 0.5);\n"
	"  }\n"
	"  gl_FragColor = vec4(x, y, x + y, 1.0);\n"
	"}\n";

static GLuint compile_shader(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;

	glShaderSource(s, 1, &src, 0);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	return ok ? s : 0;
}

static void gpu_main(void)
{
	static const float verts[] = {
		-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f
	};
	int maj = 0, min = 0, n = 0;
	int cfg_attr[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
		EGL_NONE
	};
	int pb_attr[] = { EGL_WIDTH, 1920, EGL_HEIGHT, 1080, EGL_NONE };
	int ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLDisplay d;
	EGLConfig cfg;
	EGLSurface surf;
	EGLContext ctx;
	GLuint vs, fs, prog;
	GLint loc;
	float t = 0.0f;

	d = eglGetDisplay(0);
	if (!d || !eglInitialize(d, &maj, &min))
		x_exit(1);
	if (!eglChooseConfig(d, cfg_attr, &cfg, 1, &n) || n < 1)
		x_exit(2);
	surf = eglCreatePbufferSurface(d, cfg, pb_attr);
	if (!surf)
		x_exit(3);
	ctx = eglCreateContext(d, cfg, 0, ctx_attr);
	if (!ctx)
		x_exit(4);
	if (!eglMakeCurrent(d, surf, surf, ctx))
		x_exit(5);

	glViewport(0, 0, 1920, 1080);
	vs = compile_shader(GL_VERTEX_SHADER, vs_src);
	fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
	if (!vs || !fs)
		x_exit(6);
	prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);
	{
		GLint ok = 0;

		glGetProgramiv(prog, GL_LINK_STATUS, &ok);
		if (!ok)
			x_exit(7);
	}
	glUseProgram(prog);
	loc = glGetAttribLocation(prog, "a");
	if (loc < 0)
		x_exit(8);
	glEnableVertexAttribArray((GLuint)loc);
	glVertexAttribPointer((GLuint)loc, 2, GL_FLOAT, 0, 0, verts);

	for (;;) {
		t += 0.001f;
		if (t > 1.0f)
			t = 0.0f;
		glClearColor(t, 0.2f, 0.3f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glFinish();
	}
}

static int start_gpu(void)
{
	long pid = do_fork();

	if (pid == 0) {
		sys6(SYS_PRCTL, PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0, 0);
		gpu_main();
		for (;;)
			;
	}
	if (pid <= 0)
		return -1;
	gpu_pid = pid;
	sleep_ms(1000);
	{
		int st;
		long r = sys6(SYS_WAIT4, pid, (long)&st, WNOHANG, 0, 0, 0);

		if (r == pid) {
			gpu_pid = 0;
			return -1;
		}
	}
	return 0;
}

static void start_loads(void)
{
	int i;

	for (i = 0; i < ncpu; i++) {
		char path[80];
		long pid;

		if (is_root) {
			cpu_path(path, cpus[i], "/cpufreq/scaling_governor");
			write_file(path, "performance");
		}
		pid = do_fork();
		if (pid == 0) {
			unsigned long mask = 1UL << cpus[i];
			volatile unsigned long x = 1;

			sys6(SYS_PRCTL, PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0, 0);
			sys6(SYS_SCHED_SETAFFINITY, 0, 8, (long)&mask, 0, 0, 0);
			for (;;)
				x = x * 3 + 1;
		}
		if (pid > 0)
			load_pid[nload++] = pid;
	}
}

static void root_setup(int use_torch)
{
	char maxbuf[24];

	run_capture((char *const[]){"settings", "get", "system",
				    "screen_brightness", 0},
		    scr_bright, sizeof scr_bright);
	run_capture((char *const[]){"settings", "get", "system",
				    "screen_brightness_mode", 0},
		    scr_mode, sizeof scr_mode);
	run_capture((char *const[]){"settings", "get", "system",
				    "screen_off_timeout", 0},
		    scr_timeout, sizeof scr_timeout);

	if (read_file(BACKLIGHT_MAX, maxbuf, sizeof maxbuf) < 0 ||
	    maxbuf[0] < '0' || maxbuf[0] > '9')
		s_cpy(maxbuf, "2047");

	run_cmd((char *const[]){"settings", "put", "system",
				"screen_brightness_mode", "0", 0});
	run_cmd((char *const[]){"settings", "put", "system",
				"screen_brightness", maxbuf, 0});
	run_cmd((char *const[]){"settings", "put", "system",
				"screen_off_timeout", "2147483647", 0});
	run_cmd((char *const[]){"input", "keyevent", "224", 0});

	write_file(WAKE_LOCK, "pd2055-drain");

	if (use_torch) {
		char v[16], sm[16];

		if (read_file(TORCH, v, sizeof v) >= 0) {
			s_cpy(torch_orig, v);
			have_torch = 1;
		}
		if (read_file(TORCH_MAX, v, sizeof v) < 0 ||
		    v[0] < '0' || v[0] > '9')
			s_cpy(v, "500");
		write_file(TORCH, v);

		if (read_file(SWITCH, v, sizeof v) >= 0) {
			s_cpy(switch_orig, v);
			have_switch = 1;
		}
		if (read_file(SWITCH_MAX, sm, sizeof sm) < 0 ||
		    sm[0] < '0' || sm[0] > '9')
			s_cpy(sm, "255");
		if (have_switch)
			write_file(SWITCH, sm);
	}

	run_capture((char *const[]){"settings", "get", "global", "wifi_on",
				    0},
		    wifi_was, sizeof wifi_was);
	run_capture((char *const[]){"settings", "get", "global",
				    "bluetooth_on", 0},
		    bt_was, sizeof bt_was);
	if (!s_eq(wifi_was, "1"))
		run_cmd((char *const[]){"svc", "wifi", "enable", 0});
	if (!s_eq(bt_was, "1"))
		run_cmd((char *const[]){"svc", "bluetooth", "enable", 0});

	ping_pid = spawn_cmd((char *const[]){"ping", "-i", "0.2", "-s",
					     "1400", "1.1.1.1", 0},
			     -1, 1);
	if (ping_pid <= 0)
		ping_pid = 0;

	{
		char on[8];

		if (read_file(USB_ONLINE, on, sizeof on) >= 0 &&
		    s_eq(on, "1"))
			err("drain: usb online; unplug to drain\n");
	}
}

static void print_status(void)
{
	char st[32], cap[16], tmp[16], volt[16], curr[16];
	long capacity = 0, temp = 0, uv = 0, ua = 0;
	long mw, ua_abs;
	int ok;
	char line[192];
	char *p = line;

	if (read_file(BATTERY "status", st, sizeof st) < 0 || !st[0])
		s_cpy(st, "unknown");
	if (read_file(BATTERY "capacity", cap, sizeof cap) >= 0) {
		capacity = s_num(cap, &ok);
		if (!ok)
			capacity = 0;
	}
	if (read_file(BATTERY "temp", tmp, sizeof tmp) >= 0) {
		temp = s_num(tmp, &ok);
		if (!ok)
			temp = 0;
	}
	if (read_file(BATTERY "voltage_now", volt, sizeof volt) >= 0) {
		uv = s_num(volt, &ok);
		if (!ok)
			uv = 0;
	}
	if (read_file(BATTERY "current_now", curr, sizeof curr) >= 0) {
		ua = s_num(curr, &ok);
		if (!ok)
			ua = 0;
	}
	ua_abs = ua < 0 ? -ua : ua;
	mw = (uv / 1000) * (ua_abs / 1000) / 1000;

	p = s_app(p, "drain: ");
	p = s_app(p, st);
	p = s_app(p, " ");
	p = u_app(p, (unsigned long)capacity);
	p = s_app(p, "% ");
	p = u_app(p, (unsigned long)(temp / 10));
	p = s_app(p, ".");
	p = u_app(p, (unsigned long)(temp % 10));
	p = s_app(p, "C ");
	p = u_app(p, (unsigned long)(uv / 1000000));
	p = s_app(p, ".");
	p = u_app2(p, (unsigned long)((uv % 1000000) / 10000));
	p = s_app(p, "V ");
	p = u_app(p, (unsigned long)(ua_abs / 1000));
	p = s_app(p, "mA ");
	p = u_app(p, (unsigned long)mw);
	p = s_app(p, "mW\n");
	sys6(SYS_WRITE, 1, (long)line, p - line, 0, 0, 0);
}

static void cleanup(void)
{
	int i;

	if (cleaned)
		return;
	cleaned = 1;

	for (i = 0; i < nload; i++)
		if (load_pid[i] > 0)
			sys6(SYS_KILL, load_pid[i], SIGKILL, 0, 0, 0, 0);
	if (gpu_pid > 0)
		sys6(SYS_KILL, gpu_pid, SIGKILL, 0, 0, 0, 0);
	if (ping_pid > 0)
		sys6(SYS_KILL, ping_pid, SIGKILL, 0, 0, 0, 0);
	if (cmd_pid > 0)
		sys6(SYS_KILL, cmd_pid, SIGKILL, 0, 0, 0, 0);

	for (i = 0; i < nload; i++)
		if (load_pid[i] > 0)
			reap(load_pid[i]);
	if (gpu_pid > 0)
		reap(gpu_pid);
	if (ping_pid > 0)
		reap(ping_pid);
	if (cmd_pid > 0)
		reap(cmd_pid);

	if (is_root) {
		if (have_switch)
			write_file(SWITCH, switch_orig);
		if (have_torch)
			write_file(TORCH, torch_orig);

		restore_governors();

		if (scr_bright[0])
			run_cmd((char *const[]){"settings", "put", "system",
						"screen_brightness",
						scr_bright, 0});
		if (scr_mode[0])
			run_cmd((char *const[]){"settings", "put", "system",
						"screen_brightness_mode",
						scr_mode, 0});
		if (scr_timeout[0])
			run_cmd((char *const[]){"settings", "put", "system",
						"screen_off_timeout",
						scr_timeout, 0});

		if (s_eq(wifi_was, "0"))
			run_cmd((char *const[]){"svc", "wifi", "disable",
						0});
		if (s_eq(bt_was, "0"))
			run_cmd((char *const[]){"svc", "bluetooth",
						"disable", 0});

		write_file(WAKE_UNLOCK, "pd2055-drain");
	}

	out("drain: stopped\n");
}

static int parse_arg(const char *s, long *val)
{
	long v = 0;

	if (!*s)
		return -1;
	while (*s) {
		if (*s < '0' || *s > '9')
			return -1;
		v = v * 10 + (*s++ - '0');
		if (v < 0)
			return -1;
	}
	*val = v;
	return 0;
}

static void run_main(int argc, char **argv)
{
	long duration = 0;
	long temp_stop = BATTERY_TEMP_STOP_DC;
	int use_gpu = 1;
	int use_torch = 1;
	int gpu_state = 0;
	const char *v;
	unsigned long start, end = 0, next_status;

	if (argc > 1 && parse_arg(argv[1], &duration) < 0) {
		err("usage: drain [seconds]\n");
		x_exit(2);
	}
	v = env_get("GPU");
	if (v && s_eq(v, "0"))
		use_gpu = 0;
	v = env_get("TORCH");
	if (v && s_eq(v, "0"))
		use_torch = 0;
	v = env_get("BATTERY_TEMP_STOP");
	if (v && parse_arg(v, &temp_stop) < 0)
		temp_stop = BATTERY_TEMP_STOP_DC;

	is_root = sys6(SYS_GETUID, 0, 0, 0, 0, 0, 0) == 0;
	if (setup_signals() < 0) {
		err("drain: signal setup failed\n");
		x_exit(1);
	}

	read_cpus();
	if (is_root)
		save_governors();

	start_loads();
	if (use_gpu) {
		if (start_gpu() < 0) {
			gpu_state = -1;
			err("gpu: failed to start\n");
		} else {
			gpu_state = 1;
		}
	}

	if (is_root)
		root_setup(use_torch);

	{
		char line[128];
		char *p = line;

		p = s_app(p, "drain: start; ");
		p = u_app(p, (unsigned long)nload);
		p = s_app(p, " cpus; gpu ");
		p = s_app(p, gpu_state == 1 ? "on" :
			     (gpu_state < 0 ? "failed" : "off"));
		p = s_app(p, is_root ?
			     "; root features on; ctrl-c to stop\n" :
			     "; no root; ctrl-c to stop\n");
		sys6(SYS_WRITE, 1, (long)line, p - line, 0, 0, 0);
	}

	start = now_ms();
	if (duration > 0)
		end = start + (unsigned long)duration * 1000UL;
	next_status = start;

	for (;;) {
		unsigned long now;

		if (stop_requested())
			break;
		now = now_ms();
		if (is_root) {
			long t = read_battery_temp_dc();

			if (t > 0 && t >= temp_stop) {
				char line[80];
				char *p = line;

				p = s_app(p, "drain: battery ");
				p = u_app(p, (unsigned long)(t / 10));
				p = s_app(p, ".");
				p = u_app(p, (unsigned long)(t % 10));
				p = s_app(p, "C; stop at ");
				p = u_app(p, (unsigned long)(temp_stop / 10));
				p = s_app(p, ".");
				p = u_app(p, (unsigned long)(temp_stop % 10));
				p = s_app(p, "C\n");
				sys6(SYS_WRITE, 1, (long)line, p - line, 0, 0, 0);
				break;
			}
		}
		if (is_root && now >= next_status) {
			print_status();
			next_status = now + 10000;
		}
		if (end && now >= end)
			break;
		{
			long ms = 1000;

			if (end) {
				long left = (long)(end - now);

				if (left < ms)
					ms = left;
			}
			if (is_root) {
				long left = (long)(next_status - now);

				if (left < ms)
					ms = left;
			}
			if (ms < 1)
				ms = 1;
			sleep_ms(ms);
		}
	}
}

void drain_start(long *sp) __attribute__((noreturn, used));

__asm__(
	".text\n"
	".globl _start\n"
	"_start:\n"
	"mov x0, sp\n"
	"b drain_start\n");

void drain_start(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	char **envp = argv + argc + 1;

	g_envp = envp;
	run_main(argc, argv);
	cleanup();
	x_exit(0);
}
