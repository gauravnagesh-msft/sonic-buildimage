// SPDX-License-Identifier: GPL-2.0
/*
 * kdump_runtime - PoC: enable kdump on a running SONiC switch with no reboot
 * and no crashkernel= boot parameter.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/ioport.h>
#include <linux/nodemask.h>
#include <asm/io.h>

static int stage;
module_param(stage, int, 0444);
MODULE_PARM_DESC(stage, "0=noop, 1=alloc+free, 2=alloc+crashk_res hold");

static int size_mb = 256;
module_param(size_mb, int, 0444);
MODULE_PARM_DESC(size_mb, "size of high crash region in MB (0=skip)");

static int low_mb;
module_param(low_mb, int, 0444);
MODULE_PARM_DESC(low_mb, "size of low (<4GB) crash region in MB (0=skip)");

static unsigned long acp_addr;
module_param(acp_addr, ulong, 0444);
MODULE_PARM_DESC(acp_addr, "kallsyms address of alloc_contig_pages");

static unsigned long crashk_addr;
module_param(crashk_addr, ulong, 0444);
MODULE_PARM_DESC(crashk_addr, "kallsyms address of crashk_res");

static unsigned long clow_addr;
module_param(clow_addr, ulong, 0444);
MODULE_PARM_DESC(clow_addr, "kallsyms address of crashk_low_res");

typedef struct page *(*acp_fn)(unsigned long nr_pages, gfp_t gfp,
			       int nid, nodemask_t *nodemask);

static acp_fn alloc_contig_pages_p;

static struct page *held_page;
static unsigned long held_nr;
static struct resource *crashk;

static struct page *held_low_page;
static unsigned long held_low_nr;
static struct resource *crashk_low;

static struct page *kdr_alloc(int mb, bool dma32, unsigned long *nr_out,
			      phys_addr_t *phys_out)
{
	unsigned long nr_pages = ((unsigned long)mb) << (20 - PAGE_SHIFT);
	gfp_t gfp = GFP_KERNEL;
	struct page *pg;
	phys_addr_t phys;

	if (dma32)
		gfp |= __GFP_DMA32;

	pg = alloc_contig_pages_p(nr_pages, gfp, numa_node_id(), NULL);
	if (!pg) {
		pr_err("kdump_runtime: alloc_contig_pages(%lu pages, %s) failed\n",
		       nr_pages, dma32 ? "DMA32" : "normal");
		return NULL;
	}
	phys = page_to_phys(pg);
	*nr_out = nr_pages;
	*phys_out = phys;
	pr_info("kdump_runtime: allocated %d MB %s contiguous at phys 0x%llx%s\n",
		mb, dma32 ? "LOW(<4G)" : "high", (unsigned long long)phys,
		(dma32 && (phys + ((phys_addr_t)mb << 20) - 1) >= (4ULL << 30))
			? " [WARN: crosses/above 4G!]" : "");
	return pg;
}

static void kdr_set_resource(struct resource *res, phys_addr_t phys, int mb,
			     const char *name)
{
	res->start = phys;
	res->end = phys + ((phys_addr_t)mb << 20) - 1;
	res->flags = IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM | IORESOURCE_MEM;
	if (!res->name)
		res->name = name;
	if (insert_resource(&iomem_resource, res))
		pr_warn("kdump_runtime: insert_resource(%s) failed (maybe already present)\n",
			name);
	pr_info("kdump_runtime: %s set to 0x%llx-0x%llx\n", name,
		(unsigned long long)res->start,
		(unsigned long long)res->end);
}

static int __init kdr_init(void)
{
	struct page *pg;
	phys_addr_t phys;
	unsigned long nr;

	pr_info("kdump_runtime: init stage=%d size_mb=%d low_mb=%d\n",
		stage, size_mb, low_mb);

	if (stage == 0) {
		pr_info("kdump_runtime: noop load OK (vermagic/CRC matched)\n");
		return 0;
	}

	if (!acp_addr) {
		pr_err("kdump_runtime: acp_addr required for stage>=1\n");
		return -EINVAL;
	}
	alloc_contig_pages_p = (acp_fn)acp_addr;

	if (size_mb > 0) {
		pg = kdr_alloc(size_mb, false, &nr, &phys);
		if (!pg)
			return -ENOMEM;
		if (stage == 1) {
			free_contig_range(page_to_pfn(pg), nr);
			pr_info("kdump_runtime: freed high region\n");
		} else {
			if (!crashk_addr) {
				pr_err("kdump_runtime: crashk_addr required for stage 2 high region\n");
				free_contig_range(page_to_pfn(pg), nr);
				return -EINVAL;
			}
			crashk = (struct resource *)crashk_addr;
			kdr_set_resource(crashk, phys, size_mb, "Crash kernel");
			held_page = pg;
			held_nr = nr;
		}
	}

	if (low_mb > 0) {
		pg = kdr_alloc(low_mb, true, &nr, &phys);
		if (!pg) {
			pr_err("kdump_runtime: low region alloc failed\n");
			if (held_page) {
				if (crashk) {
					remove_resource(crashk);
					crashk->start = 0;
					crashk->end = 0;
				}
				free_contig_range(page_to_pfn(held_page), held_nr);
				held_page = NULL;
			}
			return -ENOMEM;
		}
		if (stage == 1) {
			free_contig_range(page_to_pfn(pg), nr);
			pr_info("kdump_runtime: freed low region; stage 1 done\n");
		} else {
			if (!clow_addr) {
				pr_err("kdump_runtime: clow_addr required for stage 2 low region\n");
				free_contig_range(page_to_pfn(pg), nr);
			} else {
				crashk_low = (struct resource *)clow_addr;
				kdr_set_resource(crashk_low, phys, low_mb,
						 "Crash kernel");
				held_low_page = pg;
				held_low_nr = nr;
			}
		}
	}

	if (stage == 1)
		return -EAGAIN;

	if (!held_page && !held_low_page) {
		pr_err("kdump_runtime: stage 2 requested but nothing was reserved\n");
		return -EINVAL;
	}
	pr_info("kdump_runtime: stage 2 armed (high=%s low=%s)\n",
		held_page ? "yes" : "no", held_low_page ? "yes" : "no");
	return 0;
}

static void __exit kdr_exit(void)
{
	if (held_low_page) {
		if (crashk_low) {
			remove_resource(crashk_low);
			crashk_low->start = 0;
			crashk_low->end = 0;
		}
		free_contig_range(page_to_pfn(held_low_page), held_low_nr);
		pr_info("kdump_runtime: released low crash region\n");
	}
	if (held_page) {
		if (crashk) {
			remove_resource(crashk);
			crashk->start = 0;
			crashk->end = 0;
		}
		free_contig_range(page_to_pfn(held_page), held_nr);
		pr_info("kdump_runtime: released high crash region\n");
	}
	pr_info("kdump_runtime: exit\n");
}

module_init(kdr_init);
module_exit(kdr_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SONiC kdump PoC");
MODULE_DESCRIPTION("Runtime kdump enable without reboot / crashkernel=");
