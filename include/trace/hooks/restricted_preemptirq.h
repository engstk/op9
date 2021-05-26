/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */
#if !defined(_TRACE_RESTRICTED_PREEMPTIRQ_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RESTRICTED_PREEMPTIRQ_H

#ifdef CONFIG_PREEMPTIRQ_TRACEPOINTS

#undef TRACE_SYSTEM
#define TRACE_SYSTEM restricted_preemptirq

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#ifdef CONFIG_TRACE_IRQFLAGS
DECLARE_RESTRICTED_HOOK(restricted_irq_disable,
			TP_PROTO(unsigned long ip, unsigned long parent_ip),
			TP_ARGS(ip, parent_ip), 1);

DECLARE_RESTRICTED_HOOK(restricted_irq_enable,
		       TP_PROTO(unsigned long ip, unsigned long parent_ip),
		       TP_ARGS(ip, parent_ip), 1);

#else
#define trace_restricted_irq_enable(ip, parent_ip)
#define trace_restricted_irq_disable(ip, parent_ip)
#endif /* CONFIG_TRACE_IRQFLAGS */

#ifdef CONFIG_TRACE_PREEMPT_TOGGLE
DECLARE_RESTRICTED_HOOK(restricted_preempt_disable,
		       TP_PROTO(unsigned long ip, unsigned long parent_ip),
		       TP_ARGS(ip, parent_ip), 1);

DECLARE_RESTRICTED_HOOK(restricted_preempt_enable,
			TP_PROTO(unsigned long ip, unsigned long parent_ip),
			TP_ARGS(ip, parent_ip), 1);

#else
#define trace_restricted_preempt_enable(ip, parent_ip)
#define trace_restricted_preempt_disable(ip, parent_ip)
#endif /* CONFIG_TRACE_PREEMPT_TOGGLE */

#include <trace/define_trace.h>

#else  /* ! CONFIG_PREEMPTIRQ_TRACEPOINTS */
#define trace_restricted_irq_enable(...)
#define trace_restricted_irq_disable(...)
#define trace_restricted_preempt_enable(...)
#define trace_restricted_preempt_disable(...)
#endif /* ! CONFIG_PREEMPTIRQ_TRACEPOINTS */

#endif /* TRACE_RESTRICTED_PREEMPTIRQ_H || TRACE_HEADER_MULTI_READ */
