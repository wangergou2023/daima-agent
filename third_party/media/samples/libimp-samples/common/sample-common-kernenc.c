/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#include "sample-common.h"

#define TAG "sample-Common-Kernenc"

int kerenc_enable[FS_CHN_NUM] = {0};
pthread_t tid_kernenc[FS_CHN_NUM*2];
extern struct chn_conf chn[];

static void *get_kernvideo_stream(void *args)
{
	int ret = 0;
	int val = 0, chnNum = 0;
	FILE  *fd = NULL;
	char ker_stream_path[64];
	IMPEncoderKernEncOut encOut;

	val = (int)args;
	chnNum = val & 0xffff;
	IMPPayloadType payloadType;
	payloadType = (val >> 16) & 0xffff;

	sprintf(ker_stream_path, "%s/kern_enc-%d.%s", STREAM_FILE_PATH_PREFIX, chnNum,
			(payloadType == PT_H264) ? "h264" : "h265");
	fd = fopen(ker_stream_path, "a+");
	if (fd < 0) {
		IMP_LOG_ERR(TAG, "KernEnc open file failed\n");
		return NULL;
	}
	while (ret == 0) {
		ret = IMP_Encoder_KernEnc_GetStream(chnNum, &encOut);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "KernEnc get stream failed\n");
		} else {
#if 0
			printf("index=%d\n", encOut.index);
			printf("type=%d\n", encOut.type);
			printf("bufAddr=0x%08x\n", encOut.bufAddr);
			printf("bufLen=%d\n", encOut.bufLen);
			printf("strmCnt=%d\n", encOut.strmCnt);
			printf("strmAddr=0x%08x\n", encOut.strmAddr);
			printf("strmLen=%d\n", encOut.strmLen);
			printf("timestamp=%d\n", encOut.timestamp);
			printf("\n");
#endif
			fwrite((void *)encOut.strmAddr, 1, encOut.strmLen, fd);
		}
	}
	ret = IMP_Encoder_KernEnc_Release(chnNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "KernEnc release failed\n");
		return NULL;
	}
	fclose(fd);
	return NULL;

}

int sample_get_kernvideo_stream()
{
	int i = 0;
	int ret = 0;
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_Encoder_KernEnc_GetStatus(chn[i].index, &kerenc_enable[i]);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_KernEnc_GetStatus failed\n");
				return -1;
			}
			if(kerenc_enable[i] == 1){
				int arg = ((chn[i].payloadType << 16) | chn[i].index);
				ret = pthread_create(&tid_kernenc[chn[i].index], NULL,get_kernvideo_stream, (void *)arg);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "ChnNum%d get_video_stream failed\n", chn[i].index);
				}
			}
		}
	}

	return 0;
}

void sample_stop_kernvideo_stream()
{
	int i = 0;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			if(kerenc_enable[i] == 1)
				pthread_join(tid_kernenc[chn[i].index], NULL);
		}
	}

	return;
}
