/*
 * gpu_voltage_control.c -- gpu voltage control interface for the sgs2
 *
 *  Copyright (C) 2011 Michael Wodkins
 *  twitter - @xdanetarchy
 *  XDA-developers - netarchy
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of the GNU General Public License as published by the
 *  Free Software Foundation;
 *
 */

#include <linux/platform_device.h>
#include <linux/miscdevice.h>

#include "gpu_voltage_control.h"

#if defined(CONFIG_GPU_UNDERVOLTING)
int gpu_voltage_control[2] = { 850000, 950000 };
#else
int gpu_voltage_control[2] = { 950000, 1000000 };
#endif
static ssize_t gpu_voltage_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return sprintf(buf, "Step1: %d\nStep2: %d\n", gpu_voltage_control[0], gpu_voltage_control[1]);
}

static ssize_t gpu_voltage_store(struct device *dev, struct device_attribute *attr, const char *buf,
									size_t count) {
	unsigned int ret = -EINVAL;
	int i = 0;
	ret = sscanf(buf, "%d %d", &gpu_voltage_control[0], &gpu_voltage_control[1]);
	if (ret != 2) {
		return -EINVAL;
	}
       else {
                /* safety floor and ceiling - netarchy */
                for( i = 0; i < 2; i++ ) {
                        if (gpu_voltage_control[i] < 500000) {
                                gpu_voltage_control[i] = 500000;
                        }
                        else if (gpu_voltage_control[i] > 1200000) {
                                gpu_voltage_control[i] = 1200000;
                        }
                }
        }
	return count;
}

static DEVICE_ATTR(gpu_control, S_IRUGO | S_IWUGO, gpu_voltage_show, gpu_voltage_store);

static struct attribute *gpu_voltage_control_attributes[] = {
	&dev_attr_gpu_control.attr,
	NULL
};

static struct attribute_group gpu_voltage_control_group = {
	.attrs = gpu_voltage_control_attributes,
};

static struct miscdevice gpu_voltage_control_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "gpu_voltage_control",
};

void gpu_voltage_control_start()
{
	printk("Initializing gpu voltage control interface\n");

	misc_register(&gpu_voltage_control_device);
	if (sysfs_create_group(&gpu_voltage_control_device.this_device->kobj,
				&gpu_voltage_control_group) < 0) {
		printk("%s sysfs_create_group failed\n", __FUNCTION__);
		pr_err("Unable to create group for %s\n", gpu_voltage_control_device.name);
	}
}

