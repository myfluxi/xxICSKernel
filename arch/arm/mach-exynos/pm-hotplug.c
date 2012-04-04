/* linux/arch/arm/mach-exynos/pm-hotplug.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * OLD S5PV310 - Dynamic CPU hotpluging
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/serial_core.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/cpu.h>
#include <linux/percpu.h>
#include <linux/ktime.h>
#include <linux/tick.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/suspend.h>
#include <linux/reboot.h>
#include <linux/earlysuspend.h>

#include <plat/map-base.h>
#include <plat/gpio-cfg.h>

#include <mach/regs-gpio.h>
#include <mach/regs-irq.h>
#include <linux/gpio.h>

#include <linux/device.h> 	//for second_core by tegrak
#include <linux/miscdevice.h> 	//for second_core by tegrak

#define CHECK_DELAY	(HZ >> 1)
#define TRANS_LOAD_L	25
#define TRANS_LOAD_H	70

#define HOTPLUG_UNLOCKED 0
#define HOTPLUG_LOCKED 1

static struct workqueue_struct *hotplug_wq;

static struct delayed_work hotplug_work;

static unsigned int hotpluging_rate = CHECK_DELAY;
module_param_named(rate, hotpluging_rate, uint, 0644);
static unsigned int user_lock;
module_param_named(lock, user_lock, uint, 0644);
static unsigned int trans_load_l = TRANS_LOAD_L;
module_param_named(loadl, trans_load_l, uint, 0644);
static unsigned int trans_load_h = TRANS_LOAD_H;
module_param_named(loadh, trans_load_h, uint, 0644);

struct cpu_time_info {
	cputime64_t prev_cpu_idle;
	cputime64_t prev_cpu_wall;
	unsigned int load;
};

static DEFINE_PER_CPU(struct cpu_time_info, hotplug_cpu_time);

static bool screen_off;

/* mutex can be used since hotplug_timer does not run in
   timer(softirq) context but in process context */
static DEFINE_MUTEX(hotplug_lock);

/* Second core values by tegrak */
#define SECOND_CORE_VERSION (1)
int second_core_on = 1;
int hotplug_on = 1;

static void hotplug_timer(struct work_struct *work)
{
	unsigned int i, avg_load = 0, load = 0;

	mutex_lock(&hotplug_lock);

	if (screen_off && !cpu_online(1)) {
		printk(KERN_INFO "pm-hotplug: disable cpu auto-hotplug\n");
		goto out;
	}

	if (!hotplug_on) {
		if (!second_core_on && cpu_online(1) == 1)
			cpu_down(1);
		goto out;
	}

	if (user_lock == 1)
		goto no_hotplug;

	for_each_online_cpu(i) {
		struct cpu_time_info *tmp_info;
		cputime64_t cur_wall_time, cur_idle_time;
		unsigned int idle_time, wall_time;

		tmp_info = &per_cpu(hotplug_cpu_time, i);

		cur_idle_time = get_cpu_idle_time_us(i, &cur_wall_time);

		idle_time = (unsigned int)cputime64_sub(cur_idle_time,
							tmp_info->prev_cpu_idle);
		tmp_info->prev_cpu_idle = cur_idle_time;

		wall_time = (unsigned int)cputime64_sub(cur_wall_time,
							tmp_info->prev_cpu_wall);
		tmp_info->prev_cpu_wall = cur_wall_time;

		if (wall_time < idle_time)
			goto no_hotplug;

		tmp_info->load = 100 * (wall_time - idle_time) / wall_time;

		load += tmp_info->load;
	}

	avg_load = load / num_online_cpus();

	if (avg_load < trans_load_l && cpu_online(1)) {
		printk("cpu1 turning off!\n");
		cpu_down(1);
		printk("cpu1 off end!\n");
		hotpluging_rate = CHECK_DELAY;
	} else if (avg_load > trans_load_h && !cpu_online(1)) {
		printk("cpu1 turning on!\n");
		cpu_up(1);
		printk("cpu1 on end!\n");
		hotpluging_rate = CHECK_DELAY * 4;
	}

no_hotplug:
	queue_delayed_work_on(0, hotplug_wq, &hotplug_work, hotpluging_rate);
out:
	mutex_unlock(&hotplug_lock);
}

static int exynos4_pm_hotplug_notifier_event(struct notifier_block *this,
					     unsigned long event, void *ptr)
{
	static unsigned user_lock_saved;

	switch (event) {
	case PM_SUSPEND_PREPARE:
		mutex_lock(&hotplug_lock);
		user_lock_saved = user_lock;
		user_lock = 1;
		pr_info("%s: saving pm_hotplug lock %x\n",
			__func__, user_lock_saved);
		mutex_unlock(&hotplug_lock);
		return NOTIFY_OK;
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		mutex_lock(&hotplug_lock);
		pr_info("%s: restoring pm_hotplug lock %x\n",
			__func__, user_lock_saved);
		user_lock = user_lock_saved;
		mutex_unlock(&hotplug_lock);
		return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}

static struct notifier_block exynos4_pm_hotplug_notifier = {
	.notifier_call = exynos4_pm_hotplug_notifier_event,
};

static int hotplug_reboot_notifier_call(struct notifier_block *this,
					unsigned long code, void *_cmd)
{
	mutex_lock(&hotplug_lock);
	pr_err("%s: disabling pm hotplug\n", __func__);
	user_lock = 1;
	mutex_unlock(&hotplug_lock);

	return NOTIFY_DONE;
}

static struct notifier_block hotplug_reboot_notifier = {
	.notifier_call = hotplug_reboot_notifier_call,
};

static void hotplug_early_suspend(struct early_suspend *handler)
{
	mutex_lock(&hotplug_lock);
	screen_off = true;
	mutex_unlock(&hotplug_lock);
}

static void hotplug_late_resume(struct early_suspend *handler)
{
	printk(KERN_INFO "pm-hotplug: enable cpu auto-hotplug\n");

	mutex_lock(&hotplug_lock);
	screen_off = false;
	queue_delayed_work_on(0, hotplug_wq, &hotplug_work, hotpluging_rate);
	mutex_unlock(&hotplug_lock);
}

static struct early_suspend hotplug_early_suspend_notifier = {
	.suspend = hotplug_early_suspend,
	.resume = hotplug_late_resume,
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
};

/****************************************
 * DEVICE ATTRIBUTES FUNCTION by tegrak
****************************************/
#define declare_show(filename) \
	static ssize_t show_##filename(struct device *dev, struct device_attribute *attr, char *buf)

#define declare_store(filename) \
	static ssize_t store_##filename(\
		struct device *dev, struct device_attribute *attr, const char *buf, size_t size)

/****************************************
 * second_core attributes function by tegrak
 ****************************************/
declare_show(version) {
	return sprintf(buf, "%u\n", SECOND_CORE_VERSION);
}

declare_show(author) {
	return sprintf(buf, "Tegrak\n");
}

declare_show(hotplug_on) {
	return sprintf(buf, "%s\n", (hotplug_on) ? ("on") : ("off"));
}

declare_store(hotplug_on) {
	mutex_lock(&hotplug_lock);

	if (user_lock) {
		goto finish;
	}

	if (!hotplug_on && strcmp(buf, "on\n") == 0) {
		hotplug_on = 1;
		// restart worker thread.
		hotpluging_rate = CHECK_DELAY;
		queue_delayed_work_on(0, hotplug_wq, &hotplug_work, hotpluging_rate);
		printk("second_core: hotplug is on!\n");
	}
	else if (hotplug_on && strcmp(buf, "off\n") == 0) {
		hotplug_on = 0;
		second_core_on = 1;
		if (cpu_online(1) == 0) {
			cpu_up(1);
		}
		printk("second_core: hotplug is off!\n");
	}

finish:
	mutex_unlock(&hotplug_lock);
	return size;
}

declare_show(second_core_on) {
	return sprintf(buf, "%s\n", (second_core_on) ? ("on") : ("off"));
}

declare_store(second_core_on) {
	mutex_lock(&hotplug_lock);

	if (hotplug_on || user_lock) {
		goto finish;
	}

	if (!second_core_on && strcmp(buf, "on\n") == 0) {
		second_core_on = 1;
		if (cpu_online(1) == 0) {
			cpu_up(1);
		}
		printk("second_core: 2nd core is always on!\n");
	}
	else if (second_core_on && strcmp(buf, "off\n") == 0) {
		second_core_on = 0;
		if (cpu_online(1) == 1) {
			cpu_down(1);
		}
		printk("second_core: 2nd core is always off!\n");
	}

finish:
	mutex_unlock(&hotplug_lock);
	return size;
}

/****************************************
 * DEVICE ATTRIBUTE by tegrak
 ****************************************/
#define declare_attr_rw(filename, perm) \
	static DEVICE_ATTR(filename, perm, show_##filename, store_##filename)
#define declare_attr_ro(filename, perm) \
	static DEVICE_ATTR(filename, perm, show_##filename, NULL)
#define declare_attr_wo(filename, perm) \
	static DEVICE_ATTR(filename, perm, NULL, store_##filename)

declare_attr_ro(version, 0444);
declare_attr_ro(author, 0444);
declare_attr_rw(hotplug_on, 0666);
declare_attr_rw(second_core_on, 0666);

static struct attribute *second_core_attributes[] = {
	&dev_attr_hotplug_on.attr,
	&dev_attr_second_core_on.attr,
	&dev_attr_version.attr,
	&dev_attr_author.attr,
	NULL
};

static struct attribute_group second_core_group = {
		.attrs  = second_core_attributes,
};

static struct miscdevice second_core_device = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "second_core",
};

static int __init exynos4_pm_hotplug_init(void)
{
	int ret;
	printk(KERN_INFO "EXYNOS4 PM-hotplug init function\n");
	hotplug_wq = create_singlethread_workqueue("dynamic hotplug");
	if (!hotplug_wq) {
		printk(KERN_ERR "Creation of hotplug work failed\n");
		return -EFAULT;
	}

	INIT_DELAYED_WORK_DEFERRABLE(&hotplug_work, hotplug_timer);

	queue_delayed_work_on(0, hotplug_wq, &hotplug_work, 60 * HZ);

	register_pm_notifier(&exynos4_pm_hotplug_notifier);
	register_reboot_notifier(&hotplug_reboot_notifier);
	register_early_suspend(&hotplug_early_suspend_notifier);

	// register second_core device by tegrak
	ret = misc_register(&second_core_device);
	if (ret) {
		printk(KERN_ERR "failed at(%d)\n", __LINE__);
		return ret;
	}

	ret = sysfs_create_group(&second_core_device.this_device->kobj, &second_core_group);
	if (ret)
	{
		printk(KERN_ERR "failed at(%d)\n", __LINE__);
		return ret;
	}

	return 0;
}

late_initcall(exynos4_pm_hotplug_init);

static struct platform_device exynos4_pm_hotplug_device = {
	.name = "exynos4-dynamic-cpu-hotplug",
	.id = -1,
};

static int __init exynos4_pm_hotplug_device_init(void)
{
	int ret;

	ret = platform_device_register(&exynos4_pm_hotplug_device);

	if (ret) {
		printk(KERN_ERR "failed at(%d)\n", __LINE__);
		return ret;
	}

	printk(KERN_INFO "exynos4_pm_hotplug_device_init: %d\n", ret);

	return ret;
}

late_initcall(exynos4_pm_hotplug_device_init);
