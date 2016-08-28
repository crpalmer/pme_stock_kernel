/* Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/diagchar.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/ratelimit.h>
#include "diagchar.h"
#include "diag_mux.h"
#include "diag_usb.h"
#include "diag_memorydevice.h"

struct diag_logger_t *logger;
static struct diag_logger_t usb_logger;
static struct diag_logger_t md_logger;

static struct diag_logger_ops usb_log_ops = {
	.open = diag_usb_connect_all,
	.close = diag_usb_disconnect_all,
	.queue_read = diag_usb_queue_read,
	.write = diag_usb_write,
	.close_peripheral = NULL
};

static struct diag_logger_ops md_log_ops = {
	.open = diag_md_open_all,
	.close = diag_md_close_all,
	.queue_read = NULL,
	.write = diag_md_write,
	.close_peripheral = diag_md_close_peripheral,
};

int diag_mux_init()
{
	logger = kzalloc(NUM_MUX_PROC * sizeof(struct diag_logger_t),
			 GFP_KERNEL);
	if (!logger)
		return -ENOMEM;
	kmemleak_not_leak(logger);

	usb_logger.mode = DIAG_USB_MODE;
	usb_logger.log_ops = &usb_log_ops;

	md_logger.mode = DIAG_MEMORY_DEVICE_MODE;
	md_logger.log_ops = &md_log_ops;
	diag_md_init();

	/*
	 * Set USB logging as the default logger. This is the mode
	 * Diag should be in when it initializes.
	 */
	logger = &usb_logger;
	return 0;
}

void diag_mux_exit()
{
	kfree(logger);
}

int diag_mux_register(int proc, int ctx, struct diag_mux_ops *ops)
{
	int err = 0;
	if (!ops)
		return -EINVAL;

	if (proc < 0 || proc >= NUM_MUX_PROC)
		return 0;

	/* Register with USB logger */
	usb_logger.ops[proc] = ops;
	err = diag_usb_register(proc, ctx, ops);
	if (err) {
		pr_err("diag: MUX: unable to register usb operations for proc: %d, err: %d\n",
		       proc, err);
		return err;
	}

	md_logger.ops[proc] = ops;
	err = diag_md_register(proc, ctx, ops);
	if (err) {
		pr_err("diag: MUX: unable to register md operations for proc: %d, err: %d\n",
		       proc, err);
		return err;
	}

	return 0;
}

int diag_mux_queue_read(int proc)
{
	if (proc < 0 || proc >= NUM_MUX_PROC)
		return -EINVAL;
	if (!logger)
		return -EIO;
	if (logger->log_ops && logger->log_ops->queue_read)
		return logger->log_ops->queue_read(proc);
	return 0;
}

int diag_mux_write(int proc, unsigned char *buf, int len, int ctx)
{
	if (proc < 0 || proc >= NUM_MUX_PROC)
		return -EINVAL;
	if (logger && logger->log_ops && logger->log_ops->write)
		return logger->log_ops->write(proc, buf, len, ctx);
	return 0;
}

int diag_mux_close_peripheral(int proc, uint8_t peripheral)
{
	if (proc < 0 || proc >= NUM_MUX_PROC)
		return -EINVAL;
	/* Peripheral should account for Apps data as well */
	if (peripheral > NUM_PERIPHERALS)
		return -EINVAL;
	if (logger && logger->log_ops && logger->log_ops->close_peripheral)
		return logger->log_ops->close_peripheral(proc, peripheral);
	return 0;
}

int diag_mux_switch_logging(int new_mode)
{
	struct diag_logger_t *new_logger = NULL;

	switch (new_mode) {
	case DIAG_USB_MODE:
/*++ 2015/07/14, USB Team, PCN00012 ++*/
		DIAG_INFO("sdlogging disable\n");
/*-- 2015/07/14, USB Team, PCN00012 --*/
		driver->qxdm2sd_drop = 1;	/*++ 2015/02/02, USB Team, PCN00002 ++*/

		new_logger = &usb_logger;
		break;
	case DIAG_MEMORY_DEVICE_MODE:
/*++ 2015/07/14, USB Team, PCN00012 ++*/
		DIAG_INFO("sdlogging enable\n");
/*-- 2015/07/14, USB Team, PCN00012 --*/
		driver->qxdm2sd_drop = 0;	/*++ 2015/02/02, USB Team, PCN00002 ++*/

		new_logger = &md_logger;
		break;
	default:
		pr_err("diag: Invalid mode %d in %s\n", new_mode, __func__);
		return -EINVAL;
	}

	if (logger) {
		logger->log_ops->close();
		logger = new_logger;
		logger->log_ops->open();
	}

	return 0;
}

