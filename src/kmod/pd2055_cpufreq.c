#include <linux/cred.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>

/* 1 enforces max0/max4/max7. 0 leaves PEM alone. */
static bool takeover;
module_param(takeover, bool, 0644);
MODULE_PARM_DESC(takeover, "1 enforces max0/max4/max7, 0 leaves PEM in control");

/* Log CPUFREQ_ADJUST writers without changing the policy. */
static bool probe;
module_param(probe, bool, 0644);
MODULE_PARM_DESC(probe, "log CPUFREQ_ADJUST writers");

/* Target ceilings in kHz. 0 leaves the cluster alone. */
static uint max0;
module_param(max0, uint, 0644);
MODULE_PARM_DESC(max0, "policy0 ceiling in kHz");

static uint max4;
module_param(max4, uint, 0644);
MODULE_PARM_DESC(max4, "policy4 ceiling in kHz");

static uint max7;
module_param(max7, uint, 0644);
MODULE_PARM_DESC(max7, "policy7 ceiling in kHz");

/* Above this temperature the notifier only lowers max, never raises it. */
static int safety_temp_mc = 90000;
module_param(safety_temp_mc, int, 0644);
MODULE_PARM_DESC(safety_temp_mc, "stop raising max at this temperature in mC");

/* Updated by temp_work_fn(). Read-only from sysfs. */
static int temp_mc = -1000;
module_param(temp_mc, int, 0444);
MODULE_PARM_DESC(temp_mc, "cached thermal zone temperature in mC");

/* Optional zone name. Empty means try the built-in list. */
static char *zone_name;
module_param(zone_name, charp, 0444);
MODULE_PARM_DESC(zone_name, "thermal zone name, for example skin-therm");

/* Zone selected at load time. Read-only from sysfs. */
static char zone_used[32] = "none";
module_param_string(zone_used, zone_used, sizeof(zone_used), 0444);
MODULE_PARM_DESC(zone_used, "thermal zone selected at load");

static struct thermal_zone_device *zone;
static struct delayed_work temp_work;

static unsigned int cluster_target(const struct cpufreq_policy *p)
{
	unsigned int cpu = p->cpu;

	if (cpu < 4)
		return READ_ONCE(max0);
	if (cpu < 7)
		return READ_ONCE(max4);
	return READ_ONCE(max7);
}

static void log_adjust(const struct cpufreq_policy *p)
{
	pr_info_ratelimited("pd2055_cpufreq: adjust cpu=%u comm=%s pid=%d uid=%u min=%u max=%u user_max=%u take=%d temp=%d\n",
			    p->cpu, current->comm, current->pid,
			    current_uid().val, p->min, p->max,
			    p->user_policy.max, READ_ONCE(takeover),
			    READ_ONCE(temp_mc));
}

static int pd_adjust(struct notifier_block *nb, unsigned long event, void *data)
{
	struct cpufreq_policy *p = data;
	unsigned int target;
	int temp;

	if (event != CPUFREQ_ADJUST)
		return NOTIFY_DONE;

	if (READ_ONCE(probe))
		log_adjust(p);

	if (!READ_ONCE(takeover))
		return NOTIFY_OK;

	if (!p->cpuinfo.max_freq)
		return NOTIFY_OK;

	target = cluster_target(p);
	if (!target)
		return NOTIFY_OK;

	if (target < p->cpuinfo.min_freq)
		target = p->cpuinfo.min_freq;
	if (target > p->cpuinfo.max_freq)
		target = p->cpuinfo.max_freq;

	temp = READ_ONCE(temp_mc);
	if (temp >= READ_ONCE(safety_temp_mc)) {
		if (p->max > target)
			p->max = target;
	} else {
		p->max = target;
		if (p->min > target)
			p->min = target;
	}

	return NOTIFY_OK;
}

static struct notifier_block pd_nb = {
	.notifier_call = pd_adjust,
};

#define TEMP_ACTIVE_MS 5000
#define TEMP_IDLE_MS 30000

static void temp_sample(void)
{
	int temp;

	if (zone && thermal_zone_get_temp(zone, &temp) == 0)
		WRITE_ONCE(temp_mc, temp);
}

static void temp_reschedule(void)
{
	unsigned long delay = READ_ONCE(takeover) ?
			      msecs_to_jiffies(TEMP_ACTIVE_MS) :
			      msecs_to_jiffies(TEMP_IDLE_MS);

	mod_delayed_work(system_wq, &temp_work, delay);
}

static void temp_work_fn(struct work_struct *work)
{
	unsigned long delay;

	if (READ_ONCE(takeover)) {
		temp_sample();
		delay = msecs_to_jiffies(TEMP_ACTIVE_MS);
	} else {
		delay = msecs_to_jiffies(TEMP_IDLE_MS);
	}

	schedule_delayed_work(&temp_work, delay);
}

static struct thermal_zone_device *find_zone(void)
{
	static const char *const names[] = {
		"skin-therm",
		"case_therm",
		"board_therm",
		"quiet_therm",
		"ap_ntc",
		"quiet-therm-adc",
	};
	struct thermal_zone_device *tz;
	long err;
	int i;

	if (zone_name && *zone_name) {
		tz = thermal_zone_get_zone_by_name(zone_name);
		if (!IS_ERR(tz)) {
			snprintf(zone_used, sizeof(zone_used), "%s", zone_name);
			return tz;
		}
		err = PTR_ERR(tz);
		pr_info("pd2055_cpufreq: zone %s failed: %ld\n",
			zone_name, err);
	}

	for (i = 0; i < ARRAY_SIZE(names); i++) {
		tz = thermal_zone_get_zone_by_name(names[i]);
		if (!IS_ERR(tz)) {
			snprintf(zone_used, sizeof(zone_used), "%s", names[i]);
			return tz;
		}
	}

	return NULL;
}

static int apply_set(const char *val, const struct kernel_param *kp)
{
	if (READ_ONCE(takeover))
		temp_sample();
	temp_reschedule();
	cpufreq_update_policy(0);
	cpufreq_update_policy(4);
	cpufreq_update_policy(7);
	return 0;
}

static const struct kernel_param_ops apply_ops = {
	.set = apply_set,
};

module_param_cb(apply, &apply_ops, NULL, 0200);
MODULE_PARM_DESC(apply, "write to re-evaluate the three policies");

static int __init pd2055_cpufreq_init(void)
{
	int rc;

	zone = find_zone();
	if (zone)
		pr_info("pd2055_cpufreq: thermal zone %s\n", zone_used);
	else
		pr_info("pd2055_cpufreq: no thermal zone, temperature guard disabled\n");

	INIT_DELAYED_WORK(&temp_work, temp_work_fn);
	if (READ_ONCE(takeover))
		temp_sample();
	schedule_delayed_work(&temp_work,
			      msecs_to_jiffies(READ_ONCE(takeover) ?
					       TEMP_ACTIVE_MS : TEMP_IDLE_MS));

	rc = cpufreq_register_notifier(&pd_nb, CPUFREQ_POLICY_NOTIFIER);
	if (rc) {
		cancel_delayed_work_sync(&temp_work);
		pr_info("pd2055_cpufreq: notifier register failed %d\n", rc);
		return rc;
	}

	pr_info("pd2055_cpufreq: loaded takeover=%d probe=%d\n",
		READ_ONCE(takeover), READ_ONCE(probe));
	return 0;
}

static void __exit pd2055_cpufreq_exit(void)
{
	cpufreq_unregister_notifier(&pd_nb, CPUFREQ_POLICY_NOTIFIER);
	cancel_delayed_work_sync(&temp_work);
	pr_info("pd2055_cpufreq: unloaded\n");
}

module_init(pd2055_cpufreq_init);
module_exit(pd2055_cpufreq_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("PD2055 cpufreq policy guard");
