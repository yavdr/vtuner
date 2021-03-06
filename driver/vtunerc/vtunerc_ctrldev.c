/*
 * vtunerc: /dev/vtunerc device
 *
 * Copyright (C) 2010-11 Honza Petrous <jpetrous@smartimp.cz>
 * [Created 2010-03-23]
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/delay.h>

#include <linux/time.h>
#include <linux/poll.h>

#include "vtunerc_priv.h"

#define VTUNERC_CTRLDEV_MAJOR	266
#define VTUNERC_CTRLDEV_NAME	"vtunerc"

#define VTUNER_MSG_LEN (sizeof(struct vtuner_message))

static ssize_t vtunerc_ctrldev_write(struct file *filp, const char *buff,
					size_t len, loff_t *off)
{
	struct vtunerc_ctx *ctx = filp->private_data;
	struct dvb_demux *demux = &ctx->demux;
	char *kernel_buf;
	int tailsize = len % 188;

	if (ctx->closing)
		return -EINTR;

	if (len < 188) {
		printk(KERN_ERR "vtunerc%d: Data are shorter then TS packet size (188B)\n",
				ctx->idx);
		return -EINVAL;
	}

	len -= tailsize;
        
	if (down_interruptible(&ctx->tswrite_sem))
		return -ERESTARTSYS;

        if ((ctx->buffer_ptr != NULL) && (ctx->buffer_len >= len))
        {
	  kernel_buf = ctx->buffer_ptr + (unsigned long)(buff - ctx->vm_start);
        }
        else
        {
//	  printk(KERN_ERR "vtunerc%d: no mmap\n", ctx->idx);
	  kernel_buf = kmalloc(len, GFP_KERNEL);

	  if (kernel_buf == NULL)
		  return -ENOMEM;

	  if (copy_from_user(kernel_buf, buff, len)) {
		  printk(KERN_ERR "vtunerc%d: userdata passing error\n",
			  ctx->idx);
		  up(&ctx->tswrite_sem);
		  return -EINVAL;
          }
	}

	if (ctx->config->tscheck) {
		int i;

	  	for (i = 0; i < len; i += 188)
			if (kernel_buf[i] != 0x47) { /* start of TS packet */
				printk(KERN_ERR "vtunerc%d: Data not start on packet boundary: index=%d data=%02x %02x %02x %02x %02x ...\n",
						ctx->idx, i / 188, kernel_buf[i], kernel_buf[i + 1],
						kernel_buf[i + 2], kernel_buf[i + 3], kernel_buf[i + 4]);
				up(&ctx->tswrite_sem);
				return -EINVAL;
			}
	}

	ctx->stat_wr_data += len;
	dvb_dmx_swfilter_packets(demux, kernel_buf, len / 188);

	up(&ctx->tswrite_sem);

#ifdef CONFIG_PROC_FS
	/* TODO:  analyze injected data for statistics */
#endif

        if (ctx->buffer_ptr == NULL)
	  kfree(kernel_buf);

	return len;
}

static ssize_t vtunerc_ctrldev_read(struct file *filp, char __user *buff,
		size_t len, loff_t *off)
{
	struct vtunerc_ctx *ctx = filp->private_data;

	ctx->stat_rd_data += len;

	/* read op is not using in current vtuner protocol */
	return 0 ;
}

static int vtunerc_ctrldev_open(struct inode *inode, struct file *filp)
{
	struct vtunerc_ctx *ctx;
	int minor;

	minor = MINOR(inode->i_rdev);
	ctx = filp->private_data = vtunerc_get_ctx(minor);
	if (ctx == NULL)
		return -ENOMEM;

	ctx->stat_ctrl_sess++;

	/*FIXME: clear pidtab */

	ctx->fd_opened++;
	ctx->closing = 0;

        ctx->buffer_ptr = NULL;
	ctx->buffer_len = 0;

	return 0;
}

static int vtunerc_ctrldev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct vtunerc_ctx *ctx = filp->private_data;
        int ret;
        long length = vma->vm_end - vma->vm_start;
	int i;

        /* allocate a memory area with kmalloc. */
        if ((ctx->buffer_ptr = kmalloc(length, GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "vtunerc%d: no memory\n", ctx->idx);
                return -ENOMEM;
        }


        /* mark the pages reserved */
        for (i = 0; i < length; i+= PAGE_SIZE) {
		SetPageReserved(virt_to_page(((unsigned long)ctx->buffer_ptr) + i));
        }

        /* map the whole physically contiguous area in one piece */
        if ((ret = remap_pfn_range(vma,
                                   vma->vm_start,
                                   virt_to_phys((void *)ctx->buffer_ptr) >> PAGE_SHIFT,
                                   length,
                                   vma->vm_page_prot)) < 0) {
                return ret;
        }

        ctx->buffer_len = length;
        ctx->vm_start = vma->vm_start;

	return 0;
}

static int vtunerc_ctrldev_close(struct inode *inode, struct file *filp)
{
	struct vtunerc_ctx *ctx = filp->private_data;
	int minor;
	struct vtuner_message fakemsg;
	int i;

	ctx->fd_opened--;
	ctx->closing = 1;

	minor = MINOR(inode->i_rdev);

	/* set FAKE response, to allow finish any waiters
	   in vtunerc_ctrldev_xchange_message() */
	ctx->ctrldev_response.type = 0;
	wake_up_interruptible(&ctx->ctrldev_wait_response_wq);

	/* clear pidtab */
	if (down_interruptible(&ctx->xchange_sem))
		return -ERESTARTSYS;
	memset(&fakemsg, 0, sizeof(fakemsg));
	vtunerc_ctrldev_xchange_message(ctx, &fakemsg, 0);
	up(&ctx->xchange_sem);

	if (ctx->buffer_ptr)
	{
		/* unreserve the pages */
		for (i = 0; i < ctx->buffer_len; i+= PAGE_SIZE) {
			SetPageReserved(virt_to_page(((unsigned long)ctx->buffer_ptr) + i));
		}
		/* free the memory areas */
		kfree(ctx->buffer_ptr);
	}

	return 0;
}

static long vtunerc_ctrldev_ioctl(struct file *file, unsigned int cmd,
					unsigned long arg)
{
	struct vtunerc_ctx *ctx = file->private_data;
	int len, ret = 0;
	int i;

	if (ctx->closing)
		return -EINTR;

	if (down_interruptible(&ctx->ioctl_sem))
		return -ERESTARTSYS;

	switch (cmd) {
	case VTUNER_SET_NAME:
		len = strlen((char *)arg) + 1;
		ctx->name = kmalloc(len, GFP_KERNEL);
		if (ctx->name == NULL) {
			printk(KERN_ERR "vtunerc%d: no memory\n", ctx->idx);
			ret = -ENOMEM;
			break;
		}
		if (copy_from_user(ctx->name, (char *)arg, len)) {
			ret = -EFAULT;
			break;
		}
		break;

	case VTUNER_SET_MODES:
		for (i = 0; i < ctx->num_modes; i++)
			ctx->ctypes[i] = &(((char *)(arg))[i*32]);
		if (ctx->num_modes != 1) {
			printk(KERN_ERR "vtunerc%d: currently supported only num_modes = 1!\n",
					ctx->idx);
			ret = -EINVAL;
			break;
		}
		/* follow into old code for compatibility */

	case VTUNER_SET_TYPE:
		if (strcasecmp((char *)arg, "DVB-S") == 0) {
			ctx->vtype = VT_S;
			printk(KERN_NOTICE "vtunerc%d: setting DVB-S tuner vtype\n",
					ctx->idx);
		} else
		if (strcasecmp((char *)arg, "DVB-S2") == 0) {
			ctx->vtype = VT_S2;
			printk(KERN_NOTICE "vtunerc%d: setting DVB-S2 tuner vtype\n",
					ctx->idx);
		} else
		if (strcasecmp((char *)arg, "DVB-T") == 0) {
			ctx->vtype = VT_T;
			printk(KERN_NOTICE "vtunerc%d: setting DVB-T tuner vtype\n",
					ctx->idx);
		} else
		if (strcasecmp((char *)arg, "DVB-C") == 0) {
			ctx->vtype = VT_C;
			printk(KERN_NOTICE "vtunerc%d: setting DVB-C tuner vtype\n",
					ctx->idx);
		} else {
			printk(KERN_ERR "vtunerc%d: unregognized tuner vtype '%s'\n",
					ctx->idx, (char *)arg);
			ret = -ENODEV;
			break;
		}

		if ((vtunerc_frontend_init(ctx))) {
			ctx->vtype = 0;
			printk(KERN_ERR "vtunerc%d: failed to initialize tuner's internals\n",
					ctx->idx);
			ret = -ENODEV;
			break;
		}

		break;


	case VTUNER_SET_FE_INFO:
		len = sizeof(struct dvb_frontend_info);
		ctx->feinfo = kmalloc(len, GFP_KERNEL);
		if (ctx->feinfo == NULL) {
			printk(KERN_ERR "vtunerc%d: no mem\n", ctx->idx);
			ret = -ENOMEM;
			break;
		}
		if (copy_from_user(ctx->feinfo, (char *)arg, len)) {
			ret = -EFAULT;
			break;
		}
		break;

	case VTUNER_GET_MESSAGE:
		if (wait_event_interruptible(ctx->ctrldev_wait_request_wq,
					ctx->ctrldev_request.type != -1)) {
			ret = -ERESTARTSYS;
			break;
		}

		BUG_ON(ctx->ctrldev_request.type == -1);

		if (copy_to_user((char *)arg, &ctx->ctrldev_request,
					VTUNER_MSG_LEN)) {
			ret = -EFAULT;
			break;
		}

		ctx->ctrldev_request.type = -1;

		if (ctx->noresponse)
			up(&ctx->xchange_sem);

		break;

	case VTUNER_SET_RESPONSE:
		if (copy_from_user(&ctx->ctrldev_response, (char *)arg,
					VTUNER_MSG_LEN)) {
			ret = -EFAULT;
		}
		wake_up_interruptible(&ctx->ctrldev_wait_response_wq);

		break;

	case VTUNER_SET_NUM_MODES:
		ctx->num_modes = (int) arg;
		break;

	default:
		printk(KERN_ERR "vtunerc%d: unknown IOCTL 0x%x\n", ctx->idx, cmd);
		ret = -ENOTTY; /* Linus: the only correct one return value for unsupported ioctl */

		break;
	}
	up(&ctx->ioctl_sem);

	return ret;
}

static unsigned int vtunerc_ctrldev_poll(struct file *filp, poll_table *wait)
{
	struct vtunerc_ctx *ctx = filp->private_data;
	unsigned int mask = 0;

	if (ctx->closing)
		return -EINTR;

	poll_wait(filp, &ctx->ctrldev_wait_request_wq, wait);

	if (ctx->ctrldev_request.type >= -1 ||
			ctx->ctrldev_response.type >= -1) {
		mask = POLLPRI;
	}

  return mask;
}

/* ------------------------------------------------ */

static const struct file_operations vtunerc_ctrldev_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = vtunerc_ctrldev_ioctl,
	.write = vtunerc_ctrldev_write,
	.read  = vtunerc_ctrldev_read,
	.poll  = (void *) vtunerc_ctrldev_poll,
	.open  = vtunerc_ctrldev_open,
	.release  = vtunerc_ctrldev_close,
        .mmap = vtunerc_ctrldev_mmap
};

static struct class *pclass;
static struct cdev cdev;
static dev_t chdev;

int vtunerc_register_ctrldev(struct vtunerc_ctx *ctx)
{
	int idx;

	chdev = MKDEV(VTUNERC_CTRLDEV_MAJOR, 0);

	if (register_chrdev_region(chdev, ctx->config->devices, VTUNERC_CTRLDEV_NAME)) {
		printk(KERN_ERR "vtunerc%d: unable to get major %d\n",
				ctx->idx, VTUNERC_CTRLDEV_MAJOR);
		return -EINVAL;
	}

	cdev_init(&cdev, &vtunerc_ctrldev_fops);

	cdev.owner = THIS_MODULE;
	cdev.ops = &vtunerc_ctrldev_fops;

	if (cdev_add(&cdev, chdev, ctx->config->devices) < 0)
		printk(KERN_WARNING "vtunerc%d: unable to create dev\n",
				ctx->idx);

	pclass = class_create(THIS_MODULE, "vtuner");
	if (IS_ERR(pclass)) {
		printk(KERN_ERR "vtunerc%d: unable to register major %d\n",
				ctx->idx, VTUNERC_CTRLDEV_MAJOR);
		return PTR_ERR(pclass);
	}

	for (idx = 0; idx < ctx->config->devices; idx++) {
		struct device *clsdev;

		clsdev = device_create(pclass, NULL,
				MKDEV(VTUNERC_CTRLDEV_MAJOR, idx),
				/*ctx*/ NULL, "vtunerc%d", idx);

		printk(KERN_NOTICE "vtunerc: registered /dev/vtunerc%d\n",
				idx);
	}

	return 0;
}

void vtunerc_unregister_ctrldev(struct vtunerc_config *config)
{
	int idx;

	printk(KERN_NOTICE "vtunerc: unregistering\n");

	unregister_chrdev_region(chdev, config->devices);

	for (idx = 0; idx < config->devices; idx++)
		device_destroy(pclass, MKDEV(VTUNERC_CTRLDEV_MAJOR, idx));

	cdev_del(&cdev);

	class_destroy(pclass);
}


int vtunerc_ctrldev_xchange_message(struct vtunerc_ctx *ctx,
		struct vtuner_message *msg, int wait4response)
{
	if (down_interruptible(&ctx->xchange_sem))
		return -ERESTARTSYS;

	if (ctx->fd_opened < 1) {
		up(&ctx->xchange_sem);
		return 0;
	}

#if 0
	BUG_ON(ctx->ctrldev_request.type != -1);
#else
	if(ctx->ctrldev_request.type != -1)
		printk(KERN_WARNING "orphan request type %d detected\n", ctx->ctrldev_request.type);

#endif

	memcpy(&ctx->ctrldev_request, msg, sizeof(struct vtuner_message));
	ctx->ctrldev_response.type = -1;
	ctx->noresponse = !wait4response;
	wake_up_interruptible(&ctx->ctrldev_wait_request_wq);

	if (!wait4response)
		return 0;

	if (wait_event_interruptible(ctx->ctrldev_wait_response_wq,
				ctx->ctrldev_response.type != -1)) {
		ctx->ctrldev_request.type = -1;
		up(&ctx->xchange_sem);
		return -ERESTARTSYS;
	}

	BUG_ON(ctx->ctrldev_response.type == -1);

	memcpy(msg, &ctx->ctrldev_response, sizeof(struct vtuner_message));
	ctx->ctrldev_response.type = -1;

	up(&ctx->xchange_sem);

	return 0;
}
