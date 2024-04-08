// SPDX-License-Identifier: GPL-2.0+
/*
 *  Copyright (C) 2023 Synaptics Inc.
 */

#include <common.h>
#include <dm.h>
#include <errno.h>
#include <asm/gpio.h>
#include <miiphy.h>
#include <malloc.h>
#include <linux/delay.h>
#include "rtk_types.h"
#include "rtk_switch.h"
#include "port.h"
#include "rtl8367c_asicdrv_port.h"
#include "smi.h"

struct rtl8363nb_priv {
	struct udevice *dev;
	struct gpio_desc reset_gpio;
	struct gpio_desc enable_phy_gpio;
};

//#define RTL8363NB_USE_MII
#define RTL8363NB_USE_RGMII_2V5

static int
rtl8363nb_switch_init(void)
{
	rtk_api_ret_t rtk_ret;
	rtk_port_mac_ability_t rtk_macability;
	/* TODO: initialize realtek switch, and configure it to selected mode  */
	rtk_ret = rtk_switch_init();
	if (rtk_ret != RT_ERR_OK) {
		pr_err("rtk_switch_init() returned %d\n", (int)rtk_ret);
		return -1;
	}

	/* On RTL8365MB there is only one external link, and it is Ext1 */
#if defined(RTL8363NB_USE_MII) || defined(RTL8363NB_USE_RMII)
	/* RMII */
	rtk_macability.forcemode	= MAC_FORCE;
	rtk_macability.speed		= SPD_100M;
	rtk_macability.duplex		= FULL_DUPLEX;
	rtk_macability.link		= 1;
	rtk_macability.nway		= 0;
	rtk_macability.txpause		= 0;
	rtk_macability.rxpause		= 0;

#ifdef RTL8363NB_USE_RMII
	/* Force PHONE_PORT_ID at RMII MAC, 100M/Full */
	rtk_ret = rtk_port_macForceLinkExt_set(EXT_PORT0, MODE_EXT_RMII_MAC, &rtk_macability);
#else
	rtk_ret = rtk_port_macForceLinkExt_set(EXT_PORT0, MODE_EXT_MII_MAC, &rtk_macability);
#endif
	if (rtk_ret != RT_ERR_OK) {
		pr_err("rtk_port_macForceLinkExt_set() returned %d\n", (int)rtk_ret);
		return -1;
	}

#ifdef RTL8363NB_USE_MII
	/* Enable PHY (ignore EN_PHY strip pin) */
	rtk_port_phyEnableAll_set(ENABLED);
#endif

#else
	/* Standard RGMII */
	rtk_macability.forcemode	= MAC_FORCE;
	rtk_macability.speed		= SPD_1000M; /* or 10 or 100 */
	rtk_macability.duplex		= FULL_DUPLEX;
	rtk_macability.link		= PORT_LINKUP;
	rtk_macability.nway		= DISABLED;
	rtk_macability.txpause		= ENABLED;
	rtk_macability.rxpause		= ENABLED;

	rtk_ret = rtk_port_macForceLinkExt_set(EXT_PORT0, MODE_EXT_RGMII,
					       &rtk_macability);
	if (rtk_ret != RT_ERR_OK) {
		pr_err("rtk_port_macForceLinkExt_set() returned %d\n",
		       (int)rtk_ret);
		return -1;
	}

	rtk_port_phyEnableAll_set(ENABLED);
#endif

#ifdef RTL8363NB_USE_RGMII_2V5
	/* Set RGMII Interface 0 TX delay to 2ns and RX to step 0 */
	rtk_port_rgmiiDelayExt_set(EXT_PORT0, 1, 2);
#else
	/* Set RGMII Interface 0 TX delay to 2ns and RX to step 0 */
	rtk_port_rgmiiDelayExt_set(EXT_PORT0, 1, 0);
#endif
	mdelay(1000);

	return 0;
}

int
rtl83xx_smi_read(int phy_id, int regnum)
{
	unsigned short value;

	if (miiphy_read("eth_designware0", phy_id, regnum, &value))
		pr_err("error reading register %d\n", regnum);

	return value;
}

int
rtl83xx_smi_write(int phy_id, int regnum, unsigned short val)
{
	return miiphy_write("eth_designware0", phy_id, regnum, val);
}

static int rtl8363nb_probe(struct udevice *dev)
{
	struct rtl8363nb_priv *priv;
	int ret = 0;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

#ifdef CONFIG_DM_GPIO
	ret = gpio_request_by_name(dev, "reset_gpio", 0,
				   &priv->reset_gpio, GPIOD_IS_OUT);
	if (ret == 0 && dm_gpio_is_valid(&priv->reset_gpio)) {
		dm_gpio_set_value(&priv->reset_gpio, 0);
		mdelay(10);
		dm_gpio_set_value(&priv->reset_gpio, 1);
		mdelay(100);
	}

	ret = gpio_request_by_name(dev, "enable_phy_gpio", 0,
				   &priv->enable_phy_gpio, GPIOD_IS_OUT);
	if (ret == 0 && dm_gpio_is_valid(&priv->enable_phy_gpio)) {
		dm_gpio_set_value(&priv->enable_phy_gpio, 0);
		mdelay(10);
		dm_gpio_set_value(&priv->enable_phy_gpio, 1);
		mdelay(100);
	}
#endif

	(void)rtl8363nb_switch_init();

	/* return -1 for now to not registe a 2nd Ethernet device */
	return -1;
}

static const struct udevice_id rtl8363nb_ids[] = {
	{ .compatible = "dspg,rtl8363nb" },
	{ }
};

U_BOOT_DRIVER(rtl8363nb) = {
	.name	= "rtl8363nb",
	.id	= UCLASS_ETH,
	.of_match = rtl8363nb_ids,
	.probe	= rtl8363nb_probe,
	.priv_auto_alloc_size = sizeof(struct rtl8363nb_priv),
};
