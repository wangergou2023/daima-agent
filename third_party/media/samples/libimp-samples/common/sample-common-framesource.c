/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#include <sys/types.h>
#include "sample-common.h"
#include "sample-common-framesource.h"

#define TAG "sample-Common-Framesource"

//#define GET_FRAME_NOCOPY
extern struct chn_conf chn[];

static void *get_frame(void *args)
{
	int index = (int)args;
	int chnNum = chn[index].index;
	int i = 0;
	int ret = 0;
	IMPFrameInfo *frame = NULL;
	char framefilename[64];
	int fd = -1;

	if (PIX_FMT_NV12 == chn[index].fs_chn_attr.pixFmt) {
		sprintf(framefilename, "/tmp/frame%dx%d_%d.nv12", chn[index].fs_chn_attr.picWidth, chn[index].fs_chn_attr.picHeight, chnNum);
	} else {
		sprintf(framefilename, "/tmp/frame%dx%d_%d.raw", chn[index].fs_chn_attr.picWidth, chn[index].fs_chn_attr.picHeight, chnNum);
	}

	fd = open(framefilename, O_RDWR | O_CREAT, 0x644);
	if (fd < 0) {
		IMP_LOG_ERR(TAG, "open %s failed:%s\n", framefilename, strerror(errno));
		goto err_open_framefilename;
	}

#ifdef GET_FRAME_NOCOPY
	ret = IMP_FrameSource_SetFrameDepthCopyType(chnNum, 1);
	if (
	    ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_SetFrameDepthCopyType(%d) failed\n", chnNum);
		goto err_IMP_FrameSource_SetFrameDepth_1;
	}
#endif
	ret = IMP_FrameSource_SetFrameDepth(chnNum, chn[index].fs_chn_attr.nrVBs * 2);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_FrameSource_SetFrameDepth(%d,%d) failed\n", chnNum, chn[index].fs_chn_attr.nrVBs * 2);
		goto err_IMP_FrameSource_SetFrameDepth_1;
	}

	for (i = 0; i < NR_FRAMES_TO_SAVE; i++) {
		ret = IMP_FrameSource_GetFrame(chnNum, &frame);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_GetFrame(%d) i=%d failed\n", chnNum, i);
			goto err_IMP_FrameSource_GetFrame_i;
		}
		if (NR_FRAMES_TO_SAVE/2 == i) {
#ifdef GET_FRAME_NOCOPY
			if (write(fd, (void *)frame->virAddr, frame->width * frame->height) != frame->width * frame->height) {
				IMP_LOG_ERR(TAG, "chnNum=%d write frame i=%d failed\n", chnNum, i);
				goto err_write_frame;
			}

			if (write(fd, (void *)frame->virAddr + frame->width * ((frame->height + 15) & ~15), frame->width * frame->height / 2) != frame->width * frame->height / 2) {
				IMP_LOG_ERR(TAG, "chnNum=%d write frame i=%d failed\n", chnNum, i);
				goto err_write_frame;
			}
#else
			if (write(fd, (void *)frame->virAddr, frame->size) != frame->size) {
				IMP_LOG_ERR(TAG, "chnNum=%d write frame i=%d failed\n", chnNum, i);
				goto err_write_frame;
			}
#endif
		}
		ret = IMP_FrameSource_ReleaseFrame(chnNum, frame);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_FrameSource_ReleaseFrame(%d) i=%d failed\n", chnNum, i);
			goto err_IMP_FrameSource_ReleaseFrame_i;
		}
	}

	IMP_FrameSource_SetFrameDepth(chnNum, 0);

	return (void *)0;

err_IMP_FrameSource_ReleaseFrame_i:
err_write_frame:
	IMP_FrameSource_ReleaseFrame(chnNum, frame);
err_IMP_FrameSource_GetFrame_i:
	goto err_IMP_FrameSource_SetFrameDepth_1;
	IMP_FrameSource_SetFrameDepth(chnNum, 0);
err_IMP_FrameSource_SetFrameDepth_1:
	close(fd);
err_open_framefilename:
	return (void *)-1;
}

static void *snap_frame(void *args)
{
	int index = (int)args;
	int chnNum = chn[index].index;
	int i = 0;
	int ret = 0;
	char framefilename[64];
	int fd = -1;
	unsigned char * nv12_buf = NULL;
	IMPFrameInfo frame;

	if (PIX_FMT_NV12 == chn[index].fs_chn_attr.pixFmt) {
		sprintf(framefilename, "/tmp/snap_frrame%dx%d_%d.nv12", chn[index].fs_chn_attr.picWidth, chn[index].fs_chn_attr.picHeight, chnNum);
	} else {
		sprintf(framefilename, "/tmp/snap_frame%dx%d_%d.raw", chn[index].fs_chn_attr.picWidth, chn[index].fs_chn_attr.picHeight, chnNum);
	}

	fd = open(framefilename, O_RDWR | O_CREAT, 0x644);
	if (fd < 0) {
		IMP_LOG_ERR(TAG, "open %s failed:%s\n", framefilename, strerror(errno));
		goto err_open_framefilename;
	}

	nv12_buf = (unsigned char *)malloc(chn[index].fs_chn_attr.picWidth * ((chn[index].fs_chn_attr.picHeight + 15) & ~(15)) * 3 / 2);
	if (nv12_buf == 0) {
		IMP_LOG_ERR(TAG, "error(%s,%d): malloc buf failed \n", __func__, __LINE__);
		return NULL;
	}

	for (i = 0; i < NR_FRAMES_TO_SAVE; i++) {
	    ret = IMP_FrameSource_SnapFrame(chnNum, PIX_FMT_NV12, chn[index].fs_chn_attr.picWidth, chn[index].fs_chn_attr.picHeight, nv12_buf, &frame);
	    if (ret < 0) {
		    IMP_LOG_ERR(TAG, "IMP_FrameSource_SnapFrame(%d,%d, %d) failed\n", chnNum, chn[index].fs_chn_attr.picWidth, chn[index].fs_chn_attr.picHeight);
		    goto err_IMP_FrameSource_SnapFrame;
	    }
		frame.virAddr = (unsigned int)nv12_buf;
		usleep(50);
		if (NR_FRAMES_TO_SAVE/2 == i) {
			if (write(fd, (void *)frame.virAddr, frame.size) != frame.size) {
				IMP_LOG_ERR(TAG, "chnNum=%d write frame i=%d failed\n", chnNum, i);
				goto err_write_frame;
			}
		}
	}

    free(nv12_buf);

	return (void *)0;

err_write_frame:
err_IMP_FrameSource_SnapFrame:
	close(fd);
err_open_framefilename:
	return (void *)-1;
}

int sample_get_frame()
{
	int i = 0;
	int ret = 0;
	pthread_t tid[FS_CHN_NUM];

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = pthread_create(&tid[i], NULL, get_frame, (void *)chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Create ChnNum%d get_frame failed\n", chn[i].index);
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

int sample_snap_frame()
{
	int i = 0;
	int ret = 0;
	pthread_t tids[FS_CHN_NUM];

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = pthread_create(&tids[i], NULL, snap_frame, (void *)chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Create ChnNum%d snap_frame failed\n", chn[i].index);
				return -1;
			}
		}
	}

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			pthread_join(tids[i],NULL);
		}
	}

	return 0;
}

int sample_framesource_init()
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_FrameSource_CreateChn(chn[i].index, &chn[i].fs_chn_attr);
			if(ret < 0){
				IMP_LOG_ERR(TAG, "IMP_FrameSource_CreateChn(%d) failed\n", chn[i].index);
				return -1;
			}

			ret = IMP_FrameSource_SetChnAttr(chn[i].index, &chn[i].fs_chn_attr);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_FrameSource_SetChnAttr(%d) failed\n", chn[i].index);
				return -1;
			}
		}
	}

	return 0;
}

int sample_framesource_exit()
{
	int i = 0;
	int ret = 0;

	/* 销毁通道 */
	/* Destroy channel */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_FrameSource_DestroyChn(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_FrameSource_DestroyChn(%d) failed\n", chn[i].index);
				return -1;
			}
		}
	}

	return 0;
}

int sample_framesource_streamon()
{
	int i = 0;
	int ret = 0;
	/* 使能通道 */
	/* Enable channels */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_FrameSource_EnableChn(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_FrameSource_EnableChn(%d) failed\n", chn[i].index);
				return -1;
			}
		}
	}
	return 0;
}

int sample_framesource_streamoff()
{
	int i = 0;
	int ret = 0;
	/* 停止通道 */
	/* Disble channels */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable && !chn[i].joint.output) {
			ret = IMP_FrameSource_DisableChn(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_FrameSource_DisableChn(%d) failed\n", chn[i].index);
				return -1;
			}
		}
	}
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable && chn[i].joint.output) {
			ret = IMP_FrameSource_DisableChn(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_FrameSource_DisableChn(%d) failed\n", chn[i].index);
				return -1;
			}
		}
	}
	return 0;
}
