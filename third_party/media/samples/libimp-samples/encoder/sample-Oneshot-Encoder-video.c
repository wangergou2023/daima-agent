/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#include "sample-common.h"
#include "sample-common-framesource.h"
#include "sample-common-encoder.h"

#define TAG "sample-Encoder-video"
#define ENABLE_JPEG_ENCODE

extern struct chn_conf chn[];
extern int direct_switch;
extern int g_oneshot_is_open;
extern int g_oneshot_test_enable;
static pthread_t tid[FS_CHN_NUM*2];

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
	int snap_fd;
	int s32picWidth = 0, s32picHeight = 0;
	payloadType = PT_H265;
	IMPEncoderStream stream;

	for (chnNum = 0; chnNum < FS_CHN_NUM; chnNum++) {
		if (chn[chnNum].enable) {
			ret = IMP_Encoder_StartRecvPic(chnNum);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_StartRecvPic(%d) failed\n", chnNum);
				return NULL;
			}

			if((chnNum % 3) == 0){
				/* JPEG */
				ret = IMP_Encoder_StartRecvPic(12+chnNum/3);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_Encoder_StartRecvPic(%d) failed\n", chnNum);
					return NULL;
				}
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


	int totalSaveStreamCnt = NR_FRAMES_TO_SAVE;
	for (i = 0; i < totalSaveStreamCnt; i++) {

		if(g_oneshot_is_open == 'y'){
			printf("ONESHOT tick! Input Enter...\n");
			getchar();
			IMP_ISP_OneShotTick();
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

				if((chnNum % 3) == 0){
					/* JPEG */
					snap_fd = open(snap_path, O_RDWR | O_CREAT | O_TRUNC, 0777);
					if (snap_fd < 0) {
						IMP_LOG_ERR(TAG, "open %s failed\n", snap_path);
						return NULL;
					}
					ret = IMP_Encoder_PollingStream(12+chnNum/3, 10000);
					if (ret < 0) {
						IMP_LOG_ERR(TAG, "IMP_Encoder_PollingStream(%d) timeout\n", chnNum);
						continue;
					}

					/* 获取JPEG码流 */
					/* Get JPEG Snap */
					ret = IMP_Encoder_GetStream(12+chnNum/3, &stream, 1);
					if (ret < 0) {
						IMP_LOG_ERR(TAG, "IMP_Encoder_GetStream(%d) failed\n", chnNum);
						return NULL;
					}

					ret = save_stream(snap_fd, &stream);
					if (ret < 0) {
						close(snap_fd);
						return NULL;
					}
					close(snap_fd);
					IMP_Encoder_ReleaseStream(12+chnNum/3, &stream);
				}
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

			if((chnNum % 3) == 0){
				/*JPEG*/
				ret = IMP_Encoder_StopRecvPic(12+i/3);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_Encoder_StopRecvPic(%d) failed\n", 12+i/3);
					return NULL;
				}
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

	g_oneshot_test_enable = 1;
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

#ifdef ENABLE_JPEG_ENCODE
	ret = sample_jpeg_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Jpeg init failed\n");
		return -1;
	}
#endif

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

	if(g_oneshot_is_open != 'y'){
		printf("测试Framesource开流后打开oneshot?\n");
		printf("输入：y or n\n");
		g_oneshot_is_open = getchar();
		getchar();
		if(g_oneshot_is_open == 'y'){
			IMP_ISP_OneShotEnable();
		}
	}

	/* Step.6 获取码流和截图 */
	/* Step.6 Get stream and Snap */
	ret = my_sample_start_get_video_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Get video stream failed\n");
		return -1;
	}

#if 0
	/*#ifdef ENABLE_JPEG_ENCODE*/
	ret = sample_start_get_jpeg_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Get jpeg stream failed\n");
		return -1;
	}
#endif

	/* Step.7 停止获取码流 */
	/* Step.7 Stop get stream */
#ifdef ENABLE_JPEG_ENCODE
	/*sample_stop_get_jpeg_stream();*/
#endif
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
#ifdef ENABLE_JPEG_ENCODE
	ret = sample_jpeg_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Jpeg exit failed\n");
		return -1;
	}
#endif

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
	if(g_oneshot_is_open == 'y'){
		printf("测试结束关闭oneshot\n");
		g_oneshot_is_open = 0;
		IMP_ISP_OneShotDisable();
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
