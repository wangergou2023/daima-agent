/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 *
 */

#include "imp/imp_isp.h"
#include "sample-common-osd.h"
#include "sample-common.h"
#include "sample-common-framesource.h"
#include "sample-common-encoder.h"
#include <stdlib.h>

#define TAG "sample-OSD"
#define USE2BIT (1)
extern struct chn_conf chn[];
extern int direct_switch;
extern int gosd_enable;

int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;
	direct_switch = 0;

	gosd_enable = 3;
	const char* soc_name;
	soc_name = IMP_System_GetCPUInfo();
	if (strncmp(soc_name, "T32", 3) == 0) {
		g_buse2bit = USE2BIT;
	} else {
		g_buse2bit = !USE2BIT;
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

	/* Step.4 OSD init */
	/* Step.4 OSD初始化 */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable && (chn[i].index != 0) && (chn[i].index != 3) && (chn[i].index != 6) && (chn[i].index != 9)) {
			if (IMP_OSD_CreateGroup(grpNum[i]) < 0) {
				IMP_LOG_ERR(TAG, "OSD CreateGroup(%d) failed\n", grpNum[i]);
				return -1;
			}
		}
	}

	for (i = 0; i < FS_CHN_NUM; i++) {
		if(chn[i].enable){
			if((chn[i].index != 0) && (chn[i].index != 3) && (chn[i].index != 6) && (chn[i].index != 9)){
				sample_ipuosd_init(grpNum[i]);
			}
		}
	}

	for(i = 0; i < SENSOR_NUM; i++){
		sample_isposd_init(i);
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
			} else {
				IMPCell osd = { DEV_ID_OSD, grpNum[i], 0 };
				ret = IMP_System_Bind(&chn[i].framesource_chn, &osd);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "Bind framesource%d and osd%d failed\n", chn[i].framesource_chn.groupID, osd.groupID);
					return -1;
				}

				ret = IMP_System_Bind(&osd, &chn[i].imp_encoder);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "Bind osd%d and encoder%d failed\n", osd.groupID, chn[i].imp_encoder.groupID);
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

	/* Step.7 Create OSD bgramap update thread */
	/* Step.7 创建OSD位图更新线程 */
	pthread_t tid[FS_CHN_NUM];
	for (i = 0; i < FS_CHN_NUM; i++) {
		if(chn[i].enable) {
			if((chn[i].index != 0) && (chn[i].index != 3) && (chn[i].index != 6) && (chn[i].index != 9)){
				ret = pthread_create(&tid[i], NULL, sample_ipuosd_draw, (void *)i);
				if (ret) {
					IMP_LOG_ERR(TAG, "update_thread create failed\n");
					return -1;
				}
			}
		}
	}
	pthread_t isposdtid;
	ret = pthread_create(&isposdtid, NULL, sample_isposd_draw, NULL);
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


	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			if((chn[i].index != 0) && (chn[i].index != 3) && (chn[i].index != 6) && (chn[i].index != 9)){
				pthread_cancel(tid[i]);
				pthread_join(tid[i], NULL);
			}
		}
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
			} else {
				IMPCell osd = { DEV_ID_OSD, grpNum[i], 0 };
				ret = IMP_System_UnBind(&osd, &chn[i].imp_encoder);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "UnBind osd%d and encoder%d failed\n", osd.groupID, chn[i].imp_encoder.groupID);
					return -1;
				}

				ret = IMP_System_UnBind(&chn[i].framesource_chn, &osd);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "UnBind framesource%d and osd%d failed\n", chn[i].framesource_chn.groupID, osd.groupID);
					return -1;
				}
			}
		}
	}

	/* Step.11 OSD exit */
	/* Step.11 OSD退出 */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if(chn[i].enable){
			if((chn[i].index != 0) && (chn[i].index != 3) && (chn[i].index != 6) && (chn[i].index != 9)){
				ret = sample_ipuosd_exit(i);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "osd exit failed\n");
					return -1;
				}
			}
		}
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
