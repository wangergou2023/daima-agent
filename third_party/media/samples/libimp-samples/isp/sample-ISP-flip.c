/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#include "sample-common.h"
#include "sample-common-framesource.h"
#include "sample-common-encoder.h"

#define TAG "sample-ISP-flip"

extern struct chn_conf chn[];
extern int direct_switch;

int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;

	//direct_switch = 0;

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

	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_Encoder_CreateGroup(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Encoder CreateGroup(%d) failed\n", chn[i].index);
				return -1;
			}
		}
	}

	/* Step.3 Encoder初始化 */
	/* Step.3 Encoder init */
	ret = sample_video_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Video init failed\n");
		return -1;
	}

	/* Step.4 绑定 */
	/* Step.4 Bind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Bind framesource%d and encoder%d failed\n", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
	}

	/* Step.5 开启数据流 */
	/* Step.5 Stream On */
	ret = sample_framesource_streamon();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOn failed\n");
		return -1;
	}

	/* Step.6 获取数据流 */
	/* Step.6 Get stream */
	ret = sample_start_get_video_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Get video stream failed\n");
		return -1;
	}

	/* Step.7 设置ISP 镜像翻转 */
	/* Step.7 set ISP flip or mirr */
    /* 注意：1. 编码直通下，每个Sensor的channel0 的vflip只能使用sensor的。
     *  2. 同一种模式，比如IMPISP_FLIP_V_MODE，只能使用sensor_mode或者isp_mode中的一个，禁止两个同时设置。
     *  3. 若使用Sensor的vflip，需设置sensor_mode的IMPISP_FLIP_NORMAL_MODE恢复成正常模式;isp_mode同理 */
    /* Attention: 1. In coding pass-through mode, the vflip of channel 0 for each Sensor can only use the one from the sensor.
     * 2. For the same mode (e.g., IMPISP_FLIP_V_MODE), only one of the sensor_mode or isp_mode can be used; it is prohibited to set both simultaneously.
     * 3. If the vflip of the Sensor is to be used, the IMPISP_FLIP_NORMAL_MODE in sensor_mode must be set to restore it to normal mode; the same applies to isp_mode. */
	/* normal image */
	usleep(500000);
	IMPISPHVFLIPAttr attr;
	if (SENSOR_NUM >= IMPISP_TOTAL_ONE) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_MAIN, &attr);
	    attr.isp_mode[0] = IMPISP_FLIP_NORMAL_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &attr);
    }
	if (SENSOR_NUM >= IMPISP_TOTAL_TWO) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_SEC, &attr);
	    attr.isp_mode[0] = IMPISP_FLIP_NORMAL_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_SEC, &attr);
    }
	if (SENSOR_NUM >= IMPISP_TOTAL_THR) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_THR, &attr);
	    attr.isp_mode[0] = IMPISP_FLIP_NORMAL_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_THR, &attr);
    }
	if (SENSOR_NUM == IMPISP_TOTAL_FOU) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_FOUR, &attr);
	    attr.isp_mode[0] = IMPISP_FLIP_NORMAL_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_FOUR, &attr);
    }

	/* horizonital flip */
	usleep(500000);
	if (SENSOR_NUM >= IMPISP_TOTAL_ONE) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_MAIN, &attr);
	    attr.isp_mode[0] = IMPISP_FLIP_H_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &attr);
    }
	if (SENSOR_NUM >= IMPISP_TOTAL_TWO) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_SEC, &attr);
	    attr.isp_mode[0] = IMPISP_FLIP_H_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_SEC, &attr);
    }
	if (SENSOR_NUM >= IMPISP_TOTAL_THR) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_THR, &attr);
	    attr.isp_mode[0] = IMPISP_FLIP_H_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_THR, &attr);
    }
	if (SENSOR_NUM == IMPISP_TOTAL_FOU) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_FOUR, &attr);
	    attr.isp_mode[0] = IMPISP_FLIP_H_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_FOUR, &attr);
    }

	/* normal image */
	usleep(500000);
	if (SENSOR_NUM >= IMPISP_TOTAL_ONE) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_MAIN, &attr);
	    attr.isp_mode[0] = IMPISP_FLIP_NORMAL_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &attr);
    }
	if (SENSOR_NUM >= IMPISP_TOTAL_TWO) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_SEC, &attr);
	    attr.isp_mode[0] = IMPISP_FLIP_NORMAL_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_SEC, &attr);
    }
	if (SENSOR_NUM >= IMPISP_TOTAL_THR) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_THR, &attr);
	    attr.isp_mode[0] = IMPISP_FLIP_NORMAL_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_THR, &attr);
    }
	if (SENSOR_NUM == IMPISP_TOTAL_FOU) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_FOUR, &attr);
	    attr.isp_mode[0] = IMPISP_FLIP_NORMAL_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_FOUR, &attr);
    }

	/* vertical flip */
	usleep(500000);
	if (SENSOR_NUM >= IMPISP_TOTAL_ONE) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_MAIN, &attr);
	    attr.sensor_mode = IMPISP_FLIP_V_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &attr);
    }
	if (SENSOR_NUM >= IMPISP_TOTAL_TWO) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_SEC, &attr);
	    attr.sensor_mode = IMPISP_FLIP_V_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_SEC, &attr);
    }
	if (SENSOR_NUM >= IMPISP_TOTAL_THR) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_THR, &attr);
	    attr.sensor_mode = IMPISP_FLIP_V_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_THR, &attr);
    }
	if (SENSOR_NUM == IMPISP_TOTAL_FOU) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_FOUR, &attr);
	    attr.sensor_mode = IMPISP_FLIP_V_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_FOUR, &attr);
    }

	/* normal image */
	usleep(500000);
	if (SENSOR_NUM >= IMPISP_TOTAL_ONE) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_MAIN, &attr);
	    attr.sensor_mode = IMPISP_FLIP_NORMAL_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &attr);
    }
	if (SENSOR_NUM >= IMPISP_TOTAL_TWO) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_SEC, &attr);
	    attr.sensor_mode = IMPISP_FLIP_NORMAL_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_SEC, &attr);
    }
	if (SENSOR_NUM >= IMPISP_TOTAL_THR) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_THR, &attr);
	    attr.sensor_mode = IMPISP_FLIP_NORMAL_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_THR, &attr);
    }
	if (SENSOR_NUM == IMPISP_TOTAL_FOU) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_FOUR, &attr);
	    attr.sensor_mode = IMPISP_FLIP_NORMAL_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_FOUR, &attr);
    }

	/* horizonital and vertical flip */
	usleep(500000);
	if (SENSOR_NUM >= IMPISP_TOTAL_ONE) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_MAIN, &attr);
	    attr.sensor_mode = IMPISP_FLIP_HV_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &attr);
    }
	if (SENSOR_NUM >= IMPISP_TOTAL_TWO) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_SEC, &attr);
	    attr.sensor_mode = IMPISP_FLIP_HV_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_SEC, &attr);
    }
	if (SENSOR_NUM >= IMPISP_TOTAL_THR) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_THR, &attr);
	    attr.sensor_mode = IMPISP_FLIP_HV_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_THR, &attr);
    }
	if (SENSOR_NUM == IMPISP_TOTAL_FOU) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_FOUR, &attr);
	    attr.sensor_mode = IMPISP_FLIP_HV_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_FOUR, &attr);
    }

	/* normal image */
	usleep(500000);
	if (SENSOR_NUM >= IMPISP_TOTAL_ONE) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_MAIN, &attr);
	    attr.sensor_mode = IMPISP_FLIP_NORMAL_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &attr);
    }
	if (SENSOR_NUM >= IMPISP_TOTAL_TWO) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_SEC, &attr);
	    attr.sensor_mode = IMPISP_FLIP_NORMAL_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_SEC, &attr);
    }
	if (SENSOR_NUM >= IMPISP_TOTAL_THR) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_THR, &attr);
	    attr.sensor_mode = IMPISP_FLIP_NORMAL_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_THR, &attr);
    }
	if (SENSOR_NUM == IMPISP_TOTAL_FOU) {
	    IMP_ISP_Tuning_GetHVFLIP(IMPVI_FOUR, &attr);
	    attr.sensor_mode = IMPISP_FLIP_NORMAL_MODE;
	    IMP_ISP_Tuning_SetHVFLIP(IMPVI_FOUR, &attr);
    }

	/* Step.8 停止获取码流 */
	/* Step.8 Stop Get stream */
	sample_stop_get_video_stream();

	/* Step.9 停止数据流 */
	/* Step.9 Stream Off */
	ret = sample_framesource_streamoff();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOff failed\n");
		return -1;
	}

	/* Step.10 解绑 */
	/* Step.10 UnBind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_System_UnBind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "UnBind framesource%d and encoder%d failed\n", chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
				return -1;
			}
		}
	}

	/* Step.11 Encoder反初始化 */
	/* Step.11 Encoder exit */
	ret = sample_video_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Video exit failed\n");
		return -1;
	}
	for (i = 0; i < FS_CHN_NUM; i++) {
		if (chn[i].enable) {
			ret = IMP_Encoder_DestroyGroup(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Encoder DestroyGroup(%d) failed\n", chn[i].index);
				return -1;
			}
		}
	}

	/* Step.12 FrameSource反初始化 */
	/* Step.12 FrameSource exit */
	ret = sample_framesource_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource exit failed\n");
		return -1;
	}

	/* Step.13 系统反初始化 */
	/* Step.13 System exit */
	ret = sample_system_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "System exit failed\n");
		return -1;
	}

	return 0;
}
