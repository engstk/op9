/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef _ADRENO_A6XX_HWSCHED_H_
#define _ADRENO_A6XX_HWSCHED_H_

#include "adreno_a6xx_hwsched_hfi.h"
#include "adreno_hwsched.h"

/**
 * struct a6xx_hwsched_device - Container for the a6xx hwscheduling device
 */
struct a6xx_hwsched_device {
	/** @a6xx_dev: Container for the a6xx device */
	struct a6xx_device a6xx_dev;
	/** @hwsched_hfi: Container for hwscheduling specific hfi resources */
	struct a6xx_hwsched_hfi hwsched_hfi;
	/** @hwsched: Container for the hardware dispatcher */
	struct adreno_hwsched hwsched;
};

/**
 * a6xx_hwsched_probe - Target specific probe for hwsched
 * @pdev: Pointer to the platform device
 * @chipid: Chipid of the target
 * @gpucore: Pointer to the gpucore
 *
 * The target specific probe function for hwsched enabled gmu targets.
 *
 * Return: 0 on success or negative error on failure
 */
int a6xx_hwsched_probe(struct platform_device *pdev,
	u32 chipid, const struct adreno_gpu_core *gpucore);

/**
 * a6xx_hwsched_restart - Restart the gmu and gpu
 * @adreno_dev: Pointer to the adreno device
 */
void a6xx_hwsched_restart(struct adreno_device *adreno_dev);

/**
 * a6xx_hwsched_snapshot - take a6xx hwsched snapshot
 * @adreno_dev: Pointer to the adreno device
 * @snapshot: Pointer to the snapshot instance
 *
 * Snapshot the faulty ib and then snapshot rest of a6xx gmu things
 */
void a6xx_hwsched_snapshot(struct adreno_device *adreno_dev,
	struct kgsl_snapshot *snapshot);
#endif
