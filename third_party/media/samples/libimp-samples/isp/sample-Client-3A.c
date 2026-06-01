/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#include "sample-common.h"
#include "sample-common-framesource.h"
#include "sample-common-encoder.h"

#define TAG "sample-client-3a"
//#define ENABLE_JPEG_ENCODE

extern struct chn_conf chn[];
extern void sample_set_client_3a(IMPISPAeAlgoFunc *client_ae, IMPISPAwbAlgoFunc *client_awb);
static pthread_t tid[FS_CHN_NUM*2];

static struct client_ae_private {
	uint32_t open;
	uint32_t change;
	uint32_t AeIntegrationTime;	/**< AE的曝光值 */
	uint32_t AeAGain;		/**< AE Sensor 模拟增益值，单位是倍数 x 1024 */
	uint32_t AeDGain;		/**< AE Sensor数字增益值，单位是倍数 x 1024 */
	uint32_t AeIspDGain;		/**< AE ISP 数字增益值，单位倍数 x 1024*/
	pthread_mutex_t ae_mutex;
} g_client_ae_data;

static int client_ae_open(void *priv_data, IMPISPAeInitAttr *AeInitAttr)
{
	IMPISPAEStatisAttr *statis = NULL;
	pthread_mutex_init(&g_client_ae_data.ae_mutex, NULL);
	printf("########### client ae open ###############\n");
	printf("sensor0: driver ae is %s\n", AeInitAttr->AeStatis.ae_sta_en ? "enable" : "disable");
	if(AeInitAttr->AeStatis.ae_sta_en){
		g_client_ae_data.open = 1;
		statis = &AeInitAttr->AeStatis;
		printf("fps:%d\n", AeInitAttr->fps);
		printf("AeMinIntegrationTime:%d\n", AeInitAttr->AeMinIntegrationTime);
		printf("AeMinAGain:%d\n", AeInitAttr->AeMinAGain);
		printf("AeMinDgain:%d\n", AeInitAttr->AeMinDgain);
		printf("AeMinIspDGain:%d\n", AeInitAttr->AeMinIspDGain);
		printf("AeMaxIntegrationTime:%d\n", AeInitAttr->AeMaxIntegrationTime);
		printf("AeMaxAGain:%d\n", AeInitAttr->AeMaxAGain);
		printf("AeMaxDgain:%d\n", AeInitAttr->AeMaxDgain);
		printf("AeMaxIspDGain:%d\n", AeInitAttr->AeMaxIspDGain);
		printf("local:start_h:%d, start_v:%d, node_h:%d, node_v:%d\n", statis->local.start_h,
				statis->local.start_v, statis->local.node_h, statis->local.node_v);
		printf("histThresh:%d, %d, %d, %d\n", statis->histThresh[0], statis->histThresh[1], statis->histThresh[2],
				statis->histThresh[3]);
	}
	return 0;
}

static void client_ae_close(void *priv_data)
{
	g_client_ae_data.open = 0;
	printf("########### client ae closed ###############\n");
}

static void client_ae_handle(void *priv_data, IMPISPAeInfo *AeInfo, IMPISPAeAttr *AeAttr)
{
	static uint32_t frame_cnt = 0;
	if(frame_cnt > 100 && frame_cnt < 105){
		printf("ae_hist_5bin sum is %d, %d, %d, %d, %d\n", AeInfo->ae_info.ae_hist_5bin[0],
				AeInfo->ae_info.ae_hist_5bin[1],
				AeInfo->ae_info.ae_hist_5bin[2],
				AeInfo->ae_info.ae_hist_5bin[3],
				AeInfo->ae_info.ae_hist_5bin[4]);
	}
	frame_cnt++;
	pthread_mutex_lock(&g_client_ae_data.ae_mutex);
	if(g_client_ae_data.change){
		AeAttr->AeIntegrationTime	= g_client_ae_data.AeIntegrationTime;
		AeAttr->AeAGain			= g_client_ae_data.AeAGain;
		AeAttr->AeDGain			= 1024;//g_client_ae_data.AeDGain;
		AeAttr->AeIspDGain		= g_client_ae_data.AeIspDGain;
		AeAttr->change = 1;
		g_client_ae_data.change = 0;

		frame_cnt = 0;
	}else{
		AeAttr->change = 0;
	}
	pthread_mutex_unlock(&g_client_ae_data.ae_mutex);
}

static IMPISPAeAlgoFunc client_ae_func = {
	.priv_data = NULL,
	.open = client_ae_open,
	.close = client_ae_close,
	.handle = client_ae_handle,
	.notify = NULL,
};


static struct client_awb_private {
	uint32_t open;
	uint32_t change;
	uint32_t r_gain;	/**< AWB参数 r_gain */
	uint32_t b_gain;	/**< AWB参数 b_gain */
	uint32_t ct;		/**< 当前色温 */
	uint32_t bright_min;
	uint32_t bright_max;
	pthread_mutex_t awb_mutex;
} g_client_awb_data;


static int client_awb_open(void *priv_data, IMPISPAwbInitAttr *AwbInitAttr)
{
	IMPISPAWBStatisAttr *statis;	/**< AWB的初始统计属性 */
	pthread_mutex_init(&g_client_awb_data.awb_mutex, NULL);
	printf("########### client awb open ###############\n");
	printf("sensor0: driver awb is %s\n", AwbInitAttr->AwbStatis.awb_sta_en ? "enable" : "disable");
	if(AwbInitAttr->AwbStatis.awb_sta_en){
		g_client_awb_data.open = 1;
		statis = &AwbInitAttr->AwbStatis;
		printf("local:start_h:%d, start_v:%d, node_h:%d, node_v:%d\n", statis->local.start_h,
				statis->local.start_v, statis->local.node_h, statis->local.node_v);
	}
	return 0;
}

static void client_awb_close(void *priv_data)
{
	g_client_awb_data.open = 0;
	printf("########### client awb closed ###############\n");
}

static void client_awb_handle(void *priv_data, IMPISPAwbInfo *AwbInfo, IMPISPAwbAttr *AwbAttr)
{
	static uint32_t frame_cnt = 0;
	if(frame_cnt > 100 && frame_cnt < 105){
		printf("cur_r_gain %d, cur_b_gain %d, r_statis %d, b_statis %d, min %d, mid %d, max %d\n",
				AwbInfo->cur_r_gain, AwbInfo->cur_b_gain, AwbInfo->r_gain_statis, AwbInfo->b_gain_statis,
				AwbInfo->bright_min, AwbInfo->bright_mid, AwbInfo->bright_max);
	}
	frame_cnt++;
	pthread_mutex_lock(&g_client_awb_data.awb_mutex);
	if(g_client_awb_data.change){
		AwbInfo->bright_min	= g_client_awb_data.bright_min;
		AwbInfo->bright_max	= g_client_awb_data.bright_max;
		AwbAttr->r_gain		= g_client_awb_data.r_gain;
		AwbAttr->b_gain		= g_client_awb_data.b_gain;
		AwbAttr->ct		= g_client_awb_data.ct;
		AwbAttr->change = 1;
		g_client_awb_data.change = 0;
		frame_cnt = 0;
	}else{
		AwbAttr->change = 0;
	}
	pthread_mutex_unlock(&g_client_awb_data.awb_mutex);
}

static IMPISPAwbAlgoFunc client_awb_func = {
	.priv_data = NULL,
	.open = client_awb_open,
	.close = client_awb_close,
	.handle = client_awb_handle,
	.notify	= NULL,
};

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

static void *get_video_stream(void *args)
{
	int i = 0;
	int ret = 0;
	int val = 0, chnNum = 0;

	val = (int)args;
	chnNum = val & 0xffff;
	char stream_path[64];
	char snap_path[64];
	IMPPayloadType payloadType;
	int stream_fd[FS_CHN_NUM];
	int s32picWidth = 0, s32picHeight = 0;
	payloadType = PT_H265;
	IMPEncoderStream stream;
	/*int totalSaveStreamCnt = NR_FRAMES_TO_SAVE;*/
	int totalSaveStreamCnt = 2000;
	char ask = 0;

	uint32_t ae_time;	/**< AE的曝光值 */
	uint32_t ae_again;	/**< AE Sensor 模拟增益值，单位是倍数 x 1024 */
	uint32_t ae_ispdgain;	/**< AE ISP 数字增益值，单位倍数 x 1024*/
	uint32_t awb_r_gain;	/**< AWB参数 r_gain */
	uint32_t awb_b_gain;	/**< AWB参数 b_gain */
	uint32_t awb_ct;	/**< 当前色温 */
	uint32_t awb_bright_min = 0;
	uint32_t awb_bright_max = 0;

	for (chnNum = 0; chnNum < FS_CHN_NUM; chnNum++) {
		if (chn[chnNum].enable) {
			ret = IMP_Encoder_StartRecvPic(chnNum);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_StartRecvPic(%d) failed\n", chnNum);
				return NULL;
			}

			s32picWidth  = chn[chnNum].fs_chn_attr.picWidth;
			s32picHeight = chn[chnNum].fs_chn_attr.picHeight;
			sprintf(stream_path, "%s/stream-%d-%dx%d.%s", STREAM_FILE_PATH_PREFIX, chnNum,
					s32picWidth, s32picHeight, (payloadType == PT_H264) ? "h264" : "h265");

			IMP_LOG_INFO(TAG, "Video ChnNum=%d Open Stream file %s\n", chnNum, stream_path);
			stream_fd[chnNum] = open(stream_path, O_RDWR | O_CREAT | O_TRUNC, 0777);
			if (stream_fd[chnNum] < 0) {
				IMP_LOG_ERR(TAG, "open %s failed\n", stream_path);
				return NULL;
			}
			IMP_LOG_DBG(TAG, "OK\n");
		}
	}


	for (i = 0; i < totalSaveStreamCnt; i++) {
		if(i % 100 == 0){
			printf("Set the parames of Client AE?(y/n)\n");
			ask = getchar();
			getchar(); // enter
			if(ask == 'y'){
				printf("\nPlease input:integration times(lines):");
				fscanf(stdin, "%d", &ae_time);
				getchar(); // enter
				printf("\nPlease input:sensor again:");
				fscanf(stdin, "%d", &ae_again);
				getchar(); // enter
				printf("\nPlease input:isp again:");
				fscanf(stdin, "%d", &ae_ispdgain);
				getchar(); // enter
				printf("over: ae_time %d, s_again %d, isp_dgain %d\n", ae_time, ae_again, ae_ispdgain);
				pthread_mutex_lock(&g_client_ae_data.ae_mutex);
				g_client_ae_data.change = 1;
				g_client_ae_data.AeIntegrationTime = ae_time;
				g_client_ae_data.AeAGain = ae_again;
				g_client_ae_data.AeIspDGain = ae_ispdgain;
				pthread_mutex_unlock(&g_client_ae_data.ae_mutex);
			}
			printf("Set the parames of Client AWB?(y/n)\n");
			ask = getchar();
			getchar(); // enter
			if(ask == 'y'){
				printf("\nPlease input:r_gain(0~65535):");
				fscanf(stdin, "%d", &awb_r_gain);
				getchar(); // enter
				printf("\nPlease input:b_gain(0~65535):");
				fscanf(stdin, "%d", &awb_b_gain);
				getchar(); // enter
				printf("\nPlease input:ct(1000 ~ 20000):");
				fscanf(stdin, "%d", &awb_ct);
				getchar(); // enter
				printf("Set Client AWB bright's parameters?(y/n)\n");
				ask = getchar();
				getchar(); // enter
				if(ask == 'y'){
					printf("\nPlease input:bright_min(1 ~ 255):");
					fscanf(stdin, "%d", &awb_bright_min);
					getchar(); // enter
					printf("\nPlease input:bright_max(1 ~ 255):");
					fscanf(stdin, "%d", &awb_bright_max);
					getchar(); // enter
				}
				printf("over: r_gain %d, b_gain %d, ct %d, bright_min %d, bright_max %d\n",
						awb_r_gain, awb_b_gain, awb_ct, awb_bright_min, awb_bright_max);
				pthread_mutex_lock(&g_client_awb_data.awb_mutex);
				g_client_awb_data.change = 1;
				g_client_awb_data.r_gain = awb_r_gain;
				g_client_awb_data.b_gain = awb_b_gain;
				g_client_awb_data.ct = awb_ct;
				g_client_awb_data.bright_min = awb_bright_min;
				g_client_awb_data.bright_max = awb_bright_max;
				pthread_mutex_unlock(&g_client_awb_data.awb_mutex);
			}
		}

		for (chnNum = 0; chnNum < FS_CHN_NUM; chnNum++) {
			if (chn[chnNum].enable) {
				sprintf(snap_path, "%s/snap%d-%d.jpg", SNAP_FILE_PATH_PREFIX,
						chnNum, i);

				ret = IMP_Encoder_PollingStream(chnNum, 200);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_Encoder_PollingStream(%d) timeout\n", chnNum);
					continue;
				}

				/* 获取H264/H265码流 */
				/* Get H264 or H265 Stream */
				ret = IMP_Encoder_GetStream(chnNum, &stream, 1);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_Encoder_GetStream(%d) failed\n", chnNum);
					return NULL;
				}

				ret = save_stream(stream_fd[chnNum], &stream);
				if (ret < 0) {
					close(stream_fd[chnNum]);
					return NULL;
				}

				IMP_Encoder_ReleaseStream(chnNum, &stream);
			}
		}
	}


	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			close(stream_fd[i]);
			ret = IMP_Encoder_StopRecvPic(i);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_StopRecvPic(%d) failed\n", i);
				return NULL;
			}
		}
	}

	return NULL;
}

static int my_sample_start_get_video_stream()
{
	int ret = 0;

	int arg = ((chn[0].payloadType << 16) | chn[0].index);
	ret = pthread_create(&tid[0], NULL, get_video_stream, (void *)arg);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "get_video_stream pthread_create failed(%d) failed\n", chn[0].index);
		return ret;
	}

	return 0;
}

static void my_sample_stop_get_video_stream()
{
	pthread_join(tid[0], NULL);

	return;
}


int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;

#if (SENSOR_NUM > 1)
	printf("Client-3A Only support one sensor!!!\n");
	return 0;
#endif
	sample_set_client_3a(&client_ae_func, &client_awb_func);
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

	ret = sample_video_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Video init failed\n");
		return -1;
	}

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

	/* Step.5 开启视频流 */
	/* Step.5 Stream On */
	ret = sample_framesource_streamon();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOn failed\n");
		return -1;
	}

	/* Step.6 获取码流和截图 */
	/* Step.6 Get stream and Snap */
	ret = my_sample_start_get_video_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Get video stream failed\n");
		return -1;
	}

	my_sample_stop_get_video_stream();

	/* Step.8 停止视频流 */
	/* Step.8 Stream Off */
	ret = sample_framesource_streamoff();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOff failed\n");
		return -1;
	}

	/* Step.9 解绑 */
	/* Step.9 UnBind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_System_UnBind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "UnBind framesource%d and encoder%d failed\n", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
	}

	/* Step.10 Encoder反初始化 */
	/* Step.10 Encoder exit */
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

	/* Step.11 FrameSource反初始化 */
	/* Step.11 FrameSource exit */
	ret = sample_framesource_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource exit failed\n");
		return -1;
	}

	/* Step.12 系统反初始化 */
	/* Step.12 System exit */
	ret = sample_system_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System exit failed\n");
		return -1;
	}

	return 0;
}
