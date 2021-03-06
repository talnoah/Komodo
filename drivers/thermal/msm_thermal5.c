/* Copyright (c) 2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cpufreq.h>
#include <linux/mutex.h>
#include <linux/msm_tsens.h>
#include <linux/workqueue.h>
#include <linux/cpu.h>

#define DEF_TEMP_SENSOR 0
#define DEF_THERMAL_CHECK_MS 1250
#define DEF_ALLOWED_MAX_HIGH 70
#define DEF_ALLOWED_MAX_FREQ 810000

static int enabled;
static int allowed_max_high = DEF_ALLOWED_MAX_HIGH;
static int allowed_max_low = (DEF_ALLOWED_MAX_HIGH - 10);
static int allowed_max_freq = DEF_ALLOWED_MAX_FREQ;
static int check_interval_ms = DEF_THERMAL_CHECK_MS;
static int thermal_throttled = 0;
static int pre_throttled_max = 0;

module_param(allowed_max_high, int, 0);
module_param(allowed_max_freq, int, 0);
module_param(check_interval_ms, int, 0);

static struct delayed_work check_temp_work;

static int update_cpu_max_freq(struct cpufreq_policy *cpu_policy,
			       int cpu, int max_freq)
{
	int ret = 0;

	if (!cpu_policy)
		return -EINVAL;

	cpufreq_verify_within_limits(cpu_policy,
				cpu_policy->min, max_freq);
	cpu_policy->user_policy.max = max_freq;

	ret = cpufreq_update_policy(cpu);
	if (!ret)
		pr_info("msm_thermal: Limiting core%d max frequency to %d\n",
			cpu, max_freq);

	return ret;
}

static void check_temp(struct work_struct *work)
{
	struct cpufreq_policy *cpu_policy = NULL;
	struct tsens_device tsens_dev;
	unsigned long temp = 0;
	unsigned int max_freq = 0;
	int update_policy = 0;
	int cpu = 0;
	int ret = 0;

	tsens_dev.sensor_num = DEF_TEMP_SENSOR;
	ret = tsens_get_temp(&tsens_dev, &temp);
	if (ret) {
		pr_debug("msm_thermal: Unable to read TSENS sensor %d\n",
				tsens_dev.sensor_num);
		goto reschedule;
	} else
		pr_info("msm_thermal: TSENS sensor %d (%ld C)\n",
				tsens_dev.sensor_num, temp);

	for_each_possible_cpu(cpu) {
		update_policy = 0;
		cpu_policy = cpufreq_cpu_get(cpu);
		if (!cpu_policy) {
			pr_debug("msm_thermal: NULL policy on cpu %d\n", cpu);
			continue;
		}
		if (temp >= allowed_max_high) {
			if (cpu_policy->max > allowed_max_freq) {
				update_policy = 1;
				/* save pre-throttled max freq value */
				pre_throttled_max = cpu_policy->max;
				max_freq = allowed_max_freq;
	thermal_throttled = 1;
pr_warn("Thermal Throttled! Set max freq to: \
%u\n", max_freq);
			} else {
				pr_debug("msm_thermal: policy max for cpu %d "
					 "already < allowed_max_freq\n", cpu);
			}
		} else if (temp < allowed_max_low && thermal_throttled) {
			if (cpu_policy->max < cpu_policy->cpuinfo.max_freq) {
					if (pre_throttled_max != 0)
max_freq = pre_throttled_max;
else
max_freq = cpu_policy->
cpuinfo.max_freq;
				update_policy = 1;
	/* wait until 2nd core is unthrottled */
if (cpu == 1)
thermal_throttled = 0;
pr_warn("Thermal Throttling Ended! restore \
max freq to: %u\n", max_freq);
			} else {
				pr_debug("msm_thermal: policy max for cpu %d "
					 "already at max allowed\n", cpu);
			}
		}

		if (update_policy)
			update_cpu_max_freq(cpu_policy, cpu, max_freq);

		cpufreq_cpu_put(cpu_policy);
	}

reschedule:
	if (enabled)
		schedule_delayed_work(&check_temp_work,
				msecs_to_jiffies(check_interval_ms));
}

static void disable_msm_thermal(void)
{
	int cpu = 0;
	struct cpufreq_policy *cpu_policy = NULL;

	/* make sure check_temp is no longer running */
		cancel_delayed_work(&check_temp_work);
		flush_scheduled_work();

	for_each_possible_cpu(cpu) {
		cpu_policy = cpufreq_cpu_get(cpu);
		if (cpu_policy) {
			if (cpu_policy->max < cpu_policy->cpuinfo.max_freq)
				update_cpu_max_freq(cpu_policy, cpu,
						    cpu_policy->
						    cpuinfo.max_freq);
			cpufreq_cpu_put(cpu_policy);
		}
	}
}

static int set_enabled(const char *val, const struct kernel_param *kp)
{
	int ret = 0;

	ret = param_set_bool(val, kp);
	if (!enabled)
		disable_msm_thermal();
	else
		pr_info("msm_thermal: no action for enabled = %d\n", enabled);

	pr_info("msm_thermal: enabled = %d\n", enabled);

	return ret;
}

static struct kernel_param_ops module_ops = {
	.set = set_enabled,
	.get = param_get_bool,
};

module_param_cb(enabled, &module_ops, &enabled, 0644);
MODULE_PARM_DESC(enabled, "enforce thermal limit on cpu");

static int __init msm_thermal_init(void)
{
	int ret = 0;

	enabled = 1;
	INIT_DELAYED_WORK(&check_temp_work, check_temp);

	schedule_delayed_work(&check_temp_work, 0);

	return ret;
}
fs_initcall(msm_thermal_init);

