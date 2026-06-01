/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#include "sample-common.h"
#include "sample-common-framesource.h"
#include "sample-common-encoder.h"

#define TAG "sample-Encoder-video"
//#define ENABLE_JPEG_ENCODE

extern struct chn_conf chn[];
extern int direct_switch;

int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;
	IMPFSJointAttr attr[2];
	memset(attr, 0xff, sizeof(attr));

	attr[0].position[0][0] = 0;
	attr[0].position[1][0] = 3;
	chn[0].joint.enable = 1;
	chn[3].joint.enable = 1;
	chn[0].joint.output = 1;

	attr[1].position[0][0] = 1;
	attr[1].position[1][0] = 4;
	chn[1].joint.enable = 1;
	chn[4].joint.enable = 1;
	chn[1].joint.output = 1;

	{
		chn[0].fs_chn_attr.i2dattr.i2d_enable = 1;
		chn[0].fs_chn_attr.i2dattr.rotate_enable = 1;
		chn[0].fs_chn_attr.i2dattr.rotate_angle = 90;
		chn[0].fs_chn_attr.crop.enable = 0;
		chn[0].fs_chn_attr.scaler.enable = 1;
		chn[0].fs_chn_attr.scaler.outwidth = 1088;
		chn[0].fs_chn_attr.scaler.outheight = 1920;
		chn[0].fs_chn_attr.picWidth = 1088;
		chn[0].fs_chn_attr.picHeight = 1920;

		chn[1].fs_chn_attr.i2dattr.i2d_enable = 1;
		chn[1].fs_chn_attr.i2dattr.rotate_enable = 1;
		chn[1].fs_chn_attr.i2dattr.rotate_angle = 90;
		chn[1].fs_chn_attr.crop.enable = 0;
		chn[1].fs_chn_attr.scaler.enable = 1;
		chn[1].fs_chn_attr.scaler.outwidth = 640;
		chn[1].fs_chn_attr.scaler.outheight = 720;
		chn[1].fs_chn_attr.picWidth = 640;
		chn[1].fs_chn_attr.picHeight = 720;

		chn[3].fs_chn_attr.i2dattr.i2d_enable = 1;
		chn[3].fs_chn_attr.i2dattr.rotate_enable = 1;
		chn[3].fs_chn_attr.i2dattr.rotate_angle = 90;
		chn[3].fs_chn_attr.crop.enable = 0;
		chn[3].fs_chn_attr.scaler.enable = 1;
		chn[3].fs_chn_attr.scaler.outwidth = 1088;
		chn[3].fs_chn_attr.scaler.outheight = 1920;
		chn[3].fs_chn_attr.picWidth = 1088;
		chn[3].fs_chn_attr.picHeight = 1920;

		chn[4].fs_chn_attr.i2dattr.i2d_enable = 1;
		chn[4].fs_chn_attr.i2dattr.rotate_enable = 1;
		chn[4].fs_chn_attr.i2dattr.rotate_angle = 90;
		chn[4].fs_chn_attr.crop.enable = 0;
		chn[4].fs_chn_attr.scaler.enable = 1;
		chn[4].fs_chn_attr.scaler.outwidth = 640;
		chn[4].fs_chn_attr.scaler.outheight = 720;
		chn[4].fs_chn_attr.picWidth = 640;
		chn[4].fs_chn_attr.picHeight = 720;
	}

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

	ret = IMP_FrameSource_CreateJoint(&attr[0]);
	printf("[%s %d] width=%d,height=%d\n", __func__, __LINE__, attr[0].width, attr[0].height);
	chn[0].joint.width = attr[0].width;
	chn[0].joint.height = attr[0].height;

	ret = IMP_FrameSource_CreateJoint(&attr[1]);
	printf("[%s %d] width=%d,height=%d\n", __func__, __LINE__, attr[1].width, attr[1].height);
	chn[1].joint.width = attr[1].width;
	chn[1].joint.height = attr[1].height;

	/* Step.3 Encoder初始化 */
	/* Step.3 Encoder init */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if ((!chn[i].joint.enable && chn[i].enable) || chn[i].joint.output) {
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
		if ((!chn[i].joint.enable && chn[i].enable) || chn[i].joint.output) {
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
	ret = sample_start_get_video_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Get video stream failed\n");
		return -1;
	}

#ifdef ENABLE_JPEG_ENCODE
	ret = sample_start_get_jpeg_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Get jpeg stream failed\n");
		return -1;
	}
#endif

	/* Step.7 停止获取码流 */
	/* Step.7 Stop get stream */
#ifdef ENABLE_JPEG_ENCODE
	sample_stop_get_jpeg_stream();
#endif
	sample_stop_get_video_stream();

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
		if ((!chn[i].joint.enable && chn[i].enable) || chn[i].joint.output) {
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
		if ((!chn[i].joint.enable && chn[i].enable) || chn[i].joint.output) {
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
