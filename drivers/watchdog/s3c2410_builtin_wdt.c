// SPDX-License-Identifier: GPL-2.0
/*
 * s3c2410_builtin_wdt.c
 *  Support s3c2410 WDT reset builtin APIs.
 *
 *  Before using API, Normal watchdog feature must be implemented
 *  by s3c2410_wdt.c, This driver only support direct WDT reset
 *  with setting s3c2410 WDT SFR directly.
 *
 * Copyright (c) 2021 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 */
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/sec_debug.h>

#include "s3c2410_wdt.h"

struct s3c2410_builtin_wdt {
	struct device		*dev;
	spinlock_t		lock;
	void __iomem		*reg_base;
};

static struct s3c2410_builtin_wdt *s3c_reset_wdt;

static ATOMIC_NOTIFIER_HEAD(s3c2410wdt_builtin_set_addr_notifier);

int s3c2410wdt_builtin_set_addr_register_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&s3c2410wdt_builtin_set_addr_notifier, nb);
}
EXPORT_SYMBOL_GPL(s3c2410wdt_builtin_set_addr_register_notifier);

static void do_s3c2410wdt_builtin_expire_watchdog(struct s3c2410_builtin_wdt *wdt)
{
	unsigned long wtcon;

	/* emergency reset with wdt reset */
	wtcon = readl(wdt->reg_base + S3C2410_WTCON);
	wtcon |= S3C2410_WTCON_RSTEN | S3C2410_WTCON_ENABLE;

	writel(0x00, wdt->reg_base + EXYNOS_WTMINCNT);
	writel(0x00, wdt->reg_base + S3C2410_WTDAT);
	writel(0x01, wdt->reg_base + S3C2410_WTCNT);
	writel(wtcon, wdt->reg_base + S3C2410_WTCON);
}

void s3c2410wdt_builtin_expire_watchdog_raw(void)
{
	struct s3c2410_builtin_wdt *wdt = s3c_reset_wdt;

	if (!wdt) {
		cpu_park_loop();
		return;
	}

#if IS_ENABLED(CONFIG_SEC_DEBUG)
	secdbg_base_built_wdd_set_emerg_addr(_RET_IP_);
#else
	atomic_notifier_call_chain(&s3c2410wdt_builtin_set_addr_notifier, 0, (void *)_RET_IP_);
#endif
	do_s3c2410wdt_builtin_expire_watchdog(wdt);
	cpu_park_loop();
}
EXPORT_SYMBOL_GPL(s3c2410wdt_builtin_expire_watchdog_raw);

extern void *return_address(unsigned int);

void s3c2410wdt_builtin_expire_watchdog(void)
{
	struct s3c2410_builtin_wdt *wdt = s3c_reset_wdt;
	unsigned long flags, addr;

	if (!wdt) {
		cpu_park_loop();
		return;
	}

	spin_lock_irqsave(&wdt->lock, flags);

	addr = (unsigned long)return_address(0);
	dev_emerg(wdt->dev, "Caller: %pS, Built-in WDTRESET right now!\n", (void *)addr);

	atomic_notifier_call_chain(&s3c2410wdt_builtin_set_addr_notifier, 0, (void *)_RET_IP_);
	do_s3c2410wdt_builtin_expire_watchdog(wdt);

	spin_unlock_irqrestore(&wdt->lock, flags);
	cpu_park_loop();
}
EXPORT_SYMBOL_GPL(s3c2410wdt_builtin_expire_watchdog);

static const struct of_device_id s3c2410_builtin_wdt_dt_ids[] = {
	 { .compatible = "samsung,s3c2410-builtin-wdt" },
	 { }
};

static int s3c2410_builtin_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct s3c2410_builtin_wdt *wdt;
	struct device_node *np;
	const struct of_device_id *match;
	int ret = 0;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	/*
	 * devm_platform_ioremap_resource cannot be used here.
	 * Because normal WDT Driver module use it to handle kicking WDT
	 * and do emergency reset to support sudden WDT for modules.
	 * So use only of_iomap. With just writing values to SFR in WDT device,
	 * WDT reset will be triggered.
	 * But usually, it must be done after normal WDT module driver probed.
	 */
	np = of_find_matching_node_and_match(NULL,
					 s3c2410_builtin_wdt_dt_ids, &match);

	if (!np || !match)
		panic("%s, failed to find builtin-wdt node or match\n", __func__);

	/* get the memory region for the watchdog timer */
	wdt->reg_base = of_iomap(np, 0);
	if (IS_ERR(wdt->reg_base)) {
		ret = PTR_ERR(wdt->reg_base);
		return ret;
	}

	wdt->dev = dev;
	spin_lock_init(&wdt->lock);

	s3c_reset_wdt = wdt;

	dev_info(dev, "probe: done\n");

	return ret;
}

static struct platform_driver s3c2410_builtin_wdt_driver = {
	.probe = s3c2410_builtin_wdt_probe,
	.driver = {
		.name = "s3c2410_builtin_wdt_probe",
		.of_match_table	= s3c2410_builtin_wdt_dt_ids,
	},
};

builtin_platform_driver(s3c2410_builtin_wdt_driver);
