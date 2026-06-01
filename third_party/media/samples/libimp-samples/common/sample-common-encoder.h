/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __SAMPLE_COMMON_ENCODER_H__
#define __SAMPLE_COMMON_ENCODER_H__

#ifdef __cplusplus
#if __cplusplus
extern "C"
{
#endif
#endif /* __cplusplus */

int sample_jpeg_init();
int sample_jpeg_exit();
int sample_video_init();
int sample_video_exit(void);
int sample_start_get_video_stream();
void sample_stop_get_video_stream();
int sample_start_get_jpeg_stream();
void sample_stop_get_jpeg_stream();
int sample_get_video_stream_byfd();

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __SAMPLE_COMMON_ENCODER_H__ */
