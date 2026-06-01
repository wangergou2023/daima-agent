/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 *
 * */

#include "sample-common.h"
#include "sample-common-framesource.h"
#include "sample-common-encoder.h"
#include "sample-common-osd.h"

#define TAG "sample-IspOsd"

extern struct chn_conf chn[];
extern int gosd_enable;

int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;

    gosd_enable = 2;

	/* Step.1 System init */
	/* Step.1 系统初始化 */
	ret = sample_system_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System init failed\n");
		return -1;
	}

	/* Step.2 FrameSource init */
	/* Step.2 视频源初始化 */
	ret = sample_framesource_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource init failed\n");
		return -1;
	}

	/* Step.3 Encoder init */
	/* Step.3 编码器初始化 */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_Encoder_CreateGroup(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Encoder CreateGroup(%d) failed\n", chn[i].index);
				return -1;
			}
		}
	}

	ret = sample_video_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Video init failed\n");
		return -1;
	}

	/* Step.5 Bind */
	/* Step.5 绑定通道 */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Bind framesource%d and encoder%d failed\n", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
	}

	/* Step.6 ISP OSD init */
	/* Step.6 ISP OSD初始化 */
	for(i = 0; i < SENSOR_NUM; i++){
		ret = sample_isposd_init(i);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "isp osd init failed\n");
			return -1;
		}
	}

	/* Step.7 Stream On */
	/* Step.7 开启视频流 */
	ret = sample_framesource_streamon();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOn failed\n");
		return -1;
	}

	/* Step.8 Create OSD bgramap update thread */
	/* Step.8 创建OSD位图更新线程 */
	pthread_t tid;
	pthread_create(&tid, NULL, sample_isposd_draw, NULL);

	/* Step.9 Get stream */
	/* Step.9 获取视频流 */
	ret = sample_start_get_video_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Get video stream failed\n");
		return -1;
	}

	/* Step.10 Stream Off */
	/* Step.10 关闭视频流 */
	sample_stop_get_video_stream();

	/* Step.11 OSD exit */
	/* Step.11 OSD退出 */

	pthread_cancel(tid);
	pthread_join(tid, NULL);


	for(i = 0; i < SENSOR_NUM;i++){
		sample_isposd_exit(i);
	}
	free(timeStampData);

	/* Step.12 FrameSource off */
	/* Step.12 关闭出流 */
	ret = sample_framesource_streamoff();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOff failed\n");
		return -1;
	}

	/* Step.13 UnBind */
	/* Step.13 解除通道绑定 */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_System_UnBind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "UnBind framesource%d and encoder%d failed\n", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
	}

	/* Step.14 Encoder exit */
	/* Step.14 编码器退出 */
	ret = sample_video_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Video exit failed\n");
		return -1;
	}

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_Encoder_DestroyGroup(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Encoder DestroyGroup(%d) failed\n", chn[i].index);
				return -1;
			}
		}
	}

	/* Step.15 FrameSource exit */
	/* Step.15 视频源退出 */
	ret = sample_framesource_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource exit failed\n");
		return -1;
	}

	/* Step.16 System exit */
	/* Step.16 系统退出 */
	ret = sample_system_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System exit failed\n");
		return -1;
	}

	return 0;
}
