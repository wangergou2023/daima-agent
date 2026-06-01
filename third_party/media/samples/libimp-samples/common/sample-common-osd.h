/*
	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
*/

#ifndef __SAMPLE_COMMON_OSD_H__
#define __SAMPLE_COMMON_OSD_H__

#include "imp/imp_osd.h"
#include "sample-common.h"
#ifdef __cplusplus
#if __cplusplus
extern "C"
{
#endif
#endif /* __cplusplus */

int grpNum[FS_CHN_NUM];
IMPRgnHandle prHander[FS_CHN_NUM][5];

#define ISPOSD_SHOWON (1)
#define ISPOSD_SHOWOFF (0)

uint32_t *timeStampData ;
IMPOSDRgnAttr rIspOsdAttr;

char *g_pdata;
FILE *g_fp;
int g_buse2bit;

int sample_isposd_init(int sensor_num);
int sample_isposd_exit(int sensor_num);


int sample_isposd_multipe_init(int sensor_num);
int sample_isposd_multipe_exit(int sensor_num);

int sample_ipuosd_init(int grpNum);
int sample_ipuosd_exit(int grpNum);

void* sample_isposd_draw(void *arg);
void* sample_isposd_multipe_draw(void *arg);
void* sample_ipuosd_draw(void *arg);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __SAMPLE_COMMON_OSD_H__ */
