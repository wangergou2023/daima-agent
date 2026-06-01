/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#include "sample-common.h"
#include "sample-common-framesource.h"
#include "sample-common-encoder.h"

#define TAG "sample-Encoder-jpeg"

extern struct chn_conf chn[];
extern int direct_switch;

//#define JPEGQUAILTY_CHANGE
int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;

	/* Step.1 系统初始化 */
	/* Step.1 System init */
	ret = sample_system_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System init failed\n");
		return -1;
	}

	/* Step.2 FrameSource初始化 */
	/* Step.2 FrameSource init */
	ret = sample_framesource_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource init failed\n");
		return -1;
	}

	/* Step.3 Encoder初始化 */
	/* Step.3 Encoder init */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_Encoder_CreateGroup(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Encoder CreateGroup(%d) failed\n", chn[i].index);
				return -1;
			}
		}
	}

	ret = sample_jpeg_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Jpeg init failed\n");
		return -1;
	}

#ifdef JPEGQUAILTY_CHANGE
	int jpegQp = 20;
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			IMP_Encoder_SetJpegQp(12+chn[i].index/3, jpegQp);
		}
	}
#endif

	/* Step.4 绑定 */
	/* Step.4 Bind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Bind FrameSource%d and Encoder%d failed\n", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
	}

	/* Step.5 开启视频流 */
	/* Step.5 Stream On */
	ret = sample_framesource_streamon();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOn failed\n");
		return -1;
	}

	sleep(SLEEP_TIME);

	/* Step.6 获取码流和截图 */
	/* Step.6 Get Snap */
	ret = sample_start_get_jpeg_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Get Jpeg stream failed\n");
		return -1;
	}
	sample_stop_get_jpeg_stream();

	/* Step.7 切换码控模式 */
	/* Step.7 Stream Off */
	ret = sample_framesource_streamoff();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOff failed\n");
		return -1;
	}

	/* Step.8 解绑 */
	/* Step.8 UnBind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_System_UnBind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "UnBind FrameSource%d and Encoder%d failed\n", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
	}

	/* Step.9 Encoder反初始化 */
	/* Step.9 Encoder exit */
	ret = sample_jpeg_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Jpeg exit failed\n");
		return -1;
	}

	/* Step.10 FrameSource反初始化 */
	/* Step.10 FrameSource exit */
	ret = sample_framesource_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource exit failed\n");
		return -1;
	}

	/* Step.11 系统反初始化 */
	/* Step.11 System exit */
	ret = sample_system_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System exit failed\n");
		return -1;
	}

	return 0;
}
