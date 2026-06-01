/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#include "sample-common.h"
#include "sample-common-framesource.h"
#include "sample-common-encoder.h"

#define TAG "sample-Encoder-joint"
#define ENABLE_JPEG_ENCODE

#define JOINT_CHN_NUM 12
static int joint_chn[JOINT_CHN_NUM] = {
			1,//JOINT_CHN0
			0,//JOINT_CHN1
			0,//JOINT_CHN2
			0,//JOINT_CHN3
			0,//JOINT_CHN4
			0,//JOINT_CHN5
			0,//JOINT_CHN6
			0,//JOINT_CHN7
			0,//JOINT_CHN8
			0,//JOINT_CHN9
			0,//JOINT_CHN10
			0,//JOINT_CHN11
		};

extern struct chn_conf chn[];
extern int direct_switch;

int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;

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

	/* 创建拼接属性：通道0、3、1竖向拼接 */
	/* Create the splicing attribute: vertical splicing of channels 0, 3, and 1 */
	IMPFSJointAttr attr;
	memset(&attr, 0xff, sizeof(IMPFSJointAttr));
	attr.position[0][0] = 0;
	attr.position[1][0] = 3;
	attr.position[2][0] = 1;
	/* attr.position[2][1] = 4; */
	/* attr.position[1][0] = 1; */
	ret = IMP_FrameSource_CreateJoint(&attr);

	for (i = 0; i < JOINT_CHN_NUM; i++) {
		if (joint_chn[i]) {
			chn[i].enable = joint_chn[i];
			chn[i].fs_chn_attr.picWidth  = attr.width;
			chn[i].fs_chn_attr.picHeight = attr.height;
		}
	}

	/* Step.3 Encoder初始化 */
	/* Step.3 Encoder init */
	/*只需要创建非拼接通道和拼接的输出通道*/
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
		/* if (chn[i].enable) { */
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
	/*只需要绑定非拼接通道和拼接的输出通道*/
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
		/* if (chn[i].enable) { */
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

	/* Step.7 停止获取码流 */
	/* Step.7 Stop get stream */
	sample_stop_get_video_stream();

	/* 销毁拼接属性 */
	/* Destroy the splicing attribute */
	ret = IMP_FrameSource_DestroyJoint(attr.handler);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_DestroyJoint failed\n");
		return -1;
	}

	/* Step.8 停止视频流 */
	/* Step.8 Stream Off */
	ret = sample_framesource_streamoff();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOff failed\n");
		return -1;
	}

	/* Step.9 解绑 */
	/* Step.9 UnBind */
	/*只需要解绑非拼接通道和拼接的输出通道*/
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

	/*只需要解绑非拼接通道和拼接的输出通道*/
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
