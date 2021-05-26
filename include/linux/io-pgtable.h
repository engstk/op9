/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __IO_PGTABLE_H
#define __IO_PGTABLE_H

#include <linux/bitops.h>
#include <linux/iommu.h>

#include <linux/scatterlist.h>

#define to_msm_io_pgtable_info(_cfg) \
	container_of(_cfg, struct msm_io_pgtable_info, pgtbl_cfg)

/*
 * Public API for use by IOMMU drivers
 */
enum io_pgtable_fmt {
	ARM_32_LPAE_S1,
	ARM_32_LPAE_S2,
	ARM_64_LPAE_S1,
	ARM_64_LPAE_S2,
	ARM_V7S,
	ARM_MALI_LPAE,
#ifdef CONFIG_IOMMU_IO_PGTABLE_FAST
	ARM_V8L_FAST,
#endif
	IO_PGTABLE_NUM_FMTS,
};

/**
 * struct iommu_flush_ops - IOMMU callbacks for TLB and page table management.
 *
 * @tlb_flush_all:  Synchronously invalidate the entire TLB context.
 * @tlb_flush_walk: Synchronously invalidate all intermediate TLB state
 *                  (sometimes referred to as the "walk cache") for a virtual
 *                  address range.
 * @tlb_flush_leaf: Synchronously invalidate all leaf TLB state for a virtual
 *                  address range.
 * @tlb_add_page:   Optional callback to queue up leaf TLB invalidation for a
 *                  single page.  IOMMUs that cannot batch TLB invalidation
 *                  operations efficiently will typically issue them here, but
 *                  others may decide to update the iommu_iotlb_gather structure
 *                  and defer the invalidation until iommu_tlb_sync() instead.
 *
 * Note that these can all be called in atomic context and must therefore
 * not block.
 */
struct iommu_flush_ops {
	void (*tlb_flush_all)(void *cookie);
	void (*tlb_flush_walk)(unsigned long iova, size_t size, size_t granule,
			       void *cookie);
	void (*tlb_flush_leaf)(unsigned long iova, size_t size, size_t granule,
			       void *cookie);
	void (*tlb_add_page)(struct iommu_iotlb_gather *gather,
			     unsigned long iova, size_t granule, void *cookie);
};

/**
 * struct iommu_pgtable_ops - IOMMU callbacks for page table memory management.
 *
 * @alloc_pgtable: Allocate page table memory, and return a page-aligned
 *                 cacheable linear mapping address of the start of a physically
 *                 contiguous region of memory.
 * @free_pgtable: Free page table memory.
 */
struct iommu_pgtable_ops {
	void *(*alloc_pgtable)(void *cookie, int order, gfp_t gfp_mask);
	void (*free_pgtable)(void *cookie, void *virt, int order);
};

/**
 * struct io_pgtable_cfg - Configuration data for a set of page tables.
 *
 * @quirks:        A bitmap of hardware quirks that require some special
 *                 action by the low-level page table allocator.
 * @pgsize_bitmap: A bitmap of page sizes supported by this set of page
 *                 tables.
 * @ias:           Input address (iova) size, in bits.
 * @oas:           Output address (paddr) size, in bits.
 * @coherent_walk  A flag to indicate whether or not page table walks made
 *                 by the IOMMU are coherent with the CPU caches.
 * @tlb:           TLB management callbacks for this set of tables.
 * @iommu_pgtable_ops: IOMMU page table memory management callbacks (optional;
 *                     defaults to the buddy allocator if not present).
 * @iommu_dev:     The device representing the DMA configuration for the
 *                 page table walker.
 */
struct io_pgtable_cfg {
	/*
	 * IO_PGTABLE_QUIRK_ARM_NS: (ARM formats) Set NS and NSTABLE bits in
	 *	stage 1 PTEs, for hardware which insists on validating them
	 *	even in	non-secure state where they should normally be ignored.
	 *
	 * IO_PGTABLE_QUIRK_NO_PERMS: Ignore the IOMMU_READ, IOMMU_WRITE and
	 *	IOMMU_NOEXEC flags and map everything with full access, for
	 *	hardware which does not implement the permissions of a given
	 *	format, and/or requires some format-specific default value.
	 *
	 * IO_PGTABLE_QUIRK_TLBI_ON_MAP: If the format forbids caching invalid
	 *	(unmapped) entries but the hardware might do so anyway, perform
	 *	TLB maintenance when mapping as well as when unmapping.
	 *
	 * IO_PGTABLE_QUIRK_ARM_MTK_EXT: (ARM v7s format) MediaTek IOMMUs extend
	 *	to support up to 35 bits PA where the bit32, bit33 and bit34 are
	 *	encoded in the bit9, bit4 and bit5 of the PTE respectively.
	 *
	 * IO_PGTABLE_QUIRK_NON_STRICT: Skip issuing synchronous leaf TLBIs
	 *	on unmap, for DMA domains using the flush queue mechanism for
	 *	delayed invalidation.
	 *
	 * IO_PGTABLE_QUIRK_QCOM_USE_UPSTREAM_HINT: Override the attributes
	 *	set in TCR for the page table walker. Use attributes specified
	 *	by the upstream hw instead.
	 *
	 * IO_PGTABLE_QUIRK_QCOM_USE_LLC_NWA: Override the attributes
	 *	set in TCR for the page table walker with Write-Back,
	 *	no Write-Allocate cacheable encoding.
	 *
	 */
	#define IO_PGTABLE_QUIRK_ARM_NS		BIT(0)
	#define IO_PGTABLE_QUIRK_NO_PERMS	BIT(1)
	#define IO_PGTABLE_QUIRK_TLBI_ON_MAP	BIT(2)
	#define IO_PGTABLE_QUIRK_ARM_MTK_EXT	BIT(3)
	#define IO_PGTABLE_QUIRK_NON_STRICT	BIT(4)
	#define IO_PGTABLE_QUIRK_QCOM_USE_UPSTREAM_HINT	BIT(5)
	#define IO_PGTABLE_QUIRK_QCOM_USE_LLC_NWA	BIT(6)
	unsigned long			quirks;
	unsigned long			pgsize_bitmap;
	unsigned int			ias;
	unsigned int			oas;
	bool				coherent_walk;
	const struct iommu_flush_ops	*tlb;
	const struct iommu_pgtable_ops  *iommu_pgtable_ops;
	struct device			*iommu_dev;

	/* Low-level data specific to the table format */
	union {
		struct {
			u64	ttbr[2];
			u64	tcr;
			u64	mair[2];
		} arm_lpae_s1_cfg;

		struct {
			u64	vttbr;
			u64	vtcr;
		} arm_lpae_s2_cfg;

		struct {
			u32	ttbr[2];
			u32	tcr;
			u32	nmrr;
			u32	prrr;
		} arm_v7s_cfg;

		struct {
			u64	transtab;
			u64	memattr;
		} arm_mali_lpae_cfg;
	};
};

/**
 * struct io_pgtable_ops - Page table manipulation API for IOMMU drivers.
 *
 * @map:          Map a physically contiguous memory region.
 * @unmap:        Unmap a physically contiguous memory region.
 * @iova_to_phys: Translate iova to physical address.
 *
 * These functions map directly onto the iommu_ops member functions with
 * the same names.
 */
struct io_pgtable_ops {
	int (*map)(struct io_pgtable_ops *ops, unsigned long iova,
		   phys_addr_t paddr, size_t size, int prot);
	size_t (*unmap)(struct io_pgtable_ops *ops, unsigned long iova,
			size_t size, struct iommu_iotlb_gather *gather);
	phys_addr_t (*iova_to_phys)(struct io_pgtable_ops *ops,
				    unsigned long iova);
};

/**
 * struct msm_io_pgtable_info - MSM specific page table manipulation API for
 * IOMMU drivers, and page table configuration.
 *
 * @map_sg:		Map a scatterlist.  Returns the number of bytes mapped,
 *			or -ve val on failure.  The size parameter contains the
 *			size of the partial mapping in case of failure.
 * @is_iova_coherent:	Checks coherency of given IOVA. Returns True if coherent
 *			and False if non-coherent.
 * @iova_to_pte:	Translate iova to Page Table Entry (PTE).
 * @pgtbl_cfg:		The configuration for a set of page tables.
 * @iova_base:		Configured IOVA base
 * @iova_end:		Configured IOVA end
 */
struct msm_io_pgtable_info {
	int (*map_sg)(struct io_pgtable_ops *ops, unsigned long iova,
		      struct scatterlist *sg, unsigned int nents, int prot,
		      size_t *size);
	bool (*is_iova_coherent)(struct io_pgtable_ops *ops,
				 unsigned long iova);
	uint64_t (*iova_to_pte)(struct io_pgtable_ops *ops, unsigned long iova);
	struct io_pgtable_cfg pgtbl_cfg;
	dma_addr_t	iova_base;
	dma_addr_t	iova_end;
};

/**
 * alloc_io_pgtable_ops() - Allocate a page table allocator for use by an IOMMU.
 *
 * @fmt:    The page table format.
 * @cfg:    The page table configuration. This will be modified to represent
 *          the configuration actually provided by the allocator (e.g. the
 *          pgsize_bitmap may be restricted).
 * @cookie: An opaque token provided by the IOMMU driver and passed back to
 *          the callback routines in cfg->tlb.
 */
struct io_pgtable_ops *alloc_io_pgtable_ops(enum io_pgtable_fmt fmt,
					    struct io_pgtable_cfg *cfg,
					    void *cookie);

/**
 * free_io_pgtable_ops() - Free an io_pgtable_ops structure. The caller
 *                         *must* ensure that the page table is no longer
 *                         live, but the TLB can be dirty.
 *
 * @ops: The ops returned from alloc_io_pgtable_ops.
 */
void free_io_pgtable_ops(struct io_pgtable_ops *ops);

/**
 * io_pgtable_alloc_pages - Allocate memory for page tables using an IOMMU
 *                          driver's provided callback, or the buddy allocator.
 *
 * @cfg:      The page table configuration. This will be used to determine if
 *            the page table memory should be allocated through the IOMMU
 *            driver's callback, or the buddy allocator.
 * @cookie:   An opaque pointer used by the IOMMU driver's callback.
 * @order:    The order of the size of the allocation.
 * @gfp_mask: The GFP mask to be used with the allocation
 *
 * Returns a cacheable linear mapping address to a physically contiguous region
 * of memory. The start of the region must be page-aligned.
 */
void *io_pgtable_alloc_pages(struct io_pgtable_cfg *cfg, void *cookie,
			     int order, gfp_t gfp_mask);

/**
 * io_pgtable_free_pages - Free memory for page tables using an IOMMU
 *                         driver's provided callback, or the buddy allocator.
 *
 * @cfg:      The page table configuration. This will be used to determine if
 *            the page table memory should be allocated through the IOMMU
 *            driver's callback, or the buddy allocator.
 * @cookie:   An opage pointer used by the IOMMU driver's callback.
 * @virt:     The virtual address of the memory to free.
 * @order:     The order of the size of the allocation.
 */
void io_pgtable_free_pages(struct io_pgtable_cfg *cfg, void *cookie, void *virt,
			   int order);

/*
 * Internal structures for page table allocator implementations.
 */

/**
 * struct io_pgtable - Internal structure describing a set of page tables.
 *
 * @fmt:    The page table format.
 * @cookie: An opaque token provided by the IOMMU driver and passed back to
 *          any callback routines.
 * @cfg:    A copy of the page table configuration.
 * @ops:    The page table operations in use for this set of page tables.
 */
struct io_pgtable {
	enum io_pgtable_fmt	fmt;
	void			*cookie;
	struct io_pgtable_cfg	cfg;
	struct io_pgtable_ops	ops;
};

#define io_pgtable_ops_to_pgtable(x) container_of((x), struct io_pgtable, ops)

static inline void io_pgtable_tlb_flush_all(struct io_pgtable *iop)
{
	if (!iop->cfg.tlb)
		return;
	iop->cfg.tlb->tlb_flush_all(iop->cookie);
}

static inline void
io_pgtable_tlb_flush_walk(struct io_pgtable *iop, unsigned long iova,
			  size_t size, size_t granule)
{
	iop->cfg.tlb->tlb_flush_walk(iova, size, granule, iop->cookie);
}

static inline void
io_pgtable_tlb_flush_leaf(struct io_pgtable *iop, unsigned long iova,
			  size_t size, size_t granule)
{
	iop->cfg.tlb->tlb_flush_leaf(iova, size, granule, iop->cookie);
}

static inline void
io_pgtable_tlb_add_page(struct io_pgtable *iop,
			struct iommu_iotlb_gather * gather, unsigned long iova,
			size_t granule)
{
	if (iop->cfg.tlb->tlb_add_page)
		iop->cfg.tlb->tlb_add_page(gather, iova, granule, iop->cookie);
}

/**
 * struct io_pgtable_init_fns - Alloc/free a set of page tables for a
 *                              particular format.
 *
 * @alloc: Allocate a set of page tables described by cfg.
 * @free:  Free the page tables associated with iop.
 */
struct io_pgtable_init_fns {
	struct io_pgtable *(*alloc)(struct io_pgtable_cfg *cfg, void *cookie);
	void (*free)(struct io_pgtable *iop);
};

extern struct io_pgtable_init_fns io_pgtable_arm_32_lpae_s1_init_fns;
extern struct io_pgtable_init_fns io_pgtable_arm_32_lpae_s2_init_fns;
extern struct io_pgtable_init_fns io_pgtable_arm_64_lpae_s1_init_fns;
extern struct io_pgtable_init_fns io_pgtable_arm_64_lpae_s2_init_fns;
extern struct io_pgtable_init_fns io_pgtable_arm_v7s_init_fns;
extern struct io_pgtable_init_fns io_pgtable_arm_mali_lpae_init_fns;
#ifdef CONFIG_IOMMU_IO_PGTABLE_FAST
extern struct io_pgtable_init_fns io_pgtable_av8l_fast_init_fns;
#endif

/**
 * io_pgtable_alloc_pages_exact:
 *	allocate an exact number of physically-contiguous pages.
 * @size: the number of bytes to allocate
 * @gfp_mask: GFP flags for the allocation
 *
 * Like alloc_pages_exact(), but with some additional accounting for debug
 * purposes.
 */
void *io_pgtable_alloc_pages_exact(struct io_pgtable_cfg *cfg, void *cookie,
				   size_t size, gfp_t gfp_mask);

/**
 * io_pgtable_free_pages_exact:
 *	release memory allocated via io_pgtable_alloc_pages_exact()
 * @virt: the value returned by alloc_pages_exact.
 * @size: size of allocation, same value as passed to alloc_pages_exact().
 *
 * Like free_pages_exact(), but with some additional accounting for debug
 * purposes.
 */
void io_pgtable_free_pages_exact(struct io_pgtable_cfg *cfg, void *cookie,
				 void *virt, size_t size);

#endif /* __IO_PGTABLE_H */
