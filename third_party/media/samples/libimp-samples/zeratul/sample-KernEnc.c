/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#include "sample-common.h"

#define TAG "sample-KernEnc"

extern int IMP_Encoder_KernEnc_Stop();
extern int IMP_Encoder_KernEnc_GetStream(int encChn, IMPEncoderKernEncOut *encOut);
extern int IMP_Encoder_KernEnc_Release(int encChn);

int main(int argc, char *argv[])
{
	int ret = 0;
	int chnId = 0;
	FILE *fd = NULL;
	IMPEncoderKernEncOut encOut;

	/* Step.1 停止 kernel encode */
	/* Step.1 Stop kernel encode */
	ret = IMP_Encoder_KernEnc_Stop();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "KernEnc stop failed\n");
		return -1;
	}

	/* Step.2 获取 kernel 视频流 */
	/* Step.2 Get kernel encode stream */
	fd = fopen("/tmp/kern_enc", "a+");
	if (!fd) {
		IMP_LOG_ERR(TAG, "KernEnc open file failed\n");
		return -1;
	}

	while (ret == 0) {
		ret = IMP_Encoder_KernEnc_GetStream(chnId, &encOut);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "KernEnc get stream failed\n");
		} else {
#if 0
			printf("type=%d\n", encOut.type);
			printf("bufAddr=0x%08x\n", encOut.bufAddr);
			printf("bufLen=%d\n", encOut.bufLen);
			printf("strmCnt=%d\n", encOut.strmCnt);
			printf("index=%d\n", encOut.index);
			printf("strmAddr=0x%08x\n", encOut.strmAddr);
			printf("strmLen=%d\n", encOut.strmLen);
			printf("timestamp=%d\n", encOut.timestamp);
			printf("refType=%d\n", encOut.refType);
			printf("\n");
#endif
			fwrite((void *)encOut.strmAddr, 1, encOut.strmLen, fd);
		}
	}
	fclose(fd);

	/* Step.3 释放 kernel 编码 */
	/* Step.3 Release kernel encode */
	ret = IMP_Encoder_KernEnc_Release(chnId);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "KernEnc release failed\n");
		return -1;
	}

	return 0;
}
