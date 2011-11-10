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
 * cpu topology mask management
 */

unsigned int advanced_topology = 0;

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
	}
	smp_wmb();
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

	/* clear core mask */
	clear_cpu_topology_mask();

	/* update core and thread sibling masks */
	normal_cpu_topology_mask();

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
	}
	smp_wmb();
}
