/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_VIDEO_CC_SM8150_H
#define _DT_BINDINGS_CLK_QCOM_VIDEO_CC_SM8150_H

/* VIDEO_CC clocks */
#define VIDEO_PLL0					0
#define VIDEO_CC_IRIS_AHB_CLK				1
#define VIDEO_CC_IRIS_CLK_SRC				2
#define VIDEO_CC_MVS0_CORE_CLK				3
#define VIDEO_CC_MVS1_CORE_CLK				4
#define VIDEO_CC_MVSC_CORE_CLK				5
#define VIDEO_CC_XO_CLK					6

/* VIDEO_CC power domains */
#define MVS0_GDSC					0
#define MVS1_GDSC					1
#define MVSC_GDSC					2

/* VIDEO_CC resets */
#define VIDEO_CC_INTERFACE_BCR				0
#define VIDEO_CC_MVS0_BCR				1
#define VIDEO_CC_MVS1_BCR				2
#define VIDEO_CC_MVSC_BCR				3
#define VIDEO_CC_MVSC_CORE_CLK_BCR			4

#endif
