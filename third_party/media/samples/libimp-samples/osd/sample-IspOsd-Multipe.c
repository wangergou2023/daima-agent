/*
 * Copyright (C) 2025 Ingenic Semiconductor Co.,Ltd
 * 该sample 提供一个区域如何绘制多行osd的示例
 * */

#include "sample-common-osd.h"
#include "sample-common.h"
#include "sample-common-framesource.h"
#include "sample-common-encoder.h"

#define TAG "sample-OSD"

extern struct chn_conf chn[];
extern int direct_switch;
extern int gosd_enable;

int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;
	direct_switch = 0;

	gosd_enable = 3;
	if(SENSOR_NUM == IMPISP_TOTAL_ONE){
		chn[0].enable = 1;
		chn[1].enable = 0;
		chn[2].enable = 0;
		chn[3].enable = 0;
		chn[4].enable = 0;
		chn[5].enable = 0;
		chn[6].enable = 0;
		chn[7].enable = 0;
		chn[8].enable = 0;
		chn[9].enable = 0;
		chn[10].enable = 0;
		chn[11].enable = 0;
	}else if (SENSOR_NUM == IMPISP_TOTAL_TWO){
		chn[0].enable = 1;
		chn[1].enable = 0;
		chn[2].enable = 0;
		chn[3].enable = 1;
		chn[4].enable = 0;
		chn[5].enable = 0;
		chn[6].enable = 0;
		chn[7].enable = 0;
		chn[8].enable = 0;
		chn[9].enable = 0;
		chn[10].enable = 0;
		chn[11].enable = 0;
	} else if(SENSOR_NUM == IMPISP_TOTAL_THR) {
		chn[0].enable = 1;
		chn[1].enable = 0;
		chn[2].enable = 0;
		chn[3].enable = 1;
		chn[4].enable = 0;
		chn[5].enable = 0;
		chn[6].enable = 1;
		chn[7].enable = 0;
		chn[8].enable = 0;
		chn[9].enable = 0;
		chn[10].enable = 0;
		chn[11].enable = 0;
	} else if(SENSOR_NUM == IMPISP_TOTAL_FOU) {
		chn[0].enable = 1;
		chn[1].enable = 0;
		chn[2].enable = 0;
		chn[3].enable = 1;
		chn[4].enable = 0;
		chn[5].enable = 0;
		chn[6].enable = 1;
		chn[7].enable = 0;
		chn[8].enable = 0;
		chn[9].enable = 1;
		chn[10].enable = 0;
		chn[11].enable = 0;
	}

	for(i = 0;i < FS_CHN_NUM ;i++){
		grpNum[i] = i;
	}

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

	/* Step.4 isposd init */
	/* Step.5 isposd 初始化 */
	for(i = 0; i < SENSOR_NUM;i++){
		sample_isposd_multipe_init(i);
	}

	/* Step.5 Bind */
	/* Step.5 绑定通道 */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			 if(chn[i].index == 0 || chn[i].index == 3 || chn[i].index == 6 || chn[i].index == 9){
				ret = IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "Bind FrameSource channel%d and Encoder failed\n",i);
					return -1;
				}
			}
		}
	}

	/* Step.6 Stream On */
	/* Step.6 开启视频流 */
	ret = sample_framesource_streamon();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOn failed\n");
		return -1;
	}

	/* Step.7 isposd thread */
	/* Step.8 isposd 绘制线程 */
	pthread_t isposdtid;
	ret = pthread_create(&isposdtid, NULL, sample_isposd_multipe_draw, NULL);
	if (ret) {
		IMP_LOG_ERR(TAG, "update_thread_isposd create failed\n");
		return -1;
	}

	/* Step.8 Get stream */
	/* Step.8 获取视频流 */
	ret = sample_start_get_video_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Get video stream failed\n");
		return -1;
	}

	sample_stop_get_video_stream();

	pthread_cancel(isposdtid);
	pthread_join(isposdtid, NULL);


	/* Step.9 Stream Off */
	/* Step.9 关闭视频流 */
	ret = sample_framesource_streamoff();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOff failed\n");
		return -1;
	}

	/* Step.10 UnBind */
	/* Step.10 解除通道绑定 */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if(chn[i].enable){
			if(chn[i].index == 0 || chn[i].index == 3 || chn[i].index == 6 || chn[i].index == 9){
				ret = IMP_System_UnBind(&chn[i].framesource_chn, &chn[i].imp_encoder);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "UnBind FrameSource channel%d and Encoder failed\n",i);
					return -1;
				}
			}
		}
	}

	/* Step.10 isposd exit */
	/* Step.10 isposd 反初始化 */
	for(i = 0; i < SENSOR_NUM;i++){
		sample_isposd_multipe_exit(i);
	}

	/* Step.12 Encoder exit */
	/* Step.12 编码器退出 */
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

	/* Step.13 FrameSource exit */
	/* Step.13 视频源退出 */
	ret = sample_framesource_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource exit failed\n");
		return -1;
	}

	/* Step.14 System exit */
	/* Step.14 系统退出 */
	ret = sample_system_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System exit failed\n");
		return -1;
	}

	return 0;
}
