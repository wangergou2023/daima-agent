/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 *
 * */

#include "sample-common.h"
#include "sample-common-framesource.h"
#include "sample-common-encoder.h"

#define TAG "sample-AutoZoom"

extern struct chn_conf chn[];
extern int direct_switch;
float zoom_test[] = {0.9, 0.8, 0.7, 0.6, 0.5};
IMPISPAutoZoom autozoom[4];
pthread_mutex_t g_mutex;

static int sample_res_get_video_stream();

int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;

	direct_switch = 0;

	/* Step.1 系统初始化 */
	/* Step.1 System init */
	ret = sample_system_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System init failed\n");
		return -1;
	}

	pthread_mutex_init(&g_mutex, NULL);
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

	ret = sample_video_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Video init failed\n");
		return -1;
	}

	/* Step.4 绑定 */
	/* Step.4 Bind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Bind framesource%d and encoder%d failed\n", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
	}

	memset(autozoom, 0, 4*sizeof(IMPISPAutoZoom));
	/* Step.5 开启数据流 */
	/* Step.5 Stream On */
	ret = sample_framesource_streamon();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOn failed\n");
		return -1;
	}

	/* Step.6 获取码流 */
	/* Step.6 Get stream */
	ret = sample_res_get_video_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "res get video stream failed\n");
		return -1;
	}

	/* Step.7 停止数据流 */
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
				IMP_LOG_ERR(TAG, "UnBind framesource%d and encoder%d failed\n", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
	}

	/* Step.9 Encoder反初始化 */
	/* Step.9 Encoder exit */
	ret = sample_video_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Video exit failed\n");
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
	pthread_mutex_destroy(&g_mutex);

	return 0;
}

static int save_stream(int fd, IMPEncoderStream *stream)
{
	int i = 0;
	int ret = 0;
	int nr_pack = stream->packCount;

	for (i = 0; i < nr_pack; i++) {
		ret = write(fd, (void *)stream->pack[i].virAddr, stream->pack[i].length);
		if (ret != stream->pack[i].length) {
			IMP_LOG_ERR(TAG, "stream write failed\n");
			return -1;
		}
	}
	return 0;
}

static void *res_get_video_stream(void *args)
{
	int i = 0;
	int ret = 0;
	int stream_fd = -1;
	int totalSaveCnt = 0;
	char stream_path[64] = { 0 };

	int val = (int)args;
	int chnNum = val & 0xffff;
	IMPPayloadType encType = (val >> 16) & 0xffff;

	ret = IMP_Encoder_StartRecvPic(chnNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_StartRecvPic(%d) failed\n", chnNum);
		return NULL;
	}

	sprintf(stream_path, "%s/stream-%d-%dx%d.%s", STREAM_FILE_PATH_PREFIX, chnNum,
			chn[chnNum].fs_chn_attr.picWidth, chn[chnNum].fs_chn_attr.picHeight,
			(encType == PT_H264) ? "h264" : "h265");

	IMP_LOG_DBG(TAG, "Video ChnNum=%d Open Stream file %s ", chnNum, stream_path);
	stream_fd = open(stream_path, O_RDWR | O_CREAT | O_TRUNC, 0777);
	if (stream_fd < 0) {
		IMP_LOG_ERR(TAG, "open %s failed\n", stream_path);
		return NULL;
	}
	IMP_LOG_DBG(TAG, "OK\n");
	totalSaveCnt = NR_FRAMES_TO_SAVE;

	for (i = 0; i < totalSaveCnt; i++) {
		if((i != 0) && (i % 40 == 0))
		{
			pthread_mutex_lock(&g_mutex);
			ret = IMP_ISP_Tuning_GetAutoZoom(IMPVI_MAIN, &autozoom[0]);
			autozoom[0].zoom_chx_en[chnNum%3] = 1;
			autozoom[0].zoom_left[chnNum%3] = 0;
			autozoom[0].zoom_top[chnNum%3] = 0;
			autozoom[0].zoom_width[chnNum%3] = ((int)(chn[chnNum].fs_chn_attr.picWidth * zoom_test[i/40-1]))&(~1);
			autozoom[0].zoom_height[chnNum%3] = ((int)(chn[chnNum].fs_chn_attr.picHeight * zoom_test[i/40-1]))&(~1);
			printf("IMP_ISP_Tuning_SetAutoZoom, ret = %d, multiple = %f: autozoom.zoom_width[%d] = %d, autozoom.zoom_height[%d] = %d,chn:%d\n",
					ret, zoom_test[i/40-1],chnNum, autozoom[0].zoom_width[chnNum%3], chnNum, autozoom[0].zoom_height[chnNum%3],chnNum);

			ret = IMP_ISP_Tuning_SetAutoZoom(IMPVI_MAIN, &autozoom[0]);
			if(SENSOR_NUM > IMPISP_TOTAL_ONE){
				ret = IMP_ISP_Tuning_GetAutoZoom(IMPVI_SEC, &autozoom[1]);
				autozoom[1].zoom_chx_en[chnNum%3] = 1;
				autozoom[1].zoom_left[chnNum%3] = 0;
				autozoom[1].zoom_top[chnNum%3] = 0;
				autozoom[1].zoom_width[chnNum%3] = ((int)(chn[chnNum].fs_chn_attr.picWidth * zoom_test[i/40-1]))&(~1);
				autozoom[1].zoom_height[chnNum%3] = ((int)(chn[chnNum].fs_chn_attr.picHeight * zoom_test[i/40-1]))&(~1);

				ret = IMP_ISP_Tuning_SetAutoZoom(IMPVI_SEC, &autozoom[1]);
				printf("IMP_ISP_Tuning_SetAutoZoom TWO, ret = %d, multiple = %f: autozoom.zoom_width[%d] = %d, autozoom.zoom_height[%d] = %d,chn:%d\n",
						ret, zoom_test[i/40-1], chnNum, autozoom[1].zoom_width[chnNum%3], chnNum, autozoom[1].zoom_height[chnNum%3],chnNum);
			}

			if(SENSOR_NUM > IMPISP_TOTAL_TWO){
				ret = IMP_ISP_Tuning_GetAutoZoom(IMPVI_THR, &autozoom[2]);
				autozoom[2].zoom_chx_en[chnNum%3] = 1;
				autozoom[2].zoom_left[chnNum%3] = 0;
				autozoom[2].zoom_top[chnNum%3] = 0;
				autozoom[2].zoom_width[chnNum%3] = ((int)(chn[chnNum].fs_chn_attr.picWidth * zoom_test[i/40-1]))&(~1);
				autozoom[2].zoom_height[chnNum%3] = ((int)(chn[chnNum].fs_chn_attr.picHeight * zoom_test[i/40-1]))&(~1);

				ret = IMP_ISP_Tuning_SetAutoZoom(IMPVI_THR, &autozoom[2]);
				printf("IMP_ISP_Tuning_SetAutoZoom THR, ret = %d, multiple = %f: autozoom.zoom_width[%d] = %d, autozoom.zoom_height[%d] = %d,chn:%d\n",
						ret, zoom_test[i/40-1], chnNum, autozoom[2].zoom_width[chnNum%3], chnNum, autozoom[2].zoom_height[chnNum%3],chnNum);
			}

			if(SENSOR_NUM > IMPISP_TOTAL_THR){
				ret = IMP_ISP_Tuning_GetAutoZoom(IMPVI_FOUR, &autozoom[3]);
				autozoom[3].zoom_chx_en[chnNum%3] = 1;
				autozoom[3].zoom_left[chnNum%3] = 0;
				autozoom[3].zoom_top[chnNum%3] = 0;
				autozoom[3].zoom_width[chnNum%3] = ((int)(chn[chnNum].fs_chn_attr.picWidth * zoom_test[i/40-1]))&(~1);
				autozoom[3].zoom_height[chnNum%3] = ((int)(chn[chnNum].fs_chn_attr.picHeight * zoom_test[i/40-1]))&(~1);

				ret =  IMP_ISP_Tuning_SetAutoZoom(IMPVI_FOUR, &autozoom[3]);
				printf("IMP_ISP_Tuning_SetAutoZoom FOUR, ret = %d, multiple = %f: autozoom.zoom_width[%d] = %d, autozoom.zoom_height[%d] = %d,chn:%d\n",
						ret, zoom_test[i/40-1], chnNum, autozoom[3].zoom_width[chnNum%3], chnNum, autozoom[3].zoom_height[chnNum%3],chnNum);
			}
			pthread_mutex_unlock(&g_mutex);
		}

		ret = IMP_Encoder_PollingStream(chnNum, 1000);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_PollingStream(%d) i=%d timeout\n", chnNum, i);
			continue;
		}

		IMPEncoderStream stream;
		/* Get H264 or H265 Stream */
		ret = IMP_Encoder_GetStream(chnNum, &stream, 1);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_GetStream(%d) failed\n", chnNum);
			return NULL;
		}

		ret = save_stream(stream_fd, &stream);
		if (ret < 0) {
			close(stream_fd);
			return NULL;
		}

		IMP_Encoder_ReleaseStream(chnNum, &stream);
	}

	close(stream_fd);

	ret = IMP_Encoder_StopRecvPic(chnNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_StopRecvPic(%d) failed\n", chnNum);
		return NULL;
	}

	return NULL;
}

static int sample_res_get_video_stream()
{
	int i = 0;
	int ret = 0;
	pthread_t tid[FS_CHN_NUM];

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			int arg = (((chn[i].payloadType >> 24) << 16) | chn[i].index);
			ret = pthread_create(&tid[i], NULL, res_get_video_stream, (void *)arg);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Create Chn%d res_get_video_stream failed\n", chn[i].index);
			}
		}
	}

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			pthread_join(tid[i],NULL);
		}
	}

	return 0;
}
