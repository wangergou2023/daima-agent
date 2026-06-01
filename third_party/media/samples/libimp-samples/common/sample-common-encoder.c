/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#include <sys/types.h>
#include "sample-common.h"
#include "sample-common-encoder.h"

#define TAG "sample-Common-Encoder"

#define SAVE_STREAM
//#define SHOW_FRM_BITRATE
#ifdef SHOW_FRM_BITRATE
#define FRM_BIT_RATE_TIME 2
#define STREAM_TYPE_NUM 16
static int frmrate_sp[STREAM_TYPE_NUM] = { 0 };
static int statime_sp[STREAM_TYPE_NUM] = { 0 };
static int bitrate_sp[STREAM_TYPE_NUM] = { 0 };
#endif
int S_RC_METHOD = ENC_RC_MODE_CBR;
static pthread_t tid[FS_CHN_NUM*2];
extern struct chn_conf chn[];
extern int direct_switch;

static int save_stream(int fd, IMPEncoderStream *stream)
{
	int i = 0;
	int ret = 0;
	int nr_pack = stream->packCount;

	for (i = 0; i < nr_pack; i++) {
		ret = write(fd, (void *)stream->pack[i].virAddr, stream->pack[i].length);
		if (ret != stream->pack[i].length) {
			IMP_LOG_ERR(TAG, "stream write failed\n");
			return -1;
		}
	}
	return 0;
}

#if 0
static int save_stream_by_name(char *stream_prefix, int idx, IMPEncoderStream *stream)
{
	int i = 0;
	int ret = 0;
	int stream_fd = -1;
	char stream_path[128];
	int nr_pack = stream->packCount;

	sprintf(stream_path, "%s_%d", stream_prefix, idx);

	IMP_LOG_DBG(TAG, "Open Stream file %s ", stream_path);
	stream_fd = open(stream_path, O_RDWR | O_CREAT | O_TRUNC, 0777);
	if (stream_fd < 0) {
		IMP_LOG_ERR(TAG, "open %s failed\n", stream_path);
		return -1;
	}
	IMP_LOG_DBG(TAG, "OK\n");

	for (i = 0; i < nr_pack; i++) {
		ret = write(stream_fd, (void *)stream->pack[i].virAddr, stream->pack[i].length);
		if (ret != stream->pack[i].length) {
			close(stream_fd);
			IMP_LOG_ERR(TAG, "stream write failed\n");
			return -1;
		}
	}

	close(stream_fd);

	return 0;
}
#endif

int sample_jpeg_init()
{
	int i = 0;
	int ret = 0;
	IMPEncoderAttr *enc_attr;
	IMPEncoderRcAttr *rc_attr;
	IMPEncoderCHNAttr channel_attr;
	IMPFSChnAttr *imp_chn_attr_tmp;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			if (SENSOR_NUM == IMPISP_TOTAL_ONE) {
				if (chn[i].index != 0)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_TWO) {
				if (chn[i].index != 0 && chn[i].index != 3)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_THR) {
				if (chn[i].index != 0 && chn[i].index != 3 && chn[i].index != 6)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_FOU) {
				if (chn[i].index != 0 && chn[i].index != 3 && chn[i].index != 6 && chn[i].index != 9)
					continue;
			}

			imp_chn_attr_tmp = &chn[i].fs_chn_attr;
			memset(&channel_attr, 0, sizeof(IMPEncoderCHNAttr));
			enc_attr = &channel_attr.encAttr;
			enc_attr->enType = PT_JPEG;
			enc_attr->bufSize = 0;
			enc_attr->profile = 0;
			enc_attr->picWidth = imp_chn_attr_tmp->picWidth;
			enc_attr->picHeight = imp_chn_attr_tmp->picHeight;
			rc_attr = &channel_attr.rcAttr;
			rc_attr->attrRcMode.rcMode = ENC_RC_MODE_FIXQP;
			rc_attr->attrRcMode.attrJPEGFixQp.qp = 40;

			if(direct_switch == 1) {
				if (0 == chn[i].index)
					channel_attr.bEnableIvdc = true;
			} else if (direct_switch == 2) {
				if (0 == chn[i].index || 3 == chn[i].index)
					channel_attr.bEnableIvdc = true;
			} else if (direct_switch == 3) {
				if (0 == chn[i].index || 3 == chn[i].index || 6 == chn[i].index)
					channel_attr.bEnableIvdc = true;
			} else if (direct_switch == 4) {
				if (0 == chn[i].index || 3 == chn[i].index || 6 == chn[i].index || 9 == chn[i].index)
					channel_attr.bEnableIvdc = true;
			}

			/* 创建编码通道 */
			/* Create Channel */
			ret = IMP_Encoder_CreateChn(12+chn[i].index/3, &channel_attr);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_CreateChn(%d) failed\n", 12+chn[i].index/3);
				return -1;
			}

			/* 注册编码通道 */
			/* Resigter Channel */
			ret = IMP_Encoder_RegisterChn(chn[i].index, 12+chn[i].index/3);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_RegisterChn(group%d, chn%d) failed\n", chn[i].index, 12+chn[i].index/3);
				return -1;
			}
		}
	}

	return 0;
}

int sample_jpeg_exit(void)
{
	int i = 0;
	int ret = 0;
	int chnNum = 0;
	IMPEncoderCHNStat chn_stat;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			if (SENSOR_NUM == IMPISP_TOTAL_ONE) {
				if (chn[i].index != 0)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_TWO) {
				if (chn[i].index != 0 && chn[i].index != 3)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_THR) {
				if (chn[i].index != 0 && chn[i].index != 3 && chn[i].index != 6)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_FOU) {
				if (chn[i].index != 0 && chn[i].index != 3 && chn[i].index != 6 && chn[i].index != 9)
					continue;
			}
			chnNum = 12+chn[i].index/3;

			memset(&chn_stat, 0, sizeof(IMPEncoderCHNStat));
			ret = IMP_Encoder_Query(chnNum, &chn_stat);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_Query(%d) failed\n", chnNum);
				return -1;
			}

			if (chn_stat.registered) {
				ret = IMP_Encoder_UnRegisterChn(chnNum);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_Encoder_UnRegisterChn(%d) failed\n", chnNum);
					return -1;
				}

				ret = IMP_Encoder_DestroyChn(chnNum);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_Encoder_DestroyChn(%d) failed\n", chnNum);
					return -1;
				}
			}
		}
	}

	return 0;
}

int sample_video_init()
{
	int i = 0;
	int ret = 0;
	int chnNum = 0;
	int s32picWidth = 0, s32picHeight = 0;
	IMPEncoderAttr *enc_attr;
	IMPEncoderRcAttr *rc_attr;
	IMPFSChnAttr *imp_chn_attr_tmp;
	IMPEncoderCHNAttr channel_attr;
	IMPFSI2DAttr i2d_attr;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if ((!chn[i].joint.enable && chn[i].enable) || chn[i].joint.output) {
			imp_chn_attr_tmp = &chn[i].fs_chn_attr;
			memset(&channel_attr, 0, sizeof(IMPEncoderCHNAttr));
			memset(&i2d_attr, 0, sizeof(IMPFSI2DAttr));

			ret = IMP_FrameSource_GetI2dAttr(chn[i].index, &i2d_attr);
			if(ret < 0){
				IMP_LOG_ERR(TAG, "IMP_FrameSource_GetI2dAttr(%d) failed\n", chn[i].index);
				return -1;
			}

			if((1 == i2d_attr.i2d_enable) && (!chn[i].joint.output) &&
					((i2d_attr.rotate_enable) && (i2d_attr.rotate_angle == 90 || i2d_attr.rotate_angle == 270))){
				s32picWidth  = chn[i].fs_chn_attr.picHeight;
				s32picHeight = chn[i].fs_chn_attr.picWidth;
			} else if (chn[i].joint.output) {
				s32picWidth  = chn[i].joint.width;
				s32picHeight = chn[i].joint.height;
			} else {
				s32picWidth  = chn[i].fs_chn_attr.picWidth;
				s32picHeight = chn[i].fs_chn_attr.picHeight;
			}

			enc_attr = &channel_attr.encAttr;
			enc_attr->enType = chn[i].payloadType;
			if ((s32picHeight > 1920) || (s32picHeight == 1920)) {
				enc_attr->bufSize = s32picWidth * s32picHeight * 3 / 2;
			} else if ((s32picHeight > 1520) || (s32picHeight == 1520)) {
				enc_attr->bufSize = s32picWidth * s32picHeight * 3 / 8;
			} else if ((s32picHeight > 1080) || (s32picHeight == 1080)) {
				enc_attr->bufSize = s32picWidth * s32picHeight / 2;
			} else {
				enc_attr->bufSize = s32picWidth * s32picHeight * 3 / 4;
			}
			enc_attr->profile   = 1;
			enc_attr->picWidth  = s32picWidth;
			enc_attr->picHeight = s32picHeight;
			rc_attr = &channel_attr.rcAttr;
			rc_attr->attrHSkip.hSkipAttr.skipType = IMP_Encoder_STYPE_N1X;
			rc_attr->attrHSkip.hSkipAttr.m = 3;
			rc_attr->attrHSkip.hSkipAttr.n = 4;
			rc_attr->attrHSkip.hSkipAttr.maxSameSceneCnt = 0;
			rc_attr->attrHSkip.hSkipAttr.bEnableScenecut = 0;
			rc_attr->attrHSkip.hSkipAttr.bBlackEnhance = 0;
			rc_attr->attrHSkip.maxHSkipType = IMP_Encoder_STYPE_N1X;
			if (chn[i].payloadType == PT_H264) {
				chnNum = chn[i].index;
				rc_attr->outFrmRate.frmRateNum = imp_chn_attr_tmp->outFrmRateNum;
				rc_attr->outFrmRate.frmRateDen = imp_chn_attr_tmp->outFrmRateDen;
				rc_attr->maxGop = 2 * rc_attr->outFrmRate.frmRateNum / rc_attr->outFrmRate.frmRateDen;
				if (S_RC_METHOD == ENC_RC_MODE_FIXQP) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_FIXQP;
					rc_attr->attrRcMode.attrH264FixQp.IQp = 35;
					rc_attr->attrRcMode.attrH264FixQp.PQp = 35;
					rc_attr->attrRcMode.attrH264FixQp.blkQpEn = 0;
				} else if (S_RC_METHOD == ENC_RC_MODE_CBR) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_CBR;
					rc_attr->attrRcMode.attrH264Cbr.maxQp = 45;
					rc_attr->attrRcMode.attrH264Cbr.minQp = 15;
					rc_attr->attrRcMode.attrH264Cbr.blkQpEn = 0;
					rc_attr->attrRcMode.attrH264Cbr.initialQp = 35;
					rc_attr->attrRcMode.attrH264Cbr.maxIQp = 45;
					rc_attr->attrRcMode.attrH264Cbr.minIQp = 15;
					rc_attr->attrRcMode.attrH264Cbr.outBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH264Cbr.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH264Cbr.IPfrmQPDelta = 8;
					rc_attr->attrRcMode.attrH264Cbr.PPfrmQPDelta = 8;
					rc_attr->attrRcMode.attrH264Cbr.staticTime = 2;
					rc_attr->attrRcMode.attrH264Cbr.flucLvl = 2;
					rc_attr->attrRcMode.attrH264Cbr.qualityLvl = 3;
					rc_attr->attrRcMode.attrH264Cbr.maxIprop = 100;
					rc_attr->attrRcMode.attrH264Cbr.minIprop = 1;
					rc_attr->attrRcMode.attrH264Cbr.maxIPictureSize = rc_attr->attrRcMode.attrH264Cbr.outBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH264Cbr.maxPPictureSize = rc_attr->attrRcMode.attrH264Cbr.maxIPictureSize * 3 / 5;
				} else if (S_RC_METHOD == ENC_RC_MODE_VBR) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_VBR;
					rc_attr->attrRcMode.attrH264Vbr.maxQp = 45;
					rc_attr->attrRcMode.attrH264Vbr.minQp = 15;
					rc_attr->attrRcMode.attrH264Vbr.blkQpEn = 0;
					rc_attr->attrRcMode.attrH264Vbr.initialQp = 35;
					rc_attr->attrRcMode.attrH264Vbr.maxIQp = 45;
					rc_attr->attrRcMode.attrH264Vbr.minIQp = 15;
					rc_attr->attrRcMode.attrH264Vbr.maxBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH264Vbr.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH264Vbr.changePos = 80;
					rc_attr->attrRcMode.attrH264Vbr.staticTime = 2;
					rc_attr->attrRcMode.attrH264Vbr.qualityLvl = 6;
					rc_attr->attrRcMode.attrH264Vbr.maxIprop = 100;
					rc_attr->attrRcMode.attrH264Vbr.minIprop = 1;
					rc_attr->attrRcMode.attrH264Vbr.maxIPictureSize = rc_attr->attrRcMode.attrH264Vbr.maxBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH264Vbr.maxPPictureSize = rc_attr->attrRcMode.attrH264Vbr.maxIPictureSize * 3 / 5;
				} else if (S_RC_METHOD == ENC_RC_MODE_SMART) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_SMART;
					rc_attr->attrRcMode.attrH264Smart.maxQp = 45;
					rc_attr->attrRcMode.attrH264Smart.minQp = 15;
					rc_attr->attrRcMode.attrH264Smart.blkQpEn = 0;
					rc_attr->attrRcMode.attrH264Smart.initialQp = 35;
					rc_attr->attrRcMode.attrH264Smart.maxIQp = 45;
					rc_attr->attrRcMode.attrH264Smart.minIQp = 15;
					rc_attr->attrRcMode.attrH264Smart.maxBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH264Smart.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH264Smart.changePos = 80;
					rc_attr->attrRcMode.attrH264Smart.staticTime = 2;
					rc_attr->attrRcMode.attrH264Smart.qualityLvl = 6;
					rc_attr->attrRcMode.attrH264Smart.maxIprop = 100;
					rc_attr->attrRcMode.attrH264Smart.minIprop = 1;
					rc_attr->attrRcMode.attrH264Smart.minStillRate = 25;
					rc_attr->attrRcMode.attrH264Smart.maxStillQp = 35;
					rc_attr->attrRcMode.attrH264Smart.superSmartEn = 0;
					rc_attr->attrRcMode.attrH264Smart.supSmtStillLvl = 5;
					rc_attr->attrRcMode.attrH264Smart.supSmtStillRateLvl = 2;
					rc_attr->attrRcMode.attrH264Smart.maxSupSmtStillRate = 20;
					rc_attr->attrRcMode.attrH264Smart.maxIPictureSize = rc_attr->attrRcMode.attrH264Smart.maxBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH264Smart.maxPPictureSize = rc_attr->attrRcMode.attrH264Smart.maxIPictureSize * 3 / 5;
				} else if (S_RC_METHOD == ENC_RC_MODE_CVBR) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_CVBR;
					rc_attr->attrRcMode.attrH264CVbr.maxQp = 45;
					rc_attr->attrRcMode.attrH264CVbr.minQp = 15;
					rc_attr->attrRcMode.attrH264CVbr.blkQpEn = 0;
					rc_attr->attrRcMode.attrH264CVbr.initialQp = 35;
					rc_attr->attrRcMode.attrH264CVbr.maxIQp = 45;
					rc_attr->attrRcMode.attrH264CVbr.minIQp = 15;
					rc_attr->attrRcMode.attrH264CVbr.maxBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH264CVbr.longMaxBitRate = rc_attr->attrRcMode.attrH264CVbr.maxBitRate * 4 / 5;
					rc_attr->attrRcMode.attrH264CVbr.longMinBitRate = rc_attr->attrRcMode.attrH264CVbr.maxBitRate * 3 / 5;
					rc_attr->attrRcMode.attrH264CVbr.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH264CVbr.changePos = 80;
					rc_attr->attrRcMode.attrH264CVbr.shortStatTime = 2;
					rc_attr->attrRcMode.attrH264CVbr.longStatTime = 60;
					rc_attr->attrRcMode.attrH264CVbr.qualityLvl = 6;
					rc_attr->attrRcMode.attrH264CVbr.maxIprop = 100;
					rc_attr->attrRcMode.attrH264CVbr.minIprop = 1;
					rc_attr->attrRcMode.attrH264CVbr.extraBitRate = 5;
					rc_attr->attrRcMode.attrH264CVbr.maxIPictureSize = rc_attr->attrRcMode.attrH264CVbr.maxBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH264CVbr.maxPPictureSize = rc_attr->attrRcMode.attrH264CVbr.maxIPictureSize * 3 / 5;
				} else if (S_RC_METHOD == ENC_RC_MODE_AVBR){
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_AVBR;
					rc_attr->attrRcMode.attrH264AVbr.maxQp = 45;
					rc_attr->attrRcMode.attrH264AVbr.minQp = 15;
					rc_attr->attrRcMode.attrH264AVbr.blkQpEn = 0;
					rc_attr->attrRcMode.attrH264AVbr.initialQp = 35;
					rc_attr->attrRcMode.attrH264AVbr.maxIQp = 45;
					rc_attr->attrRcMode.attrH264AVbr.minIQp = 15;
					rc_attr->attrRcMode.attrH264AVbr.maxBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH264AVbr.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH264AVbr.changePos = 80;
					rc_attr->attrRcMode.attrH264AVbr.staticTime = 2;
					rc_attr->attrRcMode.attrH264AVbr.qualityLvl = 6;
					rc_attr->attrRcMode.attrH264AVbr.maxIprop = 100;
					rc_attr->attrRcMode.attrH264AVbr.minIprop = 1;
					rc_attr->attrRcMode.attrH264AVbr.minStillRate = 25;
					rc_attr->attrRcMode.attrH264AVbr.maxStillQp = 35;
					rc_attr->attrRcMode.attrH264AVbr.maxIPictureSize = rc_attr->attrRcMode.attrH264AVbr.maxBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH264AVbr.maxPPictureSize = rc_attr->attrRcMode.attrH264AVbr.maxIPictureSize * 3 / 5;
				} else {
					IMP_LOG_ERR(TAG, "S_RC_METHOD error!\n");
					return -1;
				}
			} else if (chn[i].payloadType == PT_H265) { /* PT_H265 */
				chnNum = chn[i].index;
				rc_attr = &channel_attr.rcAttr;
				rc_attr->outFrmRate.frmRateNum = imp_chn_attr_tmp->outFrmRateNum;
				rc_attr->outFrmRate.frmRateDen = imp_chn_attr_tmp->outFrmRateDen;
				rc_attr->maxGop = 2 * rc_attr->outFrmRate.frmRateNum / rc_attr->outFrmRate.frmRateDen;
				if (S_RC_METHOD == ENC_RC_MODE_FIXQP) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_FIXQP;
					rc_attr->attrRcMode.attrH265FixQp.IQp = 35;
					rc_attr->attrRcMode.attrH265FixQp.PQp = 35;
					rc_attr->attrRcMode.attrH265FixQp.blkQpEn = 0;
				} else if (S_RC_METHOD == ENC_RC_MODE_CBR) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_CBR;
					rc_attr->attrRcMode.attrH265Cbr.maxQp = 45;
					rc_attr->attrRcMode.attrH265Cbr.minQp = 15;
					rc_attr->attrRcMode.attrH265Cbr.blkQpEn = 0;
					rc_attr->attrRcMode.attrH265Cbr.initialQp = 35;
					rc_attr->attrRcMode.attrH265Cbr.maxIQp = 45;
					rc_attr->attrRcMode.attrH265Cbr.minIQp = 15;
					rc_attr->attrRcMode.attrH265Cbr.outBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH265Cbr.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH265Cbr.IPfrmQPDelta = 8;
					rc_attr->attrRcMode.attrH265Cbr.PPfrmQPDelta = 8;
					rc_attr->attrRcMode.attrH265Cbr.staticTime = 2;
					rc_attr->attrRcMode.attrH265Cbr.flucLvl = 2;
					rc_attr->attrRcMode.attrH265Cbr.qualityLvl = 3;
					rc_attr->attrRcMode.attrH265Cbr.maxIprop = 100;
					rc_attr->attrRcMode.attrH265Cbr.minIprop = 1;
					rc_attr->attrRcMode.attrH265Cbr.maxIPictureSize = rc_attr->attrRcMode.attrH265Cbr.outBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH265Cbr.maxPPictureSize = rc_attr->attrRcMode.attrH265Cbr.maxIPictureSize * 3 / 5;
				} else if (S_RC_METHOD == ENC_RC_MODE_VBR) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_VBR;
					rc_attr->attrRcMode.attrH265Vbr.maxQp = 45;
					rc_attr->attrRcMode.attrH265Vbr.minQp = 15;
					rc_attr->attrRcMode.attrH265Vbr.blkQpEn = 0;
					rc_attr->attrRcMode.attrH265Vbr.initialQp = 35;
					rc_attr->attrRcMode.attrH265Vbr.maxIQp = 45;
					rc_attr->attrRcMode.attrH265Vbr.minIQp = 15;
					rc_attr->attrRcMode.attrH265Vbr.maxBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH265Vbr.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH265Vbr.changePos = 80;
					rc_attr->attrRcMode.attrH265Vbr.staticTime = 2;
					rc_attr->attrRcMode.attrH265Vbr.qualityLvl = 6;
					rc_attr->attrRcMode.attrH265Vbr.maxIprop = 100;
					rc_attr->attrRcMode.attrH265Vbr.minIprop = 1;
					rc_attr->attrRcMode.attrH265Vbr.maxIPictureSize = rc_attr->attrRcMode.attrH265Vbr.maxBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH265Vbr.maxPPictureSize = rc_attr->attrRcMode.attrH265Vbr.maxIPictureSize * 3 / 5;
				} else if (S_RC_METHOD == ENC_RC_MODE_SMART) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_SMART;
					rc_attr->attrRcMode.attrH265Smart.maxQp = 45;
					rc_attr->attrRcMode.attrH265Smart.minQp = 15;
					rc_attr->attrRcMode.attrH265Smart.blkQpEn = 0;
					rc_attr->attrRcMode.attrH265Smart.initialQp = 35;
					rc_attr->attrRcMode.attrH265Smart.maxIQp = 45;
					rc_attr->attrRcMode.attrH265Smart.minIQp = 15;
					rc_attr->attrRcMode.attrH265Smart.maxBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH265Smart.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH265Smart.changePos = 80;
					rc_attr->attrRcMode.attrH265Smart.staticTime = 2;
					rc_attr->attrRcMode.attrH265Smart.qualityLvl = 6;
					rc_attr->attrRcMode.attrH265Smart.maxIprop = 100;
					rc_attr->attrRcMode.attrH265Smart.minIprop = 1;
					rc_attr->attrRcMode.attrH265Smart.minStillRate = 25;
					rc_attr->attrRcMode.attrH265Smart.maxStillQp = 35;
					rc_attr->attrRcMode.attrH265Smart.superSmartEn = 0;
					rc_attr->attrRcMode.attrH265Smart.supSmtStillLvl = 5;
					rc_attr->attrRcMode.attrH265Smart.supSmtStillRateLvl = 2;
					rc_attr->attrRcMode.attrH265Smart.maxSupSmtStillRate = 20;
					rc_attr->attrRcMode.attrH265Smart.maxIPictureSize = rc_attr->attrRcMode.attrH265Smart.maxBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH265Smart.maxPPictureSize = rc_attr->attrRcMode.attrH265Smart.maxIPictureSize * 3 / 5;
				} else if (S_RC_METHOD == ENC_RC_MODE_CVBR) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_CVBR;
					rc_attr->attrRcMode.attrH265CVbr.maxQp = 45;
					rc_attr->attrRcMode.attrH265CVbr.minQp = 15;
					rc_attr->attrRcMode.attrH265CVbr.blkQpEn = 0;
					rc_attr->attrRcMode.attrH265CVbr.initialQp = 35;
					rc_attr->attrRcMode.attrH265CVbr.maxIQp = 45;
					rc_attr->attrRcMode.attrH265CVbr.minIQp = 15;
					rc_attr->attrRcMode.attrH265CVbr.maxBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH265CVbr.longMaxBitRate = rc_attr->attrRcMode.attrH265CVbr.maxBitRate * 4 / 5;
					rc_attr->attrRcMode.attrH265CVbr.longMinBitRate = rc_attr->attrRcMode.attrH265CVbr.maxBitRate * 3 / 5;
					rc_attr->attrRcMode.attrH265CVbr.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH265CVbr.changePos = 80;
					rc_attr->attrRcMode.attrH265CVbr.shortStatTime = 2;
					rc_attr->attrRcMode.attrH265CVbr.longStatTime = 60;
					rc_attr->attrRcMode.attrH265CVbr.qualityLvl = 6;
					rc_attr->attrRcMode.attrH265CVbr.maxIprop = 100;
					rc_attr->attrRcMode.attrH265CVbr.minIprop = 1;
					rc_attr->attrRcMode.attrH265CVbr.extraBitRate = 5;
					rc_attr->attrRcMode.attrH265CVbr.maxIPictureSize = rc_attr->attrRcMode.attrH265CVbr.maxBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH265CVbr.maxPPictureSize = rc_attr->attrRcMode.attrH265CVbr.maxIPictureSize * 3 / 5;
				} else if (S_RC_METHOD == ENC_RC_MODE_AVBR) {
					rc_attr->attrRcMode.rcMode = ENC_RC_MODE_AVBR;
					rc_attr->attrRcMode.attrH265AVbr.maxQp = 45;
					rc_attr->attrRcMode.attrH265AVbr.minQp = 15;
					rc_attr->attrRcMode.attrH265AVbr.blkQpEn = 0;
					rc_attr->attrRcMode.attrH265AVbr.initialQp = 35;
					rc_attr->attrRcMode.attrH265AVbr.maxIQp = 45;
					rc_attr->attrRcMode.attrH265AVbr.minIQp = 15;
					rc_attr->attrRcMode.attrH265AVbr.maxBitRate = BITRATE_720P_Kbs;
					rc_attr->attrRcMode.attrH265AVbr.iBiasLvl = 0;
					rc_attr->attrRcMode.attrH265AVbr.changePos = 80;
					rc_attr->attrRcMode.attrH265AVbr.staticTime = 2;
					rc_attr->attrRcMode.attrH265AVbr.qualityLvl = 6;
					rc_attr->attrRcMode.attrH265AVbr.maxIprop = 100;
					rc_attr->attrRcMode.attrH265AVbr.minIprop = 1;
					rc_attr->attrRcMode.attrH265AVbr.minStillRate = 25;
					rc_attr->attrRcMode.attrH265AVbr.maxStillQp = 35;
					rc_attr->attrRcMode.attrH265AVbr.maxIPictureSize = rc_attr->attrRcMode.attrH265AVbr.maxBitRate * 6 / 5;
					rc_attr->attrRcMode.attrH265AVbr.maxPPictureSize = rc_attr->attrRcMode.attrH265AVbr.maxIPictureSize * 3 / 5;
				} else {
					IMP_LOG_ERR(TAG, "S_RC_METHOD error!\n");
					return -1;
				}
			}

			if(direct_switch == 1) {
				if (0 == chn[i].index)
					channel_attr.bEnableIvdc = true;
			} else if (direct_switch == 2) {
				if (0 == chn[i].index || 3 == chn[i].index)
					channel_attr.bEnableIvdc = true;
			} else if (direct_switch == 3) {
				if (0 == chn[i].index || 3 == chn[i].index || 6 == chn[i].index)
					channel_attr.bEnableIvdc = true;
			} else if (direct_switch == 4) {
				if (0 == chn[i].index || 3 == chn[i].index || 6 == chn[i].index || 9 == chn[i].index)
					channel_attr.bEnableIvdc = true;
			}

			ret = IMP_Encoder_CreateChn(chnNum, &channel_attr);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_CreateChn(%d) failed\n", chnNum);
				return -1;
			}

			ret = IMP_Encoder_RegisterChn(chn[i].index, chnNum);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_RegisterChn(group%d, chn%d) failed\n", chn[i].index, chnNum);
				return -1;
			}
		}
	}

	return 0;
}

int sample_video_exit(void)
{
	int i = 0;
	int ret = 0;
	int chnNum = 0;
	IMPEncoderCHNStat chn_stat;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if ((!chn[i].joint.enable && chn[i].enable) || chn[i].joint.output) {
			chnNum = chn[i].index;
			memset(&chn_stat, 0, sizeof(IMPEncoderCHNStat));
			ret = IMP_Encoder_Query(chnNum, &chn_stat);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_Query(%d) failed\n", chnNum);
				return -1;
			}

			if (chn_stat.registered) {
				ret = IMP_Encoder_UnRegisterChn(chnNum);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_Encoder_UnRegisterChn(%d) failed\n", chnNum);
					return -1;
				}

				ret = IMP_Encoder_DestroyChn(chnNum);
				if (ret < 0) {
					IMP_LOG_ERR(TAG, "IMP_Encoder_DestroyChn(%d) failed\n", chnNum);
					return -1;
				}
			}
		}
	}

	return 0;
}

static void *get_video_stream(void *args)
{
	int i = 0;
	int ret = 0;
	int val = 0, chnNum = 0;

	val = (int)args;
	chnNum = val & 0xffff;
#ifdef SAVE_STREAM
	char stream_path[64];
	IMPPayloadType payloadType;
	int stream_fd = -1;
	IMPFSI2DAttr i2d_attr;
	int s32picWidth = 0, s32picHeight = 0;
	payloadType = (val >> 16) & 0xffff;
#endif

	ret = IMP_Encoder_StartRecvPic(chnNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_StartRecvPic(%d) failed\n", chnNum);
		return NULL;
	}

#ifdef SAVE_STREAM
	memset(&i2d_attr, 0, sizeof(IMPFSI2DAttr));

	ret = IMP_FrameSource_GetI2dAttr(chnNum, &i2d_attr);
	if(ret < 0){
		IMP_LOG_ERR(TAG, "IMP_FrameSource_GetI2dAttr(%d) failed\n", chnNum);
		return NULL;
	}

	if((1 == i2d_attr.i2d_enable) && (!chn[i].joint.output) &&
			((i2d_attr.rotate_enable) && (i2d_attr.rotate_angle == 90 || i2d_attr.rotate_angle == 270))){
		s32picWidth  = chn[chnNum].fs_chn_attr.picHeight;
		s32picHeight = chn[chnNum].fs_chn_attr.picWidth;
	} else if (chn[i].joint.output) {
		s32picWidth  = chn[chnNum].joint.width;
		s32picHeight = chn[chnNum].joint.height;
	} else {
		s32picWidth  = chn[chnNum].fs_chn_attr.picWidth;
		s32picHeight = chn[chnNum].fs_chn_attr.picHeight;
	}
	sprintf(stream_path, "%s/stream-%d-%dx%d.%s", STREAM_FILE_PATH_PREFIX, chnNum,
			s32picWidth, s32picHeight, (payloadType == PT_H264) ? "h264" : "h265");

	IMP_LOG_INFO(TAG, "Video ChnNum=%d Open Stream file %s\n", chnNum, stream_path);
	stream_fd = open(stream_path, O_RDWR | O_CREAT | O_TRUNC, 0777);
	if (stream_fd < 0) {
		IMP_LOG_ERR(TAG, "open %s failed\n", stream_path);
		return NULL;
	}
	IMP_LOG_DBG(TAG, "OK\n");
#endif

	int totalSaveStreamCnt = NR_FRAMES_TO_SAVE;
	for (i = 0; i < totalSaveStreamCnt; i++) {
		ret = IMP_Encoder_PollingStream(chnNum, 1000);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_PollingStream(%d) timeout\n", chnNum);
			continue;
		}

		IMPEncoderStream stream;
		/* 获取H264/H265码流 */
		/* Get H264 or H265 Stream */
		ret = IMP_Encoder_GetStream(chnNum, &stream, 1);
#ifdef SHOW_FRM_BITRATE
		int j, len = 0;
		for (j = 0; j < stream.packCount; j++) {
			len += stream.pack[j].length;
		}
		bitrate_sp[chnNum] += len;
		frmrate_sp[chnNum]++;

		int64_t now = IMP_System_GetTimeStamp() / 1000;
		if(((int)(now - statime_sp[chnNum]) / 1000) >= FRM_BIT_RATE_TIME){
			double fps = (double)frmrate_sp[chnNum] / ((double)(now - statime_sp[chnNum]) / 1000);
			double kbr = (double)bitrate_sp[chnNum] * 8 / (double)(now - statime_sp[chnNum]);

			printf("streamNum[%d]:FPS: %0.2f,Bitrate: %0.2f(kbps)\n", chnNum, fps, kbr);
			//fflush(stdout);

			frmrate_sp[chnNum] = 0;
			bitrate_sp[chnNum] = 0;
			statime_sp[chnNum] = now;
		}
#endif
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_GetStream(%d) failed\n", chnNum);
			return NULL;
		}

#ifdef SAVE_STREAM
		ret = save_stream(stream_fd, &stream);
		if (ret < 0) {
			close(stream_fd);
			return NULL;
		}
#endif

		IMP_Encoder_ReleaseStream(chnNum, &stream);
	}

#ifdef SAVE_STREAM
	close(stream_fd);
#endif

	ret = IMP_Encoder_StopRecvPic(chnNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_StopRecvPic(%d) failed\n", chnNum);
		return NULL;
	}

	return NULL;
}

int sample_start_get_video_stream()
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			int arg = ((chn[i].payloadType << 16) | chn[i].index);
			ret = pthread_create(&tid[chn[i].index], NULL, get_video_stream, (void *)arg);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Create ChnNum%d get_video_stream failed\n", chn[i].index);
			}
		}
	}

	return 0;
}

void sample_stop_get_video_stream()
{
	int i = 0;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			pthread_join(tid[chn[i].index], NULL);
		}
	}

	return;
}

void *get_jpeg_stream(void *args)
{
	int i = 0;
	int ret = 0;
	int val = 0, chnNum = 0;

	val = (int)args;
	chnNum = val & 0xffff;
#ifdef SAVE_STREAM
	char snap_path[64];
	IMPFSI2DAttr i2d_attr;
	int s32picWidth = 0, s32picHeight = 0;
#endif

	srand((unsigned int)time(NULL));
	int totalSaveStreamCnt = NR_JPEG_TO_SAVE;
	for (i = 0; i < totalSaveStreamCnt; i++) {
		ret = IMP_Encoder_StartRecvPic(chnNum);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_StartRecvPic(%d) failed\n", chnNum);
			return NULL;
		}
#ifdef SAVE_STREAM
		int snap_fd;

		memset(&i2d_attr, 0, sizeof(IMPFSI2DAttr));

		ret = IMP_FrameSource_GetI2dAttr((chnNum-12)*3, &i2d_attr);
		if(ret < 0){
			IMP_LOG_ERR(TAG, "IMP_FrameSource_GetI2dAttr(%d) failed\n", (chnNum-12)*3);
			return NULL;
		}

		if((1 == i2d_attr.i2d_enable) &&
				((i2d_attr.rotate_enable) && (i2d_attr.rotate_angle == 90 || i2d_attr.rotate_angle == 270))){
			s32picWidth  = chn[(chnNum-12)*3].fs_chn_attr.picHeight;
			s32picHeight = chn[(chnNum-12)*3].fs_chn_attr.picWidth;
		} else {
			s32picWidth  = chn[(chnNum-12)*3].fs_chn_attr.picWidth;
			s32picHeight = chn[(chnNum-12)*3].fs_chn_attr.picHeight;
		}
		sprintf(snap_path, "%s/snap-%d-%dx%d-%d.jpg", SNAP_FILE_PATH_PREFIX,
				chnNum, s32picWidth, s32picHeight, i);

		IMP_LOG_INFO(TAG, "Open Snap file %s\n", snap_path);
		snap_fd = open(snap_path, O_RDWR | O_CREAT | O_TRUNC, 0777);
		if (snap_fd < 0) {
			IMP_LOG_ERR(TAG, "open %s failed\n", snap_path);
			return NULL;
		}
		IMP_LOG_DBG(TAG, "OK\n");
#endif

		/* 轮巡JPEG通道，超时时间10000ms */
		/* Polling JPEG Snap, set timeout as 10000msec */
		ret = IMP_Encoder_PollingStream(chnNum, 10000);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_PollingStream(%d) timeout\n", chnNum);
			continue;
		}

		IMPEncoderStream stream;
		/* 获取JPEG码流 */
		/* Get JPEG Snap */
		ret = IMP_Encoder_GetStream(chnNum, &stream, 1);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_GetStream(%d) failed\n", chnNum);
			return NULL;
		}

#ifdef SAVE_STREAM
		ret = save_stream(snap_fd, &stream);
		if (ret < 0) {
			close(snap_fd);
			return NULL;
		}
#endif
		IMP_Encoder_ReleaseStream(chnNum, &stream);

#ifdef SAVE_STREAM
		close(snap_fd);
#endif

		ret = IMP_Encoder_StopRecvPic(chnNum);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_StopRecvPic(%d) failed\n", chnNum);
			return NULL;
		}
	}

	return NULL;
}

int sample_start_get_jpeg_stream()
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			if (SENSOR_NUM == IMPISP_TOTAL_ONE) {
				if (chn[i].index != 0)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_TWO) {
				if (chn[i].index != 0 && chn[i].index != 3)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_THR) {
				if (chn[i].index != 0 && chn[i].index != 3 && chn[i].index != 6)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_FOU) {
				if (chn[i].index != 0 && chn[i].index != 3 && chn[i].index != 6 && chn[i].index != 9)
					continue;
			}

			int arg = ((PT_JPEG << 16) | (12+chn[i].index/3));
			ret = pthread_create(&tid[12+chn[i].index/3], NULL, get_jpeg_stream, (void *)arg);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Create ChnNum%d get_jpeg_stream failed\n", 12+chn[i].index/3);
			}
		}
	}

	return 0;
}

void sample_stop_get_jpeg_stream()
{
	int i = 0;

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			if (SENSOR_NUM == IMPISP_TOTAL_ONE) {
				if (chn[i].index != 0)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_TWO) {
				if (chn[i].index != 0 && chn[i].index != 3)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_THR) {
				if (chn[i].index != 0 && chn[i].index != 3 && chn[i].index != 6)
					continue;
			} else if (SENSOR_NUM == IMPISP_TOTAL_FOU) {
				if (chn[i].index != 0 && chn[i].index != 3 && chn[i].index != 6 && chn[i].index != 9)
					continue;
			}

			pthread_join(tid[12+chn[i].index/3], NULL);
		}
	}
	return;
}

int sample_get_video_stream_byfd()
{
	int i = 0;
	int ret = 0;
	int chnNum = 0;
	int streamFd[FS_CHN_NUM], vencFd[FS_CHN_NUM], maxVencFd = 0;
	char stream_path[FS_CHN_NUM][128];
	fd_set readfds;
	struct timeval selectTimeout;
	int saveStreamCnt[FS_CHN_NUM], totalSaveStreamCnt[FS_CHN_NUM];
	memset(streamFd, 0, sizeof(streamFd));
	memset(vencFd, 0, sizeof(vencFd));
	memset(stream_path, 0, sizeof(stream_path));
	memset(saveStreamCnt, 0, sizeof(saveStreamCnt));
	memset(totalSaveStreamCnt, 0, sizeof(totalSaveStreamCnt));

	for (i = 0; i < FS_CHN_NUM; i++) {
		streamFd[i] = -1;
		vencFd[i] = -1;
		saveStreamCnt[i] = 0;
		if (chn[i].enable) {
			chnNum = chn[i].index;
			totalSaveStreamCnt[i] = NR_FRAMES_TO_SAVE;
			sprintf(stream_path[i], "%s/stream-%d.%s", STREAM_FILE_PATH_PREFIX, chnNum,
					(chn[i].payloadType == PT_H264) ? "h264" : "h265");

			streamFd[i] = open(stream_path[i], O_RDWR | O_CREAT | O_TRUNC, 0777);
			if (streamFd[i] < 0) {
				IMP_LOG_ERR(TAG, "open %s failed\n", stream_path[i]);
				return -1;
			}

			vencFd[i] = IMP_Encoder_GetFd(chnNum);
			if (vencFd[i] < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_GetFd(%d) failed\n", chnNum);
				return -1;
			}

			if (maxVencFd < vencFd[i]) {
				maxVencFd = vencFd[i];
			}

			ret = IMP_Encoder_StartRecvPic(chnNum);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_StartRecvPic(%d) failed\n", chnNum);
				return -1;
			}
		}
	}

	while(1) {
		int breakFlag = 1;
		for (i = 0; i < FS_CHN_NUM; i++) {
			breakFlag &= (saveStreamCnt[i] >= totalSaveStreamCnt[i]);
		}
		if (breakFlag) {
			break;
		}

		FD_ZERO(&readfds);
		for (i = 0; i < FS_CHN_NUM; i++) {
			if (chn[i].enable && saveStreamCnt[i] < totalSaveStreamCnt[i]) {
				FD_SET(vencFd[i], &readfds);
			}
		}
		selectTimeout.tv_sec = 2;
		selectTimeout.tv_usec = 0;

		ret = select(maxVencFd + 1, &readfds, NULL, NULL, &selectTimeout);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "select failed\n");
			return -1;
		} else if (ret == 0) {
			continue;
		} else {
			for (i = 0; i < FS_CHN_NUM; i++) {
				if (chn[i].enable && FD_ISSET(vencFd[i], &readfds)) {
					IMPEncoderStream stream;

					chnNum = chn[i].index;
					ret = IMP_Encoder_GetStream(chnNum, &stream, 1);
					if (ret < 0) {
						IMP_LOG_ERR(TAG, "IMP_Encoder_GetStream(%d) failed\n", chnNum);
						return -1;
					}

					ret = save_stream(streamFd[i], &stream);
					if (ret < 0) {
						close(streamFd[i]);
						return -1;
					}

					IMP_Encoder_ReleaseStream(chnNum, &stream);
					saveStreamCnt[i]++;
				}
			}
		}
	}

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			chnNum = chn[i].index;
			IMP_Encoder_StopRecvPic(chnNum);
			close(streamFd[i]);
		}
	}

	return 0;
}
