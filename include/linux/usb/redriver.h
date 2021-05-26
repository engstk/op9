/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef __LINUX_USB_REDRIVER_H
#define __LINUX_USB_REDRIVER_H

#ifdef CONFIG_USB_REDRIVER

int redriver_release_usb_lanes(struct device_node *node);

#else

static inline int redriver_release_usb_lanes(struct device_node *node)
{
	return 0;
}

#endif

#endif /*__LINUX_USB_REDRIVER_H */
