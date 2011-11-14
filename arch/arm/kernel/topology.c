/*
 * arch/arm/kernel/topology.c
 *
 * Copyright (C) 2011 Linaro Limited.
 * Written by: Vincent Guittot
 *
 * based on arch/sh/kernel/topology.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/node.h>
#include <linux/nodemask.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/cpuset.h>

#include <asm/cputype.h>
#include <asm/topology.h>

#define MPIDR_SMP_BITMASK (0x3 << 30)
#define MPIDR_SMP_VALUE (0x2 << 30)

#define MPIDR_MT_BITMASK (0x1 << 24)

/*
 * These masks reflect the current use of the affinity levels.
 * The affinity level can be up to 16 bits according to ARM ARM
 */

#define MPIDR_LEVEL0_MASK 0x3
#define MPIDR_LEVEL0_SHIFT 0

#define MPIDR_LEVEL1_MASK 0xF
#define MPIDR_LEVEL1_SHIFT 8

#define MPIDR_LEVEL2_MASK 0xFF
#define MPIDR_LEVEL2_SHIFT 16

struct cputopo_arm cpu_topology[NR_CPUS];

/*
 * cpu power scale management
 */

/*
 * a per cpu data structure should be better because each cpu is mainly
 * using its own cpu_power even it's not always true because of
 * no_hz_idle_balance
 */
static DEFINE_PER_CPU(unsigned int, cpu_scale);

/*
 * cpu topology mask management
 */

unsigned int advanced_topology = 1;

static void normal_cpu_topology_mask(void);
static void (*set_cpu_topology_mask)(void) = normal_cpu_topology_mask;

/* This table sets the cpu_power scale of a cpu according to the sched_mc mode.
 * The content of this table could be SoC specific so we should add a method to
 * overwrite this default table.
 * TODO: Study how to use DT for setting this table
 */
#define ARM_CORTEX_A9_DEFAULT_SCALE 0
#define ARM_CORTEX_A9_POWER_SCALE 1
/* This table list all possible cpu power configuration */
unsigned int table_config[2] = {
	1024,
	4096
};

static void set_power_scale(unsigned int cpu, unsigned int idx)
{
	per_cpu(cpu_scale, cpu) = table_config[idx];
}

static int init_cpu_power_scale(void)
{
	/* Do we need to change default config */
	advanced_topology = 1;

	/* force topology update */
	arch_update_cpu_topology();

	/* Force a cpu topology update */
	rebuild_sched_domains();

	return 0;
}

core_initcall(init_cpu_power_scale);

/*
 * Update the cpu power
 */

unsigned long arch_scale_freq_power(struct sched_domain *sd, int cpu)
{
	return per_cpu(cpu_scale, cpu);
}

/*
 * default topology function
 */

const struct cpumask *cpu_coregroup_mask(int cpu)
{
	return &cpu_topology[cpu].core_sibling;
}

/*
 * clear cpu topology masks
 */
static void clear_cpu_topology_mask(void)
{
	unsigned int cpuid;
	for_each_possible_cpu(cpuid) {
		struct cputopo_arm *cpuid_topo = &(cpu_topology[cpuid]);
		cpumask_clear(&cpuid_topo->core_sibling);
		cpumask_clear(&cpuid_topo->thread_sibling);
	}
	smp_wmb();
}

/*
 * default_cpu_topology_mask set the core and thread mask as described in the
 * ARM ARM
 */
static inline void default_cpu_topology_mask(unsigned int cpuid)
{
	struct cputopo_arm *cpuid_topo = &cpu_topology[cpuid];
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		struct cputopo_arm *cpu_topo = &cpu_topology[cpu];

		if (cpuid_topo->socket_id == cpu_topo->socket_id) {
			cpumask_set_cpu(cpuid, &cpu_topo->core_sibling);
			if (cpu != cpuid)
				cpumask_set_cpu(cpu,
					&cpuid_topo->core_sibling);

			if (cpuid_topo->core_id == cpu_topo->core_id) {
				cpumask_set_cpu(cpuid,
					&cpu_topo->thread_sibling);
				if (cpu != cpuid)
					cpumask_set_cpu(cpu,
						&cpuid_topo->thread_sibling);
			}
		}
	}
	smp_wmb();
}

static void normal_cpu_topology_mask(void)
{
	unsigned int cpuid;

	for_each_possible_cpu(cpuid) {
		default_cpu_topology_mask(cpuid);
		set_power_scale(cpuid, ARM_CORTEX_A9_DEFAULT_SCALE);
	}
	smp_wmb();
}

/*
 * For Cortex-A9 MPcore, we emulate a multi-package topology in power mode.
 * The goal is to gathers tasks on 1 virtual package
 */
static void power_cpu_topology_mask_CA9(void)
{
	unsigned int cpuid, cpu;

	for_each_possible_cpu(cpuid) {
		struct cputopo_arm *cpuid_topo = &cpu_topology[cpuid];

		for_each_possible_cpu(cpu) {
			struct cputopo_arm *cpu_topo = &cpu_topology[cpu];

			if ((cpuid_topo->socket_id == cpu_topo->socket_id)
			&& ((cpuid & 0x1) == (cpu & 0x1))) {
				cpumask_set_cpu(cpuid, &cpu_topo->core_sibling);
				if (cpu != cpuid)
					cpumask_set_cpu(cpu,
						&cpuid_topo->core_sibling);

				if (cpuid_topo->core_id == cpu_topo->core_id) {
					cpumask_set_cpu(cpuid,
						&cpu_topo->thread_sibling);
					if (cpu != cpuid)
						cpumask_set_cpu(cpu,
							&cpuid_topo->thread_sibling);
				}
			}
		}
		set_power_scale(cpuid, ARM_CORTEX_A9_POWER_SCALE);
	}
	smp_wmb();
}

#define ARM_FAMILY_MASK 0xFF0FFFF0
#define ARM_CORTEX_A9_FAMILY 0x410FC090

/* update_cpu_topology_policy select a cpu topology policy according to the
 * available cores.
 * TODO: The current version assumes that all cores are exactly the same which
 * might not be true. We need to update it to take into account various
 * configuration among which system with different kind of core.
 */
static int update_cpu_topology_policy(void)
{
	unsigned long cpuid;

	if (sched_mc_power_savings == POWERSAVINGS_BALANCE_NONE) {
		set_cpu_topology_mask = normal_cpu_topology_mask;
		return 0;
	}

	cpuid = read_cpuid_id();
	cpuid &= ARM_FAMILY_MASK;

	switch (cpuid) {
	case ARM_CORTEX_A9_FAMILY:
		set_cpu_topology_mask = power_cpu_topology_mask_CA9;
	break;
	default:
		set_cpu_topology_mask = normal_cpu_topology_mask;
	break;
	}

	return 0;
}

/*
 * store_cpu_topology is called at boot when only one cpu is running
 * and with the mutex cpu_hotplug.lock locked, when several cpus have booted,
 * which prevents simultaneous write access to cpu_topology array
 */
void store_cpu_topology(unsigned int cpuid)
{
	struct cputopo_arm *cpuid_topo = &cpu_topology[cpuid];
	unsigned int mpidr;

	/* If the cpu topology has been already set, just return */
	if (cpuid_topo->core_id != -1)
		return;

	mpidr = read_cpuid_mpidr();

	/* create cpu topology mapping */
	if ((mpidr & MPIDR_SMP_BITMASK) == MPIDR_SMP_VALUE) {
		/*
		 * This is a multiprocessor system
		 * multiprocessor format & multiprocessor mode field are set
		 */

		if (mpidr & MPIDR_MT_BITMASK) {
			/* core performance interdependency */
			cpuid_topo->thread_id = (mpidr >> MPIDR_LEVEL0_SHIFT)
				& MPIDR_LEVEL0_MASK;
			cpuid_topo->core_id = (mpidr >> MPIDR_LEVEL1_SHIFT)
				& MPIDR_LEVEL1_MASK;
			cpuid_topo->socket_id = (mpidr >> MPIDR_LEVEL2_SHIFT)
				& MPIDR_LEVEL2_MASK;
		} else {
			/* largely independent cores */
			cpuid_topo->thread_id = -1;
			cpuid_topo->core_id = (mpidr >> MPIDR_LEVEL0_SHIFT)
				& MPIDR_LEVEL0_MASK;
			cpuid_topo->socket_id = (mpidr >> MPIDR_LEVEL1_SHIFT)
				& MPIDR_LEVEL1_MASK;
		}
	} else {
		/*
		 * This is an uniprocessor system
		 * we are in multiprocessor format but uniprocessor system
		 * or in the old uniprocessor format
		 */
		cpuid_topo->thread_id = -1;
		cpuid_topo->core_id = 0;
		cpuid_topo->socket_id = -1;
	}

	/*
	 * The core and thread sibling masks can also be updated during the
	 * call of arch_update_cpu_topology
	 */
	default_cpu_topology_mask(cpuid);

	printk(KERN_INFO "CPU%u: thread %d, cpu %d, socket %d, mpidr %x\n",
		cpuid, cpu_topology[cpuid].thread_id,
		cpu_topology[cpuid].core_id,
		cpu_topology[cpuid].socket_id, mpidr);
}

/*
 * arch_update_cpu_topology is called by the scheduler before building
 * a new sched_domain hierarchy.
 */
int arch_update_cpu_topology(void)
{
	if (!advanced_topology)
		return 0;

	/* clear core threads mask */
	clear_cpu_topology_mask();

	/* set topology policy */
	update_cpu_topology_policy();

	/* set topology mask and power */
	(*set_cpu_topology_mask)();

	return 1;
}

/*
 * init_cpu_topology is called at boot when only one cpu is running
 * which prevent simultaneous write access to cpu_topology array
 */
void init_cpu_topology(void)
{
	unsigned int cpu;

	/* init core mask */
	for_each_possible_cpu(cpu) {
		struct cputopo_arm *cpu_topo = &(cpu_topology[cpu]);

		cpu_topo->thread_id = -1;
		cpu_topo->core_id =  -1;
		cpu_topo->socket_id = -1;
		cpumask_clear(&cpu_topo->core_sibling);
		cpumask_clear(&cpu_topo->thread_sibling);

		per_cpu(cpu_scale, cpu) = SCHED_POWER_SCALE;
	}
	smp_wmb();
}
