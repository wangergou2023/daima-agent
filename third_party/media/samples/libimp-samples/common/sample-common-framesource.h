/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __SAMPLE_COMMON_FRAMESOURCE_H__
#define __SAMPLE_COMMON_FRAMESOURCE_H__

#ifdef __cplusplus
#if __cplusplus
extern "C"
{
#endif
#endif /* __cplusplus */

int sample_framesource_init();
int sample_framesource_exit();
int sample_framesource_streamon();
int sample_framesource_streamoff();
int sample_get_frame();

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __SAMPLE_COMMON_FRAMESOURCE_H__ */
