/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#include "sample-common.h"
#include "sample-common-framesource.h"

#define TAG "sample-GetFrame"

extern struct chn_conf chn[];

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

//#define GET_FRAME_EX
#ifdef GET_FRAME_EX
static int sample_mainpro_getframeex();
#endif

int main(int argc, char *argv[])
{
	int ret = 0;
	int i = 0;

	/* Step.1 System init */
	ret = sample_system_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System init failed\n");
		return -1;
	}

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
			chn[i].fs_chn_attr.picWidth  = attr.width;
			chn[i].fs_chn_attr.picHeight = attr.height;
			chn[i].fs_chn_attr.pixFmt   = PIX_FMT_NV12;
			ret = IMP_FrameSource_SetChnAttr(chn[i].index, &chn[i].fs_chn_attr);
			// printf("SetChnAttr chn=%d,width=%d,height=%d,size=%d\n", chn[i].index, attr.width, attr.height, chn[i].fs_chn_attr.size);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "SetChnAttr failed\n");
				return -1;
			}
		}
	}

	/* Step.3 Stream On */
	ret = sample_framesource_streamon();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOn failed\n");
		return -1;
	}

	/* Step.4 Get frame */
#ifdef GET_FRAME_EX
	ret = sample_mainpro_getframeex();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "get frameex failed\n");
		return -1;
	}
#else
	ret = sample_get_frame();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "get frame failed\n");
		return -1;
	}
#endif

	/* 销毁拼接属性 */
	/* Destroy the splicing attribute */
	ret = IMP_FrameSource_DestroyJoint(attr.handler);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_DestroyJoint failed\n");
		return -1;
	}

	/* Step.5 Stream Off */
	ret = sample_framesource_streamoff();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOff failed\n");
		return -1;
	}

	/* Step.6 FrameSource exit */
	ret = sample_framesource_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource exit failed\n");
		return -1;
	}

	/* Step.7 System exit */
	ret = sample_system_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System exit failed\n");
		return -1;
	}

	return 0;
}

#ifdef GET_FRAME_EX
static void *mainpro_getframeex_thread(void *args)
{
	int i = 0;
	int ret = 0;
	int fd = -1;
	char framefilename[64] = { 0 };
	int index = (int)args;
	int chnNum = chn[index].index;
	IMPFrameInfo *frame = NULL;

	if (PIX_FMT_NV12 == chn[index].fs_chn_attr.pixFmt) {
		sprintf(framefilename, "/tmp/frame%dx%d_%d.nv12", chn[index].fs_chn_attr.picWidth, chn[index].fs_chn_attr.picHeight, index);
	} else {
		sprintf(framefilename, "/tmp/frame%dx%d_%d.raw", chn[index].fs_chn_attr.picWidth, chn[index].fs_chn_attr.picHeight, index);
	}

	fd = open(framefilename, O_RDWR | O_CREAT, 0x644);
	if (fd < 0) {
		IMP_LOG_ERR(TAG, "open %s failed\n", framefilename);
		return NULL;
	}

	for (i = 0; i < NR_FRAMES_TO_SAVE; i++) {
		printf("IMP_FrameSource_GetFrameEx%d i=%d\n",chnNum,i);
		ret = IMP_FrameSource_GetFrameEx(chnNum, &frame);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_GetFrameEx(%d) failed\n", chnNum);
			return NULL;
		}

		if (NR_FRAMES_TO_SAVE/2 == i) {
			write(fd, (void *)frame->virAddr, frame->width * frame->height);
			write(fd, (void *)frame->virAddr + frame->width * ((frame->height + 15) & ~15),frame->width * frame->height / 2);
		}


		ret = IMP_FrameSource_ReleaseFrameEx(chnNum, frame);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_ReleaseFrameEx(%d) failed\n", chnNum);
			return NULL;
		}
	}

	close(fd);

	return NULL;
}

static int sample_mainpro_getframeex(void)
{
	int i = 0;
	int ret = 0;
	pthread_t tid[FS_CHN_NUM];

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = pthread_create(&tid[i], NULL, mainpro_getframeex_thread, (void *)(chn[i].index));
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Create Chn%d mainpro_getframeex_thread failed\n", chn[i].index);
				return -1;
			}
		}
	}

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			pthread_join(tid[i],NULL);
		}
	}

	return 0;
}
#endif
