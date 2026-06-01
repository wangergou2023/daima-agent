/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 *
 * */

#include "sample-common.h"
#include "sample-common-framesource.h"

#define TAG "sample-Isp-InternalChn"

extern struct chn_conf chn[];

int main(int argc, char *argv[])
{
	int ret = 0;
	IMPISPInternalChnAttr attr;

	/* Step.1 系统初始化 */
	/* Step.1 System init */
	ret = sample_system_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System init failed\n");
		return -1;
	}

	/* Step.2 获取和设置internal属性 */
	/* Step.2 set or get internal atrr */
	IMP_ISP_GetInternalChnAttr(IMPVI_MAIN, &attr);

#if 0
	/* Before using the IR channel, check whether the Sensor supports it */
	attr.ch[1].type = TX_ISP_INTERNAL_CHANNEL_ISP_IR;
	chn[1].fs_chn_attr.pixFmt = PIX_FMT_RAW8;
	chn[1].fs_chn_attr.nrVBs = 2;
	chn[1].enable = 1;
#endif
#if 0
	attr.ch[1].type = TX_ISP_INTERNAL_CHANNEL_VIC_DMA0;
	attr.ch[1].vc_index = 0;
	chn[1].fs_chn_attr.pixFmt = PIX_FMT_RAW16;
	chn[1].fs_chn_attr.nrVBs = 2;
	chn[1].enable = 1;
#endif

#if 0
	attr.ch[2].type = TX_ISP_INTERNAL_CHANNEL_VIC_DMA1;
	attr.ch[2].vc_index = 1;
	chn[2].fs_chn_attr.pixFmt = PIX_FMT_RAW16;
	chn[2].fs_chn_attr.nrVBs = 2;
	chn[2].enable = 1;
#endif

	IMP_ISP_SetInternalChnAttr(IMPVI_MAIN, &attr);

	/* Step.3 FrameSource初始化 */
	/* Step.3 FrameSource init */
	ret = sample_framesource_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource init failed\n");
		return -1;
	}

	/* Step.4 开启数据流 */
	/* Step.4 Stream On */
	ret = sample_framesource_streamon();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOn failed\n");
		return -1;
	}

	/* Step.5 获取YUV数据 */
	/* Step.5 Get frame */
	ret = sample_get_frame();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "get frame failed\n");
		return -1;
	}

	/* Step.6 关闭数据流 */
	/* Step.6 Stream Off */
	ret = sample_framesource_streamoff();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOff failed\n");
		return -1;
	}

	/* Step.7 FrameSource反初始化 */
	/* Step.7 FrameSource exit */
	ret = sample_framesource_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource exit failed\n");
		return -1;
	}

	/* Step.8 系统反初始化 */
	/* Step.8 System exit */
	ret = sample_system_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System exit failed\n");
		return -1;
	}

	return 0;
}
