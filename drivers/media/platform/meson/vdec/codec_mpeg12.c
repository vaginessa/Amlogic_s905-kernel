// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Maxime Jourdan <maxi.jourdan@wanadoo.fr>
 */

#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "codec_mpeg12.h"
#include "codec_helpers.h"

#define SIZE_WORKSPACE	(2 * SZ_64K)
#define SIZE_CCBUF	(5 * SZ_1K)

/* DOS registers */
#define ASSIST_MBOX1_CLR_REG 0x01d4
#define ASSIST_MBOX1_MASK    0x01d8

#define PSCALE_CTRL 0x2444

#define MDEC_PIC_DC_CTRL   0x2638

#define AV_SCRATCH_0		0x2700
#define MREG_SEQ_INFO		0x2710
#define MREG_PIC_INFO		0x2714
#define MREG_PIC_WIDTH		0x2718
#define MREG_PIC_HEIGHT		0x271c
#define MREG_BUFFERIN		0x2720
#define MREG_BUFFEROUT		0x2724
#define MREG_CMD		0x2728
#define MREG_CO_MV_START	0x272c
#define MREG_ERROR_COUNT	0x2730
#define MREG_FRAME_OFFSET	0x2734
#define MREG_WAIT_BUFFER	0x2738
#define MREG_FATAL_ERROR	0x273c

#define MPEG1_2_REG	0x3004
#define PIC_HEAD_INFO	0x300c
#define POWER_CTL_VLD	0x3020
#define M4_CONTROL_REG	0x30a4

#define DOS_SW_RESET0 0xfc00

struct codec_mpeg12 {
	/* Buffer for the MPEG1/2 Workspace */
	void      *workspace_vaddr;
	dma_addr_t workspace_paddr;

	/* Housekeeping thread for marking buffers to DONE
	 * and recycling them into the hardware
	 */
	struct task_struct *buffers_thread;
};

static int codec_mpeg12_buffers_thread(void *data)
{
	struct vdec_buffer *tmp;
	struct vdec_session *sess = data;
	struct vdec_core *core = sess->core;;

	while (!kthread_should_stop()) {
		mutex_lock(&sess->bufs_recycle_lock);
		while (!list_empty(&sess->bufs_recycle) &&
		       !readl_relaxed(core->dos_base + MREG_BUFFERIN))
		{
			tmp = list_first_entry(&sess->bufs_recycle, struct vdec_buffer, list);

			/* Tell the decoder he can recycle this buffer */
			writel_relaxed(tmp->index + 1, core->dos_base + MREG_BUFFERIN);

			printk("Buffer %d recycled\n", tmp->index);

			list_del(&tmp->list);
			kfree(tmp);
		}
		mutex_unlock(&sess->bufs_recycle_lock);

		usleep_range(5000, 10000);
	}

	return 0;
}

static int codec_mpeg12_start(struct vdec_session *sess) {
	struct vdec_core *core = sess->core;
	struct codec_mpeg12 *mpeg12 = sess->priv;
	int ret;

	printk("codec_mpeg12_start\n");

	mpeg12 = kzalloc(sizeof(*mpeg12), GFP_KERNEL);
	if (!mpeg12)
		return -ENOMEM;

	sess->priv = mpeg12;

	/* Allocate some memory for the MPEG1/2 decoder's state */
	mpeg12->workspace_vaddr = dma_alloc_coherent(core->dev, SIZE_WORKSPACE, &mpeg12->workspace_paddr, GFP_KERNEL);
	if (!mpeg12->workspace_vaddr) {
		printk("Failed to request MPEG 1/2 Workspace\n");
		ret = -ENOMEM;
		goto free_mpeg12;
	}
	printk("Allocated Workspace: %08X - %08X\n", mpeg12->workspace_paddr, mpeg12->workspace_paddr + SIZE_WORKSPACE);

	writel_relaxed((1<<9) | (1<<8) | (1<<7) | (1<<6) | (1<<4), core->dos_base + DOS_SW_RESET0);
	writel_relaxed(0, core->dos_base + DOS_SW_RESET0);
	readl_relaxed(core->dos_base + DOS_SW_RESET0);

	writel_relaxed((1 << 4), core->dos_base + POWER_CTL_VLD);

	codec_helper_set_canvases(sess, core->dos_base + AV_SCRATCH_0);
	writel_relaxed(mpeg12->workspace_paddr + SIZE_CCBUF, core->dos_base + MREG_CO_MV_START);

	writel_relaxed(0, core->dos_base + MPEG1_2_REG);
	writel_relaxed(0, core->dos_base + PSCALE_CTRL);
	writel_relaxed(0x380, core->dos_base + PIC_HEAD_INFO);
	writel_relaxed(0, core->dos_base + M4_CONTROL_REG);
	writel_relaxed(1, core->dos_base + ASSIST_MBOX1_CLR_REG);
	writel_relaxed(0, core->dos_base + MREG_BUFFERIN);
	writel_relaxed(0, core->dos_base + MREG_BUFFEROUT);
	writel_relaxed((sess->width << 16) | sess->height, core->dos_base + MREG_CMD);
	writel_relaxed(0, core->dos_base + MREG_ERROR_COUNT);
	writel_relaxed(0, core->dos_base + MREG_FATAL_ERROR);
	writel_relaxed(0, core->dos_base + MREG_WAIT_BUFFER);

	/* Enable NV21 */
	writel_relaxed(readl_relaxed(core->dos_base + MDEC_PIC_DC_CTRL) | (1 << 17), core->dos_base + MDEC_PIC_DC_CTRL);

	mpeg12->buffers_thread = kthread_run(codec_mpeg12_buffers_thread, sess, "buffers_done");

	return 0;

free_mpeg12:
	kfree(mpeg12);
	return ret;
}

static int codec_mpeg12_stop(struct vdec_session *sess)
{
	struct codec_mpeg12 *mpeg12 = sess->priv;
	struct vdec_core *core = sess->core;

	printk("codec_mpeg12_stop\n");

	kthread_stop(mpeg12->buffers_thread);

	if (mpeg12->workspace_vaddr) {
		dma_free_coherent(core->dev, SIZE_WORKSPACE, mpeg12->workspace_vaddr, mpeg12->workspace_paddr);
		mpeg12->workspace_vaddr = 0;
	}

	kfree(mpeg12);
	sess->priv = 0;

	return 0;
}

static irqreturn_t codec_mpeg12_isr(struct vdec_session *sess)
{
	u32 reg;
	u32 buffer_index;
	struct vdec_core *core = sess->core;

	writel_relaxed(1, core->dos_base + ASSIST_MBOX1_CLR_REG);

	reg = readl_relaxed(core->dos_base + MREG_FATAL_ERROR);
	if (reg == 1)
		printk("MPEG12 fatal error\n");

	reg = readl_relaxed(core->dos_base + MREG_BUFFEROUT);
	//printk("codec_mpeg12_isr ; reg = %08X\n", reg);
	if (!reg)
		return IRQ_HANDLED;

	if ((reg >> 16) & 0xfe)
		goto end;

	buffer_index = ((reg & 0xf) - 1) & 7;
	vdec_dst_buf_done_idx(sess, buffer_index);

end:
	writel_relaxed(0, core->dos_base + MREG_BUFFEROUT);
	return IRQ_HANDLED;
}

struct vdec_codec_ops codec_mpeg12_ops = {
	.start = codec_mpeg12_start,
	.stop = codec_mpeg12_stop,
	.isr = codec_mpeg12_isr,
};

