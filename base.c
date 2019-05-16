/*
 * DAHDI device driver demo
 *
 * Written by QiLin.Liu <736432762@qq.com>
 *          
 *
 * Copyright (C) 2019-2029, QiLin.Liu, Inc.
 *
 * All rights reserved.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/kfifo.h>
#include <linux/reset.h>

#include <asm/io.h>
#include <dahdi/kernel.h>
#include <dahdi/wctdm_user.h>

#define debug() printk("  %s:%d \n",__FUNCTION__,__LINE__);

#define NUM_CARDS				1


struct h3_wctdm
{
struct platform_device * dev; //struct pci_dev *dev;

char *			variety;
struct dahdi_span span;
struct dahdi_device * ddev;

int 			usecount;
unsigned int	intcount;
int 			dead;
int 			pos;

struct dahdi_chan _chans[NUM_CARDS];
struct dahdi_chan * chans[NUM_CARDS];
};


struct h3_wctdm * wc = NULL;

static int h3_wctdm_hooksig(struct dahdi_chan *chan, enum dahdi_txsig txsig)
{
	debug();
	return 0;
}

static int h3_wctdm_open(struct dahdi_chan *chan)
{
	debug();
	return 0;
}

static int h3_wctdm_close(struct dahdi_chan *chan)
{
	debug();
	return 0;
}

static int h3_wctdm_ioctl(struct dahdi_chan *chan, unsigned int cmd, unsigned long data)
{
	debug();
	return 0;
}

static int h3_wctdm_watchdog(struct dahdi_span *span, int event)
{
	debug();
	return 0;
}


static const struct dahdi_span_ops wctdm_span_ops = {
	.owner = THIS_MODULE,
	.hooksig = h3_wctdm_hooksig,
	.open = h3_wctdm_open,
	.close = h3_wctdm_close,
	.ioctl = h3_wctdm_ioctl,
	.watchdog = h3_wctdm_watchdog,
};


static int h3_wctdm_initialize(struct h3_wctdm * wc)
{
	wc->ddev = dahdi_create_device();

	if (!wc->ddev)
		return - ENOMEM;

	/* DAHDI stuff */
	sprintf(wc->span.name, "WCTDM/%d", wc->pos);
	snprintf(wc->span.desc, sizeof(wc->span.desc) - 1, "%s Board %d", wc->variety, wc->pos + 1);
	wc->ddev->location = kasprintf(GFP_KERNEL, 
		"SPI Bus %02d Slot %02d", 
		1, 
		0);

	if (!wc->ddev->location)
	{
		dahdi_free_device(wc->ddev);
		wc->ddev = NULL;
		return - ENOMEM;
	}

	int x;
	wc->ddev->manufacturer = "OpenVox";
	wc->ddev->devicetype = wc->variety;

	wc->span.deflaw = DAHDI_LAW_MULAW;

	debug();
	for (x = 0; x < NUM_CARDS; x++)
	{
		sprintf(wc->chans[x]->name, "WCTDM/%d/%d", wc->pos, x);
		wc->chans[x]->sigcap =
			 DAHDI_SIG_FXOKS | DAHDI_SIG_FXOLS | DAHDI_SIG_FXOGS | DAHDI_SIG_SF | DAHDI_SIG_EM | DAHDI_SIG_CLEAR;
		wc->chans[x]->sigcap |= DAHDI_SIG_FXSKS | DAHDI_SIG_FXSLS | DAHDI_SIG_SF | DAHDI_SIG_CLEAR;
		wc->chans[x]->chanpos = x + 1;
		wc->chans[x]->pvt = wc;
	}

	wc->span.chans = wc->chans;
	wc->span.channels = NUM_CARDS;
	wc->span.flags = DAHDI_FLAG_RBS;
	wc->span.ops = &wctdm_span_ops;
	wc->span.spantype = SPANTYPE_ANALOG_MIXED;		// add spantype to current

	list_add_tail(&wc->span.device_node, &wc->ddev->spans);

	debug();
	if (dahdi_register_device(wc->ddev, &wc->dev->dev))
	{
		printk(KERN_NOTICE "Unable to register span with DAHDI\n");
		kfree(wc->ddev->location);
		dahdi_free_device(wc->ddev);
		wc->ddev = NULL;
		return - 1;
	}

	return 0;
}


static int h3_wctdm_hardware_init(struct h3_wctdm * wc)
{
	return 0;
}


static void h3_wctdm_release(struct h3_wctdm * wc)
{	
	dahdi_unregister_device(wc->ddev);	
	dahdi_free_device(wc->ddev);
	printk(KERN_INFO "h3_wctdm_release\n");
}


static int __devinit h3_wctdm_init_one(struct platform_device * pdev)
{
	printk("device init\n");

	int x;
	device_reset(&pdev->dev);
	wc	= kmalloc(sizeof(struct h3_wctdm), GFP_KERNEL);

	if (wc)
	{
		memset(wc, 0, sizeof(struct h3_wctdm));

		
		wc->dev = pdev;
		wc->pos = 0;
		wc->variety = "H3 WCTDM";

		for (x = 0; x < sizeof(wc->chans) / sizeof(wc->chans[0]); ++x)
		{ 
			wc->chans[x] = &wc->_chans[x];
		}

		debug();
		if (h3_wctdm_initialize(wc))
		{
			kfree(wc);
			printk(KERN_NOTICE "wctdm: Unable to intialize FXS\n");
			return - EIO;
		}

		debug();
		platform_set_drvdata(pdev, wc);

		debug();
		if (h3_wctdm_hardware_init(wc))
		{
			platform_set_drvdata(pdev, NULL);			
			dahdi_unregister_device(wc->ddev);
			kfree(wc->ddev->location);
			dahdi_free_device(wc->ddev);
			kfree(wc);

			printk(KERN_NOTICE "wctdm: Failed to intialize hardware\n");
			return - EIO;
		}
		debug();

	}


	return 0;
}


static int __devexit h3_wctdm_remove_one(struct platform_device * pdev)
{
	struct h3_wctdm * wc = platform_get_drvdata(pdev);

	if (wc)
	{
		if (!wc->usecount)
			h3_wctdm_release(wc);
		else 
			wc->dead = 1;
	}

	return 0;
}


static int h3_wctdm_suspend(struct platform_device * pdev, pm_message_t state)
{
	return - ENOSYS;
}


static const struct of_device_id tdm_of_match_table[] =
{
	{
		.compatible = "h3,h3wctdm"
	},
	{
	},
};


static


struct platform_driver h3_wctdm_driver =
{
	.probe = h3_wctdm_init_one, 
	.remove = __devexit_p(h3_wctdm_remove_one), 
	.suspend = h3_wctdm_suspend, 
	.driver = {
		.name = "h3-wctdm", 
		.owner = THIS_MODULE, 
		.of_match_table = tdm_of_match_table, 
		},
};


static int __init wctdm_init(void)
{
	int res = platform_driver_register(&h3_wctdm_driver);

	if (res)
		return - ENODEV;

	return 0;
}


static void __exit wctdm_cleanup(void)
{
	platform_driver_unregister(&h3_wctdm_driver);
}


MODULE_DESCRIPTION("MINITEL Dahdi Driver");
MODULE_AUTHOR("qilin.liu <736432762@qq.com>");
MODULE_ALIAS("h3-Dahdi");
MODULE_LICENSE("GPL v2");

module_init(wctdm_init);
module_exit(wctdm_cleanup);

