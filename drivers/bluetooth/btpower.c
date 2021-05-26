// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 */

/*
 * Bluetooth Power Switch Module
 * controls power to external Bluetooth device
 * with interface to power management device
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/uaccess.h>
#include <linux/btpower.h>
#include <linux/of_device.h>
#include <soc/qcom/cmd-db.h>

#if defined CONFIG_BT_SLIM_QCA6390 || \
	defined CONFIG_BT_SLIM_QCA6490 || \
	defined CONFIG_BTFM_SLIM_WCN3990
#include "btfm_slim.h"
#endif
#include <linux/fs.h>

#define PWR_SRC_NOT_AVAILABLE -2
#define DEFAULT_INVALID_VALUE -1
#define PWR_SRC_INIT_STATE_IDX 0

#define SW_CTRL_GPIO_MAX_RETRY_TIMES 5

enum power_src_pos {
	BT_RESET_GPIO = PWR_SRC_INIT_STATE_IDX,
	BT_SW_CTRL_GPIO,
	BT_VDD_AON_LDO,
	BT_VDD_DIG_LDO,
	BT_VDD_RFA1_LDO,
	BT_VDD_RFA2_LDO,
	BT_VDD_ASD_LDO,
	BT_VDD_XTAL_LDO,
	BT_VDD_PA_LDO,
	BT_VDD_CORE_LDO,
	BT_VDD_IO_LDO,
	BT_VDD_LDO,
	BT_VDD_RFA_0p8,
	BT_VDD_RFACMN,
	// these indexes GPIOs/regs value are fetched during crash.
	BT_RESET_GPIO_CURRENT,
	BT_SW_CTRL_GPIO_CURRENT,
	BT_VDD_AON_LDO_CURRENT,
	BT_VDD_DIG_LDO_CURRENT,
	BT_VDD_RFA1_LDO_CURRENT,
	BT_VDD_RFA2_LDO_CURRENT,
	BT_VDD_ASD_LDO_CURRENT,
	BT_VDD_XTAL_LDO_CURRENT,
	BT_VDD_PA_LDO_CURRENT,
	BT_VDD_CORE_LDO_CURRENT,
	BT_VDD_IO_LDO_CURRENT,
	BT_VDD_LDO_CURRENT,
	BT_VDD_RFA_0p8_CURRENT,
	BT_VDD_RFACMN_CURRENT
};

// Regulator structure for QCA6390 and QCA6490 BT SoC series
static struct bt_power_vreg_data bt_vregs_info_qca6x9x[] = {
	{NULL, "qcom,bt-vdd-io",      1800000, 1800000, 0, false, true,
		{BT_VDD_IO_LDO, BT_VDD_IO_LDO_CURRENT}},
	{NULL, "qcom,bt-vdd-aon",     950000,  950000,  0, false, true,
		{BT_VDD_AON_LDO, BT_VDD_AON_LDO_CURRENT}},
	{NULL, "qcom,bt-vdd-rfacmn",  950000,  950000,  0, false, true,
		{BT_VDD_RFACMN, BT_VDD_RFACMN_CURRENT}},
	/* BT_CX_MX */
	{NULL, "qcom,bt-vdd-dig",      950000,  952000,  0, false, true,
		{BT_VDD_DIG_LDO, BT_VDD_DIG_LDO_CURRENT}},
	{NULL, "qcom,bt-vdd-rfa-0p8",  950000,  952000,  0, false, true,
		{BT_VDD_RFA_0p8, BT_VDD_RFA_0p8_CURRENT}},
	{NULL, "qcom,bt-vdd-rfa1",     1900000, 1900000, 0, false, true,
		{BT_VDD_RFA1_LDO, BT_VDD_RFA1_LDO_CURRENT}},
	{NULL, "qcom,bt-vdd-rfa2",     1900000, 1900000, 0, false, true,
		{BT_VDD_RFA2_LDO, BT_VDD_RFA2_LDO_CURRENT}},
	{NULL, "qcom,bt-vdd-asd",      2800000, 2800000, 0, false, true,
		{BT_VDD_ASD_LDO, BT_VDD_ASD_LDO_CURRENT}},
};

// Regulator structure for WCN399x BT SoC series
static struct bt_power bt_vreg_info_wcn399x = {
	.compatible = "qcom,wcn3990",
	.vregs = (struct bt_power_vreg_data []) {
		{NULL, "qcom,bt-vdd-io",   1700000, 1900000, 0, false, false,
			{BT_VDD_IO_LDO, BT_VDD_IO_LDO_CURRENT}},
		{NULL, "qcom,bt-vdd-core", 1304000, 1304000, 0, false, false,
			{BT_VDD_CORE_LDO, BT_VDD_CORE_LDO_CURRENT}},
		{NULL, "qcom,bt-vdd-pa",   3000000, 3312000, 0, false, false,
			{BT_VDD_PA_LDO, BT_VDD_PA_LDO_CURRENT}},
		{NULL, "qcom,bt-vdd-xtal", 1700000, 1900000, 0, false, false,
			{BT_VDD_XTAL_LDO, BT_VDD_XTAL_LDO_CURRENT}},
	},
	.num_vregs = 4,
};

static struct bt_power bt_vreg_info_qca6390 = {
	.compatible = "qcom,qca6390",
	.vregs = bt_vregs_info_qca6x9x,
	.num_vregs = ARRAY_SIZE(bt_vregs_info_qca6x9x),
};

static struct bt_power bt_vreg_info_qca6490 = {
	.compatible = "qcom,qca6490",
	.vregs = bt_vregs_info_qca6x9x,
	.num_vregs = ARRAY_SIZE(bt_vregs_info_qca6x9x),
};

static const struct of_device_id bt_power_match_table[] = {
	{	.compatible = "qcom,qca6174" },
	{	.compatible = "qcom,wcn3990", .data = &bt_vreg_info_wcn399x},
	{	.compatible = "qcom,qca6390", .data = &bt_vreg_info_qca6390},
	{	.compatible = "qcom,qca6490", .data = &bt_vreg_info_qca6490},
	{},
};

static int bt_power_vreg_set(enum bt_power_modes mode);
static int btpower_get_tcs_table_info(struct platform_device *plat_dev,
			struct bluetooth_power_platform_data *bt_power_pdata);
static int btpower_enable_ipa_vreg(struct platform_device *plat_dev,
			struct bluetooth_power_platform_data *bt_power_pdata);

static int bt_power_src_status[BT_POWER_SRC_SIZE];
static struct bluetooth_power_platform_data *bt_power_pdata;
static struct platform_device *btpdev;
static bool previous;
static int pwr_state;
static struct class *bt_class;
static int bt_major;
static int soc_id;
static int sw_ctl_pin_rety_cnt;

static int bt_vreg_enable(struct bt_power_vreg_data *vreg)
{
	int rc = 0;

	pr_debug("%s: vreg_en for : %s\n", __func__, vreg->name);

	if (!vreg->is_enabled) {
		if ((vreg->min_vol != 0) && (vreg->max_vol != 0)) {
			rc = regulator_set_voltage(vreg->reg,
						vreg->min_vol,
						vreg->max_vol);
			if (rc < 0) {
				pr_err("%s: regulator_set_voltage(%s) failed rc=%d\n",
						__func__, vreg->name, rc);
				goto out;
			}
		}

		if (vreg->load_curr >= 0) {
			rc = regulator_set_load(vreg->reg,
					vreg->load_curr);
			if (rc < 0) {
				pr_err("%s: regulator_set_load(%s) failed rc=%d\n",
				__func__, vreg->name, rc);
				goto out;
			}
		}

		rc = regulator_enable(vreg->reg);
		if (rc < 0) {
			pr_err("regulator_enable(%s) failed. rc=%d\n",
					__func__, vreg->name, rc);
			goto out;
		}
		vreg->is_enabled = true;
	}
out:
	return rc;
}

static int bt_vreg_enable_retention(struct bt_power_vreg_data *vreg)
{
	int rc = 0;

	if (!vreg)
		return rc;

	pr_debug("%s: enable_retention for : %s\n", __func__, vreg->name);

	if ((vreg->is_enabled) && (vreg->is_retention_supp)) {
		if ((vreg->min_vol != 0) && (vreg->max_vol != 0)) {
			/* Set the min voltage to 0 */
			rc = regulator_set_voltage(vreg->reg, 0, vreg->max_vol);
			if (rc < 0) {
				pr_err("%s: regulator_set_voltage(%s) failed rc=%d\n",
				__func__, vreg->name, rc);
				goto out;
			}
		}
		if (vreg->load_curr >= 0) {
			rc = regulator_set_load(vreg->reg, 0);
			if (rc < 0) {
				pr_err("%s: regulator_set_load(%s) failed rc=%d\n",
				__func__, vreg->name, rc);
			}
		}
	}
out:
	return rc;
}

static int bt_vreg_disable(struct bt_power_vreg_data *vreg)
{
	int rc = 0;

	if (!vreg)
		return rc;

	pr_debug("%s for : %s\n", __func__, vreg->name);

	if (vreg->is_enabled) {
		rc = regulator_disable(vreg->reg);
		if (rc < 0) {
			pr_err("%s, regulator_disable(%s) failed. rc=%d\n",
					__func__, vreg->name, rc);
			goto out;
		}
		vreg->is_enabled = false;

		if ((vreg->min_vol != 0) && (vreg->max_vol != 0)) {
			/* Set the min voltage to 0 */
			rc = regulator_set_voltage(vreg->reg, 0,
					vreg->max_vol);
			if (rc < 0) {
				pr_err("%s: regulator_set_voltage(%s) failed rc=%d\n",
				__func__, vreg->name, rc);
				goto out;
			}
		}
		if (vreg->load_curr >= 0) {
			rc = regulator_set_load(vreg->reg, 0);
			if (rc < 0) {
				pr_err("%s: regulator_set_load(%s) failed rc=%d\n",
				__func__, vreg->name, rc);
			}
		}
	}
out:
	return rc;
}

static int bt_clk_enable(struct bt_power_clk_data *clk)
{
	int rc = 0;

	pr_debug("%s: %s\n", __func__, clk->name);

	/* Get the clock handle for vreg */
	if (!clk->clk || clk->is_enabled) {
		pr_err("%s: error - node: %p, clk->is_enabled:%d\n",
			__func__, clk->clk, clk->is_enabled);
		return -EINVAL;
	}

	rc = clk_prepare_enable(clk->clk);
	if (rc) {
		pr_err("%s: failed to enable %s, rc(%d)\n",
				__func__, clk->name, rc);
		return rc;
	}

	clk->is_enabled = true;
	return rc;
}

static int bt_clk_disable(struct bt_power_clk_data *clk)
{
	int rc = 0;

	pr_debug("%s: %s\n", __func__, clk->name);

	/* Get the clock handle for vreg */
	if (!clk->clk || !clk->is_enabled) {
		pr_err("%s: error - node: %p, clk->is_enabled:%d\n",
			__func__, clk->clk, clk->is_enabled);
		return -EINVAL;
	}
	clk_disable_unprepare(clk->clk);

	clk->is_enabled = false;
	return rc;
}

static int bt_configure_gpios(int on)
{
	int rc = 0;
	int bt_reset_gpio = bt_power_pdata->bt_gpio_sys_rst;
	int wl_reset_gpio = bt_power_pdata->wl_gpio_sys_rst;
	int bt_sw_ctrl_gpio  =  bt_power_pdata->bt_gpio_sw_ctrl;
	int bt_debug_gpio  =  bt_power_pdata->bt_gpio_debug;
	int assert_dbg_gpio = 0;

	if (on) {
		rc = gpio_request(bt_reset_gpio, "bt_sys_rst_n");
		if (rc) {
			pr_err("%s: unable to request gpio %d (%d)\n",
					__func__, bt_reset_gpio, rc);
			return rc;
		}

		rc = gpio_direction_output(bt_reset_gpio, 0);
		if (rc) {
			pr_err("%s: Unable to set direction\n", __func__);
			return rc;
		}
		bt_power_src_status[BT_RESET_GPIO] =
			gpio_get_value(bt_reset_gpio);
		msleep(50);
		pr_info("BTON:Turn Bt Off bt-reset-gpio(%d) value(%d)\n",
			bt_reset_gpio, gpio_get_value(bt_reset_gpio));
		if (bt_sw_ctrl_gpio >= 0) {
			pr_info("BTON:Turn Bt Off\n");
			bt_power_src_status[BT_SW_CTRL_GPIO] =
			gpio_get_value(bt_sw_ctrl_gpio);
			pr_info("bt-sw-ctrl-gpio(%d) value(%d)\n",
				bt_sw_ctrl_gpio,
				bt_power_src_status[BT_SW_CTRL_GPIO]);
		}
		if (wl_reset_gpio >= 0)
			pr_info("BTON:Turn Bt On wl-reset-gpio(%d) value(%d)\n",
				wl_reset_gpio, gpio_get_value(wl_reset_gpio));

		if ((wl_reset_gpio < 0) ||
			((wl_reset_gpio >= 0) && gpio_get_value(wl_reset_gpio))) {
			pr_info("%s: BTON: Asserting BT_EN\n", __func__);
			rc = gpio_direction_output(bt_reset_gpio, 1);
			if (rc) {
				pr_err("%s: Unable to set direction\n", __func__);
				return rc;
			}
			bt_power_src_status[BT_RESET_GPIO] =
				gpio_get_value(bt_reset_gpio);
		}
		if ((wl_reset_gpio >= 0) && (gpio_get_value(wl_reset_gpio) == 0)) {
			if (gpio_get_value(bt_reset_gpio)) {
				pr_info("%s: Wlan Off and BT On too close\n", __func__);
				pr_info("%s: reset BT_EN, enable it after delay\n", __func__);
				rc = gpio_direction_output(bt_reset_gpio, 0);
				if (rc) {
					pr_err("%s: Unable to set direction\n", __func__);
					return rc;
				}
				bt_power_src_status[BT_RESET_GPIO] =
					gpio_get_value(bt_reset_gpio);
			}
			pr_info("%s:add 100ms delay for AON output to fully discharge\n",
				 __func__);
			msleep(100);
			rc = gpio_direction_output(bt_reset_gpio, 1);
			if (rc) {
				pr_err("%s: Unable to set direction\n", __func__);
				return rc;
			}
			bt_power_src_status[BT_RESET_GPIO] =
				gpio_get_value(bt_reset_gpio);
		}
		msleep(50);
		/*  Check  if  SW_CTRL  is  asserted  */
		if  (bt_sw_ctrl_gpio  >=  0)  {
			rc  =  gpio_direction_input(bt_sw_ctrl_gpio);
			if  (rc)  {
				pr_err("%s:SWCTRL Dir Set Problem:%d\n",
					__func__, rc);
			}  else  if  (!gpio_get_value(bt_sw_ctrl_gpio))  {
				/* SW_CTRL not asserted, assert debug GPIO */
				if  (bt_debug_gpio  >=  0)
					assert_dbg_gpio = 1;
			}
		}
		if (assert_dbg_gpio) {
			rc  =  gpio_request(bt_debug_gpio, "bt_debug_n");
			if  (rc)  {
				pr_err("unable to request Debug Gpio\n");
			}  else  {
				rc = gpio_direction_output(bt_debug_gpio,  1);
				if (rc)
					pr_err("%s:Prob Set Debug-Gpio\n",
						__func__);
			}
		}
		pr_info("BTON:Turn Bt On bt-reset-gpio(%d) value(%d)\n",
			bt_reset_gpio, gpio_get_value(bt_reset_gpio));
		if (bt_sw_ctrl_gpio >= 0) {
			pr_info("BTON:Turn Bt On\n");
			bt_power_src_status[BT_SW_CTRL_GPIO] =
			gpio_get_value(bt_sw_ctrl_gpio);
			pr_info("bt-sw-ctrl-gpio(%d) value(%d)\n",
				bt_sw_ctrl_gpio,
				bt_power_src_status[BT_SW_CTRL_GPIO]);
		}

		while ((gpio_get_value(bt_sw_ctrl_gpio) == 0) && (sw_ctl_pin_rety_cnt > 0)) {
			sw_ctl_pin_rety_cnt--;
			pr_info("BTON:bt-sw-ctrol-gpio is 0, retry %d",
					SW_CTRL_GPIO_MAX_RETRY_TIMES - sw_ctl_pin_rety_cnt);

			rc = gpio_direction_output(bt_reset_gpio, 0);
			if (rc) {
				pr_err("Unable to set direction to 0\n");
				return rc;
			}
			msleep(50);
			pr_info("BTON:Switch reset pin to low, bt-reset-gpio(%d) value(%d)\n",
					bt_reset_gpio, gpio_get_value(bt_reset_gpio));
			if (bt_sw_ctrl_gpio >= 0) {
				pr_info("State: bt-sw-ctrl-gpio(%d) value(%d)",
						bt_sw_ctrl_gpio,
						gpio_get_value(bt_sw_ctrl_gpio));
			}

			rc = gpio_direction_output(bt_reset_gpio, 1);
			if (rc) {
				pr_err("Unable to set direction to 1\n");
				return rc;
			}
			msleep(50);
			pr_info("BTON:Switch reset pin to hight, bt-reset-gpio(%d) value(%d)\n",
					bt_reset_gpio, gpio_get_value(bt_reset_gpio));
			if (bt_sw_ctrl_gpio >= 0) {
				pr_info("State: bt-sw-ctrl-gpio(%d) value(%d)",
						bt_sw_ctrl_gpio,
						gpio_get_value(bt_sw_ctrl_gpio));
			}
		}
	} else {
		gpio_set_value(bt_reset_gpio, 0);
		msleep(100);
		pr_info("BT-OFF:bt-reset-gpio(%d) value(%d)\n",
			bt_reset_gpio, gpio_get_value(bt_reset_gpio));
		if (bt_sw_ctrl_gpio >= 0) {
			pr_info("BT-OFF:bt-sw-ctrl-gpio(%d) value(%d)\n",
				bt_sw_ctrl_gpio,
				gpio_get_value(bt_sw_ctrl_gpio));
		}
	}

	pr_info("%s: bt_gpio= %d on: %d\n", __func__, bt_reset_gpio, on);

	return rc;
}

static int bluetooth_power(int on)
{
	int rc = 0;

	pr_debug("%s: on: %d\n", __func__, on);

	if (on == 1) {
		rc = bt_power_vreg_set(BT_POWER_ENABLE);
		if (rc < 0) {
			pr_err("%s: bt_power regulators config failed\n",
				__func__);
			goto regulator_fail;
		}
		/* Parse dt_info and check if a target requires clock voting.
		 * Enable BT clock when BT is on and disable it when BT is off
		 */
		if (bt_power_pdata->bt_chip_clk) {
			rc = bt_clk_enable(bt_power_pdata->bt_chip_clk);
			if (rc < 0) {
				pr_err("%s: bt_power gpio config failed\n",
					__func__);
				goto clk_fail;
			}
		}
		if (bt_power_pdata->bt_gpio_sys_rst > 0) {
			bt_power_src_status[BT_RESET_GPIO] =
				DEFAULT_INVALID_VALUE;
			bt_power_src_status[BT_SW_CTRL_GPIO] =
				DEFAULT_INVALID_VALUE;
			sw_ctl_pin_rety_cnt = SW_CTRL_GPIO_MAX_RETRY_TIMES;
			rc = bt_configure_gpios(on);
			if (rc < 0) {
				pr_err("%s: bt_power gpio config failed\n",
					__func__);
				goto gpio_fail;
			}
		}
	} else if (on == 0) {
		// Power Off
		if (bt_power_pdata->bt_gpio_sys_rst > 0)
			bt_configure_gpios(on);
gpio_fail:
		if (bt_power_pdata->bt_gpio_sys_rst > 0)
			gpio_free(bt_power_pdata->bt_gpio_sys_rst);
		if (bt_power_pdata->wl_gpio_sys_rst > 0)
			gpio_free(bt_power_pdata->wl_gpio_sys_rst);
		if  (bt_power_pdata->bt_gpio_sw_ctrl  >  0)
			gpio_free(bt_power_pdata->bt_gpio_sw_ctrl);
		if  (bt_power_pdata->bt_gpio_debug  >  0)
			gpio_free(bt_power_pdata->bt_gpio_debug);
		if (bt_power_pdata->bt_chip_clk)
			bt_clk_disable(bt_power_pdata->bt_chip_clk);
clk_fail:
regulator_fail:
		bt_power_vreg_set(BT_POWER_DISABLE);
	} else if (on == 2) {
		/* Retention mode */
		bt_power_vreg_set(BT_POWER_RETENTION);
	} else {
		pr_err("%s: Invalid power mode: %d\n", __func__, on);
		rc = -1;
	}
	return rc;
}

static int bluetooth_toggle_radio(void *data, bool blocked)
{
	int ret = 0;
	int (*power_control)(int enable);

	power_control =
		((struct bluetooth_power_platform_data *)data)->bt_power_setup;

	if (previous != blocked)
		ret = (*power_control)(!blocked);
	if (!ret)
		previous = blocked;
	return ret;
}

static const struct rfkill_ops bluetooth_power_rfkill_ops = {
	.set_block = bluetooth_toggle_radio,
};

static ssize_t extldo_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return scnprintf(buf, 6, "false\n");
}

static DEVICE_ATTR_RO(extldo);

static int bluetooth_power_rfkill_probe(struct platform_device *pdev)
{
	struct rfkill *rfkill;
	int ret;

	rfkill = rfkill_alloc("bt_power", &pdev->dev, RFKILL_TYPE_BLUETOOTH,
						&bluetooth_power_rfkill_ops,
						pdev->dev.platform_data);

	if (!rfkill) {
		dev_err(&pdev->dev, "rfkill allocate failed\n");
		return -ENOMEM;
	}

	/* add file into rfkill0 to handle LDO27 */
	ret = device_create_file(&pdev->dev, &dev_attr_extldo);
	if (ret < 0)
		pr_err("%s: device create file error\n", __func__);

	/* force Bluetooth off during init to allow for user control */
	rfkill_init_sw_state(rfkill, 1);
	previous = true;

	ret = rfkill_register(rfkill);
	if (ret) {
		dev_err(&pdev->dev, "rfkill register failed=%d\n", ret);
		rfkill_destroy(rfkill);
		return ret;
	}

	platform_set_drvdata(pdev, rfkill);

	return 0;
}

static void bluetooth_power_rfkill_remove(struct platform_device *pdev)
{
	struct rfkill *rfkill;

	pr_debug("%s\n", __func__);

	rfkill = platform_get_drvdata(pdev);
	if (rfkill)
		rfkill_unregister(rfkill);
	rfkill_destroy(rfkill);
	platform_set_drvdata(pdev, NULL);
}

#define MAX_PROP_SIZE 32
static int bt_dt_parse_vreg_info(struct device *dev,
		struct bt_power_vreg_data *vreg_data)
{
	int len, ret = 0;
	const __be32 *prop;
	char prop_name[MAX_PROP_SIZE];
	struct bt_power_vreg_data *vreg = vreg_data;
	struct device_node *np = dev->of_node;
	const char *vreg_name = vreg_data->name;

	pr_debug("%s: vreg dev tree parse for %s\n", __func__, vreg_name);

	snprintf(prop_name, sizeof(prop_name), "%s-supply", vreg_name);
	if (of_parse_phandle(np, prop_name, 0)) {
		vreg->reg = regulator_get(dev, vreg_name);
		if (IS_ERR(vreg->reg)) {
			ret = PTR_ERR(vreg->reg);
			vreg->reg = NULL;
			pr_warn("%s: failed to get: %s error:%d\n", __func__,
				vreg_name, ret);
			return ret;
		}

		snprintf(prop_name, sizeof(prop_name), "%s-config", vreg->name);
		prop = of_get_property(dev->of_node, prop_name, &len);
		if (!prop || len != (4 * sizeof(__be32))) {
			pr_debug("%s: Property %s %s, use default\n",
				__func__, prop_name,
				prop ? "invalid format" : "doesn't exist");
		} else {
			vreg->min_vol = be32_to_cpup(&prop[0]);
			vreg->max_vol = be32_to_cpup(&prop[1]);
			vreg->load_curr = be32_to_cpup(&prop[2]);
			vreg->is_retention_supp = be32_to_cpup(&prop[3]);
		}

		pr_debug("%s: Got regulator: %s, min_vol: %u, max_vol: %u, load_curr: %u,is_retention_supp: %u\n",
			__func__, vreg->name, vreg->min_vol, vreg->max_vol,
			vreg->load_curr, vreg->is_retention_supp);
	} else {
		pr_info("%s: %s is not provided in device tree\n", __func__,
			vreg_name);
	}

	return ret;
}

static int bt_dt_parse_clk_info(struct device *dev,
		struct bt_power_clk_data **clk_data)
{
	int ret = -EINVAL;
	struct bt_power_clk_data *clk = NULL;
	struct device_node *np = dev->of_node;

	pr_debug("%s\n", __func__);

	*clk_data = NULL;
	if (of_parse_phandle(np, "clocks", 0)) {
		clk = devm_kzalloc(dev, sizeof(*clk), GFP_KERNEL);
		if (!clk) {
			ret = -ENOMEM;
			goto err;
		}

		/* Allocated 20 bytes size buffer for clock name string */
		clk->name = devm_kzalloc(dev, 20, GFP_KERNEL);

		/* Parse clock name from node */
		ret = of_property_read_string_index(np, "clock-names", 0,
				&(clk->name));
		if (ret < 0) {
			pr_err("%s: reading \"clock-names\" failed\n",
				__func__);
			return ret;
		}

		clk->clk = devm_clk_get(dev, clk->name);
		if (IS_ERR(clk->clk)) {
			ret = PTR_ERR(clk->clk);
			pr_err("%s: failed to get %s, ret (%d)\n",
				__func__, clk->name, ret);
			clk->clk = NULL;
			return ret;
		}

		*clk_data = clk;
	} else {
		pr_err("%s: clocks is not provided in device tree\n", __func__);
	}

err:
	return ret;
}

static int bt_power_vreg_get(struct platform_device *pdev)
{
	int num_vregs, i = 0, ret = 0;
	const struct bt_power *data;

	data = of_device_get_match_data(&pdev->dev);
	if (!data) {
		pr_err("%s: failed to get dev node\n", __func__);
		return -EINVAL;
	}

	memcpy(&bt_power_pdata->compatible, &data->compatible, MAX_PROP_SIZE);
	bt_power_pdata->vreg_info = data->vregs;
	num_vregs = bt_power_pdata->num_vregs = data->num_vregs;
	for (; i < num_vregs; i++) {
		ret = bt_dt_parse_vreg_info(&(pdev->dev),
					    &bt_power_pdata->vreg_info[i]);
		/* No point to go further if failed to get regulator handler */
		if (ret)
			break;
	}

	return ret;
}

static int bt_power_vreg_set(enum bt_power_modes mode)
{
	int num_vregs, i = 0, ret = 0;
	int log_indx;
	struct bt_power_vreg_data *vreg_info = NULL;

	num_vregs =  bt_power_pdata->num_vregs;
	if (mode == BT_POWER_ENABLE) {
		for (; i < num_vregs; i++) {
			vreg_info = &bt_power_pdata->vreg_info[i];
			log_indx = vreg_info->indx.init;
			if (vreg_info->reg) {
				bt_power_src_status[log_indx] =
					DEFAULT_INVALID_VALUE;
				ret = bt_vreg_enable(vreg_info);
				if (ret < 0)
					goto out;
				if (vreg_info->is_enabled) {
					bt_power_src_status[log_indx] =
						regulator_get_voltage(
							vreg_info->reg);
				}
			}
		}
	} else if (mode == BT_POWER_DISABLE) {
		for (; i < num_vregs; i++) {
			vreg_info = &bt_power_pdata->vreg_info[i];
			ret = bt_vreg_disable(vreg_info);
		}
	} else if (mode == BT_POWER_RETENTION) {
		for (; i < num_vregs; i++) {
			vreg_info = &bt_power_pdata->vreg_info[i];
			ret = bt_vreg_enable_retention(vreg_info);
		}
	} else {
		pr_err("%s: Invalid power mode: %d\n", __func__, mode);
		ret = -1;
	}

out:
	return ret;
}

static void bt_power_vreg_put(void)
{
	int i = 0;
	struct bt_power_vreg_data *vreg_info = NULL;
	int num_vregs = bt_power_pdata->num_vregs;

	for (; i < num_vregs; i++) {
		vreg_info = &bt_power_pdata->vreg_info[i];
		if (vreg_info->reg)
			regulator_put(vreg_info->reg);
	}
}


static int bt_power_populate_dt_pinfo(struct platform_device *pdev)
{
	int rc;

	pr_debug("%s\n", __func__);

	if (!bt_power_pdata)
		return -ENOMEM;

	if (pdev->dev.of_node) {
		rc = bt_power_vreg_get(pdev);
		if (rc)
			return rc;

		bt_power_pdata->bt_gpio_sys_rst =
			of_get_named_gpio(pdev->dev.of_node,
						"qcom,bt-reset-gpio", 0);
		if (bt_power_pdata->bt_gpio_sys_rst < 0)
			pr_warn("bt-reset-gpio not provided in devicetree\n");

		bt_power_pdata->wl_gpio_sys_rst =
			of_get_named_gpio(pdev->dev.of_node,
						"qcom,wl-reset-gpio", 0);
		if (bt_power_pdata->wl_gpio_sys_rst < 0)
			pr_err("%s: wl-reset-gpio not provided in device tree\n",
				__func__);


		bt_power_pdata->bt_gpio_sw_ctrl  =
			of_get_named_gpio(pdev->dev.of_node,
						"qcom,bt-sw-ctrl-gpio",  0);
		if (bt_power_pdata->bt_gpio_sw_ctrl < 0)
			pr_warn("bt-sw-ctrl-gpio not provided in devicetree\n");

		bt_power_pdata->bt_gpio_debug  =
			of_get_named_gpio(pdev->dev.of_node,
						"qcom,bt-debug-gpio",  0);
		if (bt_power_pdata->bt_gpio_debug < 0)
			pr_warn("bt-debug-gpio not provided in devicetree\n");

		rc = bt_dt_parse_clk_info(&pdev->dev,
					&bt_power_pdata->bt_chip_clk);
		if (rc < 0)
			pr_warn("%s: clock not provided in device tree\n",
				__func__);
	}

	bt_power_pdata->bt_power_setup = bluetooth_power;

	return 0;
}

static int bt_power_probe(struct platform_device *pdev)
{
	int ret = 0;
	int itr;

	pr_debug("%s\n", __func__);

	/* Fill whole array with -2 i.e NOT_AVAILABLE state by default
	 * for any GPIO or Reg handle.
	 */
	for (itr = PWR_SRC_INIT_STATE_IDX;
		itr < BT_POWER_SRC_SIZE; ++itr)
		bt_power_src_status[itr] = PWR_SRC_NOT_AVAILABLE;

	bt_power_pdata = kzalloc(sizeof(*bt_power_pdata), GFP_KERNEL);

	if (!bt_power_pdata)
		return -ENOMEM;

	if (pdev->dev.of_node) {
		ret = bt_power_populate_dt_pinfo(pdev);
		if (ret < 0) {
			pr_err("%s, Failed to populate device tree info\n",
				__func__);
			goto free_pdata;
		}
		pdev->dev.platform_data = bt_power_pdata;
	} else if (pdev->dev.platform_data) {
		/* Optional data set to default if not provided */
		if (!((struct bluetooth_power_platform_data *)
			(pdev->dev.platform_data))->bt_power_setup)
			((struct bluetooth_power_platform_data *)
				(pdev->dev.platform_data))->bt_power_setup =
						bluetooth_power;

		memcpy(bt_power_pdata, pdev->dev.platform_data,
			sizeof(struct bluetooth_power_platform_data));
		pwr_state = 0;
	} else {
		pr_err("%s: Failed to get platform data\n", __func__);
		goto free_pdata;
	}

	if (bluetooth_power_rfkill_probe(pdev) < 0)
		goto free_pdata;

	btpdev = pdev;
	if (btpower_get_tcs_table_info(pdev, bt_power_pdata) < 0)
		pr_err("%s: Failed to get TCS table info\n", __func__);

	return 0;

free_pdata:
	kfree(bt_power_pdata);
	return ret;
}

static int bt_power_remove(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "%s\n", __func__);

	bluetooth_power_rfkill_remove(pdev);
	bt_power_vreg_put();

	kfree(bt_power_pdata);

	return 0;
}

int btpower_register_slimdev(struct device *dev)
{
	pr_debug("%s\n", __func__);
	if (!bt_power_pdata || (dev == NULL)) {
		pr_err("%s: Failed to allocate memory\n", __func__);
		return -EINVAL;
	}
	bt_power_pdata->slim_dev = dev;
	return 0;
}
EXPORT_SYMBOL(btpower_register_slimdev);

int btpower_get_chipset_version(void)
{
	pr_debug("%s\n", __func__);
	return soc_id;
}
EXPORT_SYMBOL(btpower_get_chipset_version);

static void  set_pwr_srcs_status(struct bt_power_vreg_data *handle)
{
	int ldo_index;

	if (handle) {
		ldo_index = handle->indx.crash;
		bt_power_src_status[ldo_index] =
			DEFAULT_INVALID_VALUE;
		if (handle->is_enabled &&
			(regulator_is_enabled(handle->reg))) {
			bt_power_src_status[ldo_index] =
				(int)regulator_get_voltage(handle->reg);
			pr_err("%s(%d) value(%d)\n", handle->name,
				handle, bt_power_src_status[ldo_index]);
		} else {
			pr_err("%s:%s is_enabled: %d\n",
				__func__, handle->name,
				handle->is_enabled);
		}
	}
}

static void  set_gpios_srcs_status(char *gpio_name,
		int gpio_index, int handle)
{
	if (handle >= 0) {
		bt_power_src_status[gpio_index] =
			DEFAULT_INVALID_VALUE;
		bt_power_src_status[gpio_index] =
			gpio_get_value(handle);
		pr_err("%s(%d) value(%d)\n", gpio_name,
			handle, bt_power_src_status[gpio_index]);
	} else {
		pr_err("%s: %s not configured\n",
			__func__, gpio_name);
	}
}

static long bt_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0, pwr_cntrl = 0;
	int chipset_version = 0;
	int itr, num_vregs;
	struct bt_power_vreg_data *vreg_info = NULL;

	switch (cmd) {
	case BT_CMD_SLIM_TEST:
#if (defined CONFIG_BT_SLIM_QCA6390 || \
	defined CONFIG_BT_SLIM_QCA6490 || \
	defined CONFIG_BTFM_SLIM_WCN3990)
		if (!bt_power_pdata->slim_dev) {
			pr_err("%s: slim_dev is null\n", __func__);
			return -EINVAL;
		}
		ret = btfm_slim_hw_init(
			bt_power_pdata->slim_dev->platform_data
		);
#endif
		break;
	case BT_CMD_PWR_CTRL:
		pwr_cntrl = (int)arg;
		pr_warn("%s: BT_CMD_PWR_CTRL pwr_cntrl: %d\n",
			__func__, pwr_cntrl);
		if (pwr_state != pwr_cntrl) {
			ret = bluetooth_power(pwr_cntrl);
			if (!ret)
				pwr_state = pwr_cntrl;
		} else {
			pr_err("%s: BT chip state is already: %d no change\n",
				__func__, pwr_state);
			ret = 0;
		}
		break;
	case BT_CMD_CHIPSET_VERS:
		chipset_version = (int)arg;
		pr_warn("%s: unified Current SOC Version : %x\n", __func__,
			chipset_version);
		if (chipset_version) {
			soc_id = chipset_version;
		} else {
			pr_err("%s: got invalid soc version\n", __func__);
			soc_id = 0;
		}
		break;
	case BT_CMD_GET_CHIPSET_ID:
		if (copy_to_user((void __user *)arg, bt_power_pdata->compatible,
		    MAX_PROP_SIZE)) {
			pr_err("%s: copy to user failed\n", __func__);
			ret = -EFAULT;
		}
		break;
	case BT_CMD_CHECK_SW_CTRL:
		/*  Check  if  SW_CTRL  is  asserted  */
		pr_info("BT_CMD_CHECK_SW_CTRL\n");
		if (bt_power_pdata->bt_gpio_sw_ctrl > 0) {
			bt_power_src_status[BT_SW_CTRL_GPIO] =
				DEFAULT_INVALID_VALUE;
			ret  =  gpio_direction_input(
				bt_power_pdata->bt_gpio_sw_ctrl);
			if (ret) {
				pr_err("%s:gpio_direction_input api\n",
					 __func__);
				pr_err("%s:failed for SW_CTRL:%d\n",
					__func__, ret);
			} else {
				bt_power_src_status[BT_SW_CTRL_GPIO] =
					gpio_get_value(
					bt_power_pdata->bt_gpio_sw_ctrl);
				pr_info("bt-sw-ctrl-gpio(%d) value(%d)\n",
					bt_power_pdata->bt_gpio_sw_ctrl,
					bt_power_src_status[BT_SW_CTRL_GPIO]);
			}
		} else {
			pr_err("bt_gpio_sw_ctrl not configured\n");
			return -EINVAL;
		}
		break;
	case BT_CMD_GETVAL_POWER_SRCS:
		pr_err("BT_CMD_GETVAL_POWER_SRCS\n");
		set_gpios_srcs_status("BT_RESET_GPIO", BT_RESET_GPIO_CURRENT,
			bt_power_pdata->bt_gpio_sys_rst);
		set_gpios_srcs_status("SW_CTRL_GPIO", BT_SW_CTRL_GPIO_CURRENT,
			bt_power_pdata->bt_gpio_sw_ctrl);

		num_vregs =  bt_power_pdata->num_vregs;
		for (itr = 0; itr < num_vregs; itr++) {
			vreg_info = &bt_power_pdata->vreg_info[itr];
			set_pwr_srcs_status(vreg_info);
		}
		if (copy_to_user((void __user *)arg,
			bt_power_src_status, sizeof(bt_power_src_status))) {
			pr_err("%s: copy to user failed\n", __func__);
			ret = -EFAULT;
		}
		break;
	case BT_CMD_SET_IPA_TCS_INFO:
		pr_err("%s: BT_CMD_SET_IPA_TCS_INFO\n", __func__);
		btpower_enable_ipa_vreg(btpdev, bt_power_pdata);
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return ret;
}

static struct platform_driver bt_power_driver = {
	.probe = bt_power_probe,
	.remove = bt_power_remove,
	.driver = {
		.name = "bt_power",
		.of_match_table = bt_power_match_table,
	},
};

static const struct file_operations bt_dev_fops = {
	.unlocked_ioctl = bt_ioctl,
	.compat_ioctl = bt_ioctl,
};

static int __init bluetooth_power_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&bt_power_driver);
	if (ret) {
		pr_err("%s: platform_driver_register error: %d\n",
			__func__, ret);
		goto driver_err;
	}

	bt_major = register_chrdev(0, "bt", &bt_dev_fops);
	if (bt_major < 0) {
		pr_err("%s: failed to allocate char dev\n", __func__);
		ret = -1;
		goto chrdev_err;
	}

	bt_class = class_create(THIS_MODULE, "bt-dev");
	if (IS_ERR(bt_class)) {
		pr_err("%s: coudn't create class\n", __func__);
		ret = -1;
		goto class_err;
	}


	if (device_create(bt_class, NULL, MKDEV(bt_major, 0),
		NULL, "btpower") == NULL) {
		pr_err("%s: failed to allocate char dev\n", __func__);
		goto device_err;
	}
	return 0;

device_err:
	class_destroy(bt_class);
class_err:
	unregister_chrdev(bt_major, "bt");
chrdev_err:
	platform_driver_unregister(&bt_power_driver);
driver_err:
	return ret;
}

static int btpower_get_tcs_table_info(struct platform_device *dev,
			struct bluetooth_power_platform_data *bt_power_pdata)
{
	struct platform_device *plat_dev = dev;
	struct btpower_tcs_table_info *tcs_table_info =
			&bt_power_pdata->tcs_table_info;
	struct resource *res;
	resource_size_t addr_len;
	void __iomem *tcs_cmd_base_addr;
	int ret = -1;

	res = platform_get_resource_byname(plat_dev, IORESOURCE_MEM, "tcs_cmd");
	if (!res) {
		pr_err("No TCS CMD entry found in DTSI\n");
		goto out;
	}

	tcs_table_info->tcs_cmd_base_addr = res->start;
	addr_len = resource_size(res);
	pr_info("TCS CMD base address is %pa with length %pa\n",
		    &tcs_table_info->tcs_cmd_base_addr, &addr_len);

	tcs_cmd_base_addr = devm_ioremap(&plat_dev->dev, res->start, addr_len);
	if (!tcs_cmd_base_addr) {
		ret = -EINVAL;
		pr_err("Failed to map TCS CMD address, err = %d\n",
			    ret);
		goto out;
	}

	tcs_table_info->tcs_cmd_base_addr_io = tcs_cmd_base_addr;

	return 0;

out:
	return ret;
}

static int btpower_enable_ipa_vreg(struct platform_device *dev,
			struct bluetooth_power_platform_data *bt_power_pdata)
{
	struct platform_device *plat_dev = dev;
	struct btpower_tcs_table_info *tcs_table_info =
					&bt_power_pdata->tcs_table_info;
	u32 offset, addr_val, data_val;
	void __iomem *tcs_cmd;
	int ret = 0;

	if (!tcs_table_info->tcs_cmd_base_addr_io) {
		pr_err("TCS command not configured\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(plat_dev->dev.of_node,
					"qcom,tcs_offset_ipa",
					&offset);
	if (ret) {
		pr_err("iPA failed to configure\n");
		return -EINVAL;
	}
	tcs_cmd = tcs_table_info->tcs_cmd_base_addr_io + offset;
	addr_val = readl_relaxed(tcs_cmd);
	tcs_cmd += TCS_CMD_IO_ADDR_OFFSET;

	writel_relaxed(1, tcs_cmd);

	data_val = readl_relaxed(tcs_cmd);
	pr_info("Configure S3E TCS Addr for iPA: %x with Data: %d\n"
		, addr_val, data_val);
	return 0;
}

static void __exit bluetooth_power_exit(void)
{
	platform_driver_unregister(&bt_power_driver);
}

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MSM Bluetooth power control driver");

module_init(bluetooth_power_init);
module_exit(bluetooth_power_exit);
