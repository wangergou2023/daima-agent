/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __SAMPLE_COMMON_KERNENC_H__
#define __SAMPLE_COMMON_KERNENC_H__

#ifdef __cplusplus
#if __cplusplus
extern "C"
{
#endif
#endif /* __cplusplus */

int sample_get_kernvideo_stream();
void sample_stop_kernvideo_stream();
extern int IMP_Encoder_KernEnc_Stop();
extern int IMP_Encoder_KernEnc_GetStream(int encChn, IMPEncoderKernEncOut *encOut);
extern int IMP_Encoder_KernEnc_Release(int encChn);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __SAMPLE_COMMON_KERNENC_H__ */
