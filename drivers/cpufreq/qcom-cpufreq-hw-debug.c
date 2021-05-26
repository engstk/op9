// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) "cpufreq_hw_debug: %s: " fmt, __func__

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

enum debug_hw_regs_data {
	REG_PERF_STATE,
	REG_CYCLE_CNTR,
	REG_PSTATE_STATUS,

	REG_ARRAY_SIZE,
};

struct cpufreq_hwregs {
	void __iomem *base[REG_ARRAY_SIZE];
	int domain_cnt;
	struct dentry *debugfs_base;
};

struct cpufreq_register_data {
	char *name;
	u16 offset;
};

static struct cpufreq_hwregs *hw_regs;
static const u16 *offsets;

static const u16 cpufreq_qcom_std_data[REG_ARRAY_SIZE] = {
	[REG_PERF_STATE]		= 0x920,
	[REG_CYCLE_CNTR]		= 0x9c0,
	[REG_PSTATE_STATUS]		= 0x700,
};

static const u16 cpufreq_qcom_std_epss_data[REG_ARRAY_SIZE] = {
	[REG_PERF_STATE]		= 0x320,
	[REG_CYCLE_CNTR]		= 0x3c4,
	[REG_PSTATE_STATUS]		= 0x020,
};

static int print_cpufreq_hw_debug_regs(struct seq_file *s, void *unused)
{
	int i, j;
	u32 regval;

	static struct cpufreq_register_data data[] = {
		{"PERF_STATE_DESIRED", REG_PERF_STATE},
		{"CYCLE_CNTR_VAL", REG_CYCLE_CNTR},
		{"PSTATE_STATUS", REG_PSTATE_STATUS},
	};

	for (i = 0; i < hw_regs->domain_cnt; i++) {
		seq_printf(s, "FREQUENCY DOMAIN %d\n", i);
		for (j = 0; j < ARRAY_SIZE(data); j++) {
			regval = readl_relaxed(hw_regs->base[i] +
						offsets[data[j].offset]);
			seq_printf(s, "%25s: 0x%.8x\n", data[j].name, regval);
		}
	}

	return 0;
}

static int print_cpufreq_hw_reg_open(struct inode *inode, struct file *file)
{
	return single_open(file, print_cpufreq_hw_debug_regs, NULL);
}

static const struct file_operations cpufreq_debug_register_fops = {
	.open = print_cpufreq_hw_reg_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int cpufreq_panic_callback(struct notifier_block *nfb,
					unsigned long event, void *unused)
{
	int i, j;
	u32 regval;

	static struct cpufreq_register_data data[] = {
		{"PERF_STATE_DESIRED", REG_PERF_STATE},
		{"CYCLE_CNTR_VAL", REG_CYCLE_CNTR},
		{"PSTATE_STATUS", REG_PSTATE_STATUS},
	};

	for (i = 0; i < hw_regs->domain_cnt; i++) {
		pr_err("FREQUENCY DOMAIN %d\n", i);
		for (j = 0; j < ARRAY_SIZE(data); j++) {
			regval = readl_relaxed(hw_regs->base[i] +
						offsets[data[j].offset]);
			pr_err("%25s: 0x%.8x\n", data[j].name, regval);
		}
	}

	return NOTIFY_OK;
}

static struct notifier_block cpufreq_panic_notifier = {
	.notifier_call = cpufreq_panic_callback,
	.priority = 1,
};

static int cpufreq_get_hwregs(struct platform_device *pdev)
{
	struct of_phandle_args args;
	struct property *prop;
	struct resource res;
	void __iomem *base;
	int i, ret;

	offsets = of_device_get_match_data(&pdev->dev);
	if (!offsets)
		return -EINVAL;

	hw_regs = devm_kzalloc(&pdev->dev, sizeof(*hw_regs), GFP_KERNEL);
	if (!hw_regs)
		return -ENOMEM;

	prop = of_find_property(pdev->dev.of_node, "qcom,freq-hw-domain", NULL);
	if (!prop)
		return -EINVAL;

	hw_regs->domain_cnt = prop->length / (2 * sizeof(prop->length));

	for (i = 0; i < hw_regs->domain_cnt; i++) {
		ret = of_parse_phandle_with_fixed_args(pdev->dev.of_node,
			"qcom,freq-hw-domain", 1, i, &args);
		of_node_put(pdev->dev.of_node);
		if (ret)
			return ret;

		ret = of_address_to_resource(args.np, args.args[0], &res);
		if (ret)
			return ret;

		base = devm_ioremap(&pdev->dev, res.start, resource_size(&res));
		if (!base)
			return -ENOMEM;

		hw_regs->base[i] = base;
	}

	atomic_notifier_chain_register(&panic_notifier_list,
						&cpufreq_panic_notifier);

	return 0;
}

static int enable_cpufreq_hw_debug(struct platform_device *pdev)
{
	int ret;

	ret = cpufreq_get_hwregs(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to map cpufreq hw regs\n");
		return ret;
	}

	hw_regs->debugfs_base = debugfs_create_dir("qcom-cpufreq-hw", NULL);
	if (!hw_regs->debugfs_base) {
		dev_err(&pdev->dev, "Failed to create debugfs entry\n");
		return -ENODEV;
	}

	if (!debugfs_create_file("print_cpufreq_debug_regs", 0444,
		hw_regs->debugfs_base, NULL, &cpufreq_debug_register_fops))
		goto debugfs_fail;

	return 0;

debugfs_fail:
	dev_err(&pdev->dev, "Failed to create debugfs entry so cleaning up\n");
	debugfs_remove_recursive(hw_regs->debugfs_base);
	return -ENODEV;
}

static int qcom_cpufreq_hw_debug_probe(struct platform_device *pdev)
{
	return enable_cpufreq_hw_debug(pdev);
}

static int qcom_cpufreq_hw_debug_remove(struct platform_device *pdev)
{
	debugfs_remove_recursive(hw_regs->debugfs_base);
	return 0;
}

static const struct of_device_id qcom_cpufreq_hw_debug_match[] = {
	{ .compatible = "qcom,cpufreq-hw-debug",
					.data = &cpufreq_qcom_std_data },
	{ .compatible = "qcom,cpufreq-hw-epss-debug",
					.data = &cpufreq_qcom_std_epss_data },
	{}
};

static struct platform_driver qcom_cpufreq_hw_debug = {
	.probe = qcom_cpufreq_hw_debug_probe,
	.remove = qcom_cpufreq_hw_debug_remove,
	.driver = {
		.name = "qcom-cpufreq-hw-debug",
		.of_match_table = qcom_cpufreq_hw_debug_match,
	},
};

static int __init qcom_cpufreq_hw_debug_init(void)
{
	return platform_driver_register(&qcom_cpufreq_hw_debug);
}
fs_initcall(qcom_cpufreq_hw_debug_init);

static void __exit qcom_cpufreq_hw_debug_exit(void)
{
	return platform_driver_unregister(&qcom_cpufreq_hw_debug);
}
module_exit(qcom_cpufreq_hw_debug_exit);

MODULE_DESCRIPTION("QTI clock driver for CPUFREQ HW debug");
MODULE_LICENSE("GPL v2");
