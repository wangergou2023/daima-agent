/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#include <sys/types.h>
#include "sample-common.h"

#define TAG "sample-Common"

int direct_switch = 1;
/* 1: ipu osd, 2: isp osd, 3: ipu osd和isp osd */
/* 1: ipu osd, 2: isp osd, 3: ipu osd and isp osd */
int gosd_enable = 0;
/* 多进程编码，依据具体需求开启 */
/* Multi process encode, Can be enabled based on your's demand */
#define MULTI_ENCODE
struct chn_conf chn[FS_CHN_NUM] = {
#if (SENSOR_NUM >= 1)
	{
		.index = CHN0_INDEX,
		.enable = CHN0_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = FIRST_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = FIRST_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = FIRST_CROP_EN,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = FIRST_SENSOR_WIDTH,
			.crop.height = FIRST_SENSOR_HEIGHT,

			.scaler.enable = 1,
			.scaler.outwidth = FIRST_SENSOR_WIDTH,
			.scaler.outheight = FIRST_SENSOR_HEIGHT,

			.picWidth = FIRST_SENSOR_WIDTH,
			.picHeight = FIRST_SENSOR_HEIGHT,

		},
		.framesource_chn =	{ DEV_ID_FS, CHN0_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN0_INDEX, 0},
	},
	{
		.index = CHN1_INDEX,
		.enable = CHN1_EN,
		.payloadType = PT_H264,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = FIRST_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = FIRST_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = 1,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = FIRST_SENSOR_WIDTH_SECOND,
			.crop.height = FIRST_SENSOR_HEIGHT_SECOND,

			.scaler.enable = 1,
			.scaler.outwidth = FIRST_SENSOR_WIDTH_SECOND,
			.scaler.outheight = FIRST_SENSOR_HEIGHT_SECOND,

			.picWidth = FIRST_SENSOR_WIDTH_SECOND,
			.picHeight = FIRST_SENSOR_HEIGHT_SECOND,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN1_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN1_INDEX, 0},
	},
	{
		.index = CHN2_INDEX,
		.enable = CHN2_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = FIRST_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = FIRST_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = 0,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = FIRST_SENSOR_WIDTH_THIRD,
			.crop.height = FIRST_SENSOR_HEIGHT_THIRD,

			.scaler.enable = 1,
			.scaler.outwidth = FIRST_SENSOR_WIDTH_THIRD,
			.scaler.outheight = FIRST_SENSOR_HEIGHT_THIRD,

			.picWidth = FIRST_SENSOR_WIDTH_THIRD,
			.picHeight = FIRST_SENSOR_HEIGHT_THIRD,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN2_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN2_INDEX, 0},
	},
#endif
#if (SENSOR_NUM >= 2)
	{
		.index = CHN3_INDEX,
		.enable = CHN3_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = SECOND_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = SECOND_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = SECOND_CROP_EN,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = SECOND_SENSOR_WIDTH,
			.crop.height = SECOND_SENSOR_HEIGHT,

			.scaler.enable = 1,
			.scaler.outwidth = SECOND_SENSOR_WIDTH,
			.scaler.outheight = SECOND_SENSOR_HEIGHT,

			.picWidth = SECOND_SENSOR_WIDTH,
			.picHeight = SECOND_SENSOR_HEIGHT,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN3_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN3_INDEX, 0},
	},
	{
		.index = CHN4_INDEX,
		.enable = CHN4_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = SECOND_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = SECOND_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = 1,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = SECOND_SENSOR_WIDTH_SECOND,
			.crop.height = SECOND_SENSOR_HEIGHT_SECOND,

			.scaler.enable = 1,
			.scaler.outwidth = SECOND_SENSOR_WIDTH_SECOND,
			.scaler.outheight = SECOND_SENSOR_HEIGHT_SECOND,

			.picWidth = SECOND_SENSOR_WIDTH_SECOND,
			.picHeight = SECOND_SENSOR_HEIGHT_SECOND,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN4_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN4_INDEX, 0},
	},
	{
		.index = CHN5_INDEX,
		.enable = CHN5_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = SECOND_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = SECOND_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = 0,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = SECOND_SENSOR_WIDTH_THIRD,
			.crop.height = SECOND_SENSOR_HEIGHT_THIRD,

			.scaler.enable = 1,
			.scaler.outwidth = SECOND_SENSOR_WIDTH_THIRD,
			.scaler.outheight = SECOND_SENSOR_HEIGHT_THIRD,

			.picWidth = SECOND_SENSOR_WIDTH_THIRD,
			.picHeight = SECOND_SENSOR_HEIGHT_THIRD,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN5_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN5_INDEX, 0},
	},
#endif
#if (SENSOR_NUM >= 3)
	{
		.index = CHN6_INDEX,
		.enable = CHN6_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = THIRD_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = THIRD_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = THIRD_CROP_EN,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = THIRD_SENSOR_WIDTH,
			.crop.height = THIRD_SENSOR_HEIGHT,

			.scaler.enable = 1,
			.scaler.outwidth = THIRD_SENSOR_WIDTH,
			.scaler.outheight = THIRD_SENSOR_HEIGHT,

			.picWidth = THIRD_SENSOR_WIDTH,
			.picHeight = THIRD_SENSOR_HEIGHT,

		},
		.framesource_chn =	{ DEV_ID_FS, CHN6_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN6_INDEX, 0},
	},
	{
		.index = CHN7_INDEX,
		.enable = CHN7_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = THIRD_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = THIRD_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = 1,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = THIRD_SENSOR_WIDTH_SECOND,
			.crop.height = THIRD_SENSOR_HEIGHT_SECOND,

			.scaler.enable = 1,
			.scaler.outwidth = THIRD_SENSOR_WIDTH_SECOND,
			.scaler.outheight = THIRD_SENSOR_HEIGHT_SECOND,

			.picWidth = THIRD_SENSOR_WIDTH_SECOND,
			.picHeight = THIRD_SENSOR_HEIGHT_SECOND,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN7_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN7_INDEX, 0},
	},
	{
		.index = CHN8_INDEX,
		.enable = CHN8_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = THIRD_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = THIRD_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = 0,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = THIRD_SENSOR_WIDTH_THIRD,
			.crop.height = THIRD_SENSOR_HEIGHT_THIRD,

			.scaler.enable = 1,
			.scaler.outwidth = THIRD_SENSOR_WIDTH_THIRD,
			.scaler.outheight = THIRD_SENSOR_HEIGHT_THIRD,

			.picWidth = THIRD_SENSOR_WIDTH_THIRD,
			.picHeight = THIRD_SENSOR_HEIGHT_THIRD,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN8_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN8_INDEX, 0},
	},
#endif
#if (SENSOR_NUM >= 4)
	{
		.index = CHN9_INDEX,
		.enable = CHN9_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = FOURTH_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = FOURTH_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = FOURTH_CROP_EN,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = FOURTH_SENSOR_WIDTH,
			.crop.height = FOURTH_SENSOR_HEIGHT,

			.scaler.enable = 1,
			.scaler.outwidth = FOURTH_SENSOR_WIDTH,
			.scaler.outheight = FOURTH_SENSOR_HEIGHT,

			.picWidth = FOURTH_SENSOR_WIDTH,
			.picHeight = FOURTH_SENSOR_HEIGHT,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN9_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN9_INDEX, 0},
	},
	{
		.index = CHN10_INDEX,
		.enable = CHN10_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = FOURTH_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = FOURTH_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = 1,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = FOURTH_SENSOR_WIDTH_SECOND,
			.crop.height = FOURTH_SENSOR_HEIGHT_SECOND,

			.scaler.enable = 1,
			.scaler.outwidth = FOURTH_SENSOR_WIDTH_SECOND,
			.scaler.outheight = FOURTH_SENSOR_HEIGHT_SECOND,

			.picWidth = FOURTH_SENSOR_WIDTH_SECOND,
			.picHeight = FOURTH_SENSOR_HEIGHT_SECOND,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN10_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN10_INDEX, 0},
	},
	{
		.index = CHN11_INDEX,
		.enable = CHN11_EN,
		.payloadType = PT_H265,
		.fs_chn_attr = {
			.pixFmt = PIX_FMT_NV12,
			.outFrmRateNum = FOURTH_SENSOR_FRAME_RATE_NUM,
			.outFrmRateDen = FOURTH_SENSOR_FRAME_RATE_DEN,
			.nrVBs = 2,
			.type = FS_PHY_CHANNEL,

			.crop.enable = 0,
			.crop.top = 0,
			.crop.left = 0,
			.crop.width = FOURTH_SENSOR_WIDTH_THIRD,
			.crop.height = FOURTH_SENSOR_HEIGHT_THIRD,

			.scaler.enable = 1,
			.scaler.outwidth = FOURTH_SENSOR_WIDTH_THIRD,
			.scaler.outheight = FOURTH_SENSOR_HEIGHT_THIRD,

			.picWidth = FOURTH_SENSOR_WIDTH_THIRD,
			.picHeight = FOURTH_SENSOR_HEIGHT_THIRD,
		},
		.framesource_chn =	{ DEV_ID_FS, CHN11_INDEX, 0},
		.imp_encoder = { DEV_ID_ENC, CHN11_INDEX, 0},
	},
#endif
};

IMPSensorInfo Def_Sensor_Info[4] = {
#if (SENSOR_NUM >= 1)
	{
		.name = FIRST_SNESOR_NAME,
		.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
		.i2c = {FIRST_SNESOR_NAME, FIRST_I2C_ADDR, FIRST_I2C_ADAPTER_ID},
		.rst_gpio = FIRST_RST_GPIO,
		.pwdn_gpio = FIRST_PWDN_GPIO,
		.power_gpio = FIRST_POWER_GPIO,
		.switch_gpio = FIRST_SWITCH_GPIO,
		.switch_gpio_state = 0,
		.sensor_id = FIRST_SENSOR_ID,
		.video_interface = FIRST_VIDEO_INTERFACE,
		.mclk = FIRST_MCLK,
		.default_boot = FIRST_DEFAULT_BOOT,
		.fps = {15, 0}
	},
#endif
#if (SENSOR_NUM >= 2)
	{
		.name = SECOND_SNESOR_NAME,
		.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
		.i2c = {SECOND_SNESOR_NAME, SECOND_I2C_ADDR, SECOND_I2C_ADAPTER_ID},
		.rst_gpio = SECOND_RST_GPIO,
		.pwdn_gpio = SECOND_PWDN_GPIO,
		.power_gpio = SECOND_POWER_GPIO,
		.switch_gpio = SECOND_SWITCH_GPIO,
		.switch_gpio_state = 0,
		.sensor_id = SECOND_SENSOR_ID,
		.video_interface = SECOND_VIDEO_INTERFACE,
		.mclk = SECOND_MCLK,
		.default_boot = SECOND_DEFAULT_BOOT,
		.fps = {15, 0}
	},
#endif
#if (SENSOR_NUM >= 3)
	{
		.name = THIRD_SNESOR_NAME,
		.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
		.i2c = {THIRD_SNESOR_NAME, THIRD_I2C_ADDR, THIRD_I2C_ADAPTER_ID},
		.rst_gpio = THIRD_RST_GPIO,
		.pwdn_gpio = THIRD_PWDN_GPIO,
		.power_gpio = THIRD_POWER_GPIO,
		.switch_gpio = THIRD_SWITCH_GPIO,
		.switch_gpio_state = 0,
		.sensor_id = THIRD_SENSOR_ID,
		.video_interface = THIRD_VIDEO_INTERFACE,
		.mclk = THIRD_MCLK,
		.default_boot = THIRD_DEFAULT_BOOT,
		.fps = {15, 0}
	},
#endif
#if (SENSOR_NUM >= 4)
	{
		.name = FOURTH_SNESOR_NAME,
		.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
		.i2c = {FOURTH_SNESOR_NAME, FOURTH_I2C_ADDR, FOURTH_I2C_ADAPTER_ID},
		.rst_gpio = FOURTH_RST_GPIO,
		.pwdn_gpio = FOURTH_PWDN_GPIO,
		.power_gpio = FOURTH_POWER_GPIO,
		.switch_gpio = FOURTH_SWITCH_GPIO,
		.switch_gpio_state = 0,
		.sensor_id = FOURTH_SENSOR_ID,
		.video_interface = FOURTH_VIDEO_INTERFACE,
		.mclk = FOURTH_MCLK,
		.default_boot = FOURTH_DEFAULT_BOOT,
		.fps = {15, 0}
	}
#endif
};

extern int IMP_OSD_SetPoolSize(int size);

IMPSensorInfo sensor_info[4];
IMPISPCameraInputMode mode = {
	.sensor_num = SENSOR_NUM,
};

int g_oneshot_test_enable = 0;
int g_oneshot_is_open = 0;
int g_t32_online_ldc_enable = 0;
int g_t33_online_ldc_mode = 0;
int g_t33_offline_ldc_mode = 0;

int g_client_3a_test_enable = 0;
IMPISPAeAlgoFunc *g_client_ae = NULL;
IMPISPAwbAlgoFunc *g_client_awb = NULL;

void sample_set_client_3a(IMPISPAeAlgoFunc *client_ae, IMPISPAwbAlgoFunc *client_awb)
{
	g_client_3a_test_enable = 1;
	g_client_ae = client_ae;
	g_client_awb = client_awb;
}


int sample_system_init()
{
	int ret = 0;

	/* isp osd and ipu osd buffer size set */
	if(1 == gosd_enable) { /* only use ipu osd */
		IMP_OSD_SetPoolSize(512*1024);
	} else if(2 == gosd_enable) { /* only use isp osd */
		IMP_ISP_Tuning_SetOsdPoolSize(512 * 1024);
	}else if(3 == gosd_enable) { /* use ipu osd and isp osd */
		IMP_OSD_SetPoolSize(512*1024);
		IMP_ISP_Tuning_SetOsdPoolSize(512 * 1024);
	} else {
		IMP_OSD_SetPoolSize(512*1024);
		IMP_ISP_Tuning_SetOsdPoolSize(512 * 1024);
	}

	ret = IMP_Encoder_SetJpegBsSize(500 * 1024);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_SetJpegBsSize failed\n");
		return -1;
	}

	ret = IMP_Encoder_SetMultiSectionMode(1, 250, 2);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_SetMultiSectionMode failed\n");
		return -1;
	}

#ifdef MULTI_ENCODE
	ret = IMP_Encoder_MultiProcessInit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_MultiProcessInit failed\n");
		return -1;
	}
#endif

	memset(&sensor_info, 0, sizeof(sensor_info));
	memcpy(&sensor_info[0], &Def_Sensor_Info[0], sizeof(IMPSensorInfo));
	memcpy(&sensor_info[1], &Def_Sensor_Info[1], sizeof(IMPSensorInfo));
	memcpy(&sensor_info[2], &Def_Sensor_Info[2], sizeof(IMPSensorInfo));
	memcpy(&sensor_info[3], &Def_Sensor_Info[3], sizeof(IMPSensorInfo));

	IMP_LOG_DBG(TAG, "sample_system_init start\n");

	ret = IMP_ISP_Open();
	if(ret < 0){
		IMP_LOG_ERR(TAG, "failed to open ISP\n");
		return -1;
	}

	if (mode.sensor_num > IMPISP_TOTAL_ONE) {
		ret = IMP_ISP_SetCameraInputMode(&mode);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_SetCameraInputMode failed\n");
			return -1;
		}
	}

	if(g_oneshot_test_enable){
		printf("测试添加sensor前打开oneshot?\n");
		printf("输入：y or n\n");
		g_oneshot_is_open = getchar();
		getchar();
		if(g_oneshot_is_open == 'y'){
			IMP_ISP_OneShotEnable();
		}
	}

	if (g_t32_online_ldc_enable) {
		IMPISPLDCMode mode = IMPISP_ONLINE_MODE;
		IMPISPLDCInitOnlineAttr attr;

		printf("Using T32 Online LDC mode!\n");
		ret = IMP_ISP_LDC_SetMode(mode);

		attr.func = IMPISP_LDC_MODE;
		ret = IMP_ISP_LDC_OnlineInit(IMPVI_MAIN, &attr);
		ret = IMP_ISP_LDC_OnlineSetLut(IMPVI_MAIN, "/etc/sensor/ldc_lut_1920_1080_t32.bin");
	}
	if (g_t33_online_ldc_mode == 1) {
		IMPISPLDCMode mode = IMPISP_ONLINE_MODE;
		IMPISPLDCInitOnlineAttr attr;

		printf("Using T33 Online LDC mode!\n");
		ret = IMP_ISP_LDC_SetMode(mode);

		attr.func = IMPISP_LDC_MODE;
		attr.size = IMPISP_LUT_BLOCKSIZE_8x8;
		ret = IMP_ISP_LDC_OnlineInit(IMPVI_MAIN, &attr);
		ret = IMP_ISP_LDC_OnlineSetLut(IMPVI_MAIN, "/etc/sensor/ldc_lut_1920_1080_8_t33.bin");
	} else if (g_t33_online_ldc_mode == 2) {
		IMPISPLDCMode mode = IMPISP_ONLINE_MODE;
		IMPISPLDCInitOnlineAttr attr;

		printf("Using T33 Online I2D mode!\n");
		ret = IMP_ISP_LDC_SetMode(mode);

		attr.func  = IMPISP_I2D_MODE;
		attr.angle = IMPISP_LDC_I2D_180;
		ret = IMP_ISP_LDC_OnlineInit(IMPVI_MAIN, &attr);
	} else if (g_t33_online_ldc_mode == 3) {
		IMPISPLDCMode mode = IMPISP_ONLINE_MODE;
		IMPISPLDCInitOnlineAttr attr;

		printf("Using T33 Online HLDC mode!\n");
		ret = IMP_ISP_LDC_SetMode(mode);

		attr.func = IMPISP_HLDC_MODE;
		attr.size = IMPISP_LUT_BLOCKSIZE_8x8;
		ret = IMP_ISP_LDC_OnlineInit(IMPVI_MAIN, &attr);
		ret = IMP_ISP_LDC_OnlineSetLut(IMPVI_MAIN, "/etc/sensor/ldc_lut_1920_1080_8_t33.bin");
	}
	if ((g_t33_offline_ldc_mode == 1) || (g_t33_offline_ldc_mode == 2)) {
		IMPISPLDCMode mode = IMPISP_OFFLINE_MODE;
		IMPISPLDCOfflineOpsMode mode_state = IMPISP_Offline_ENABLE;

		printf("T33 Offline LDC Module enabled!\n");
		ret = IMP_ISP_LDC_SetMode(mode);
		ret = IMP_ISP_LDC_OfflineInit(mode_state);
	}

	ret = IMP_ISP_AddSensor(IMPVI_MAIN, &sensor_info[0]);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_ISP_AddSensor(%d) failed\n", IMPVI_MAIN);
		return -1;
	}
	if (mode.sensor_num > IMPISP_TOTAL_ONE) {
		ret = IMP_ISP_AddSensor(IMPVI_SEC, &sensor_info[1]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_AddSensor(%d) failed\n", IMPVI_SEC);
			return -1;
		}
	}
	if (mode.sensor_num > IMPISP_TOTAL_TWO) {
		ret = IMP_ISP_AddSensor(IMPVI_THR, &sensor_info[2]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_AddSensor(%d) failed\n", IMPVI_SEC);
			return -1;
		}
	}
	if (mode.sensor_num > IMPISP_TOTAL_THR) {
		ret = IMP_ISP_AddSensor(IMPVI_FOUR, &sensor_info[3]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_AddSensor(%d) failed\n", IMPVI_SEC);
			return -1;
		}
	}

	if(g_client_3a_test_enable){
		if(g_client_awb)
			IMP_ISP_SetAwbAlgoFunc(0, g_client_awb);

		if(g_client_ae)
			IMP_ISP_SetAeAlgoFunc(0, g_client_ae);
	}

	ret = IMP_ISP_EnableSensor(IMPVI_MAIN, &sensor_info[0]);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_ISP_EnableSensor(%d) failed\n", IMPVI_MAIN);
		return -1;
	}
	if (mode.sensor_num > IMPISP_TOTAL_ONE) {
		ret = IMP_ISP_EnableSensor(IMPVI_SEC, &sensor_info[1]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_EnableSensor(%d) failed\n", IMPVI_SEC);
			return -1;
		}
	}
	if (mode.sensor_num > IMPISP_TOTAL_TWO) {
		ret = IMP_ISP_EnableSensor(IMPVI_THR, &sensor_info[2]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_EnableSensor(%d) failed\n", IMPVI_SEC);
			return -1;
		}
	}
	if (mode.sensor_num > IMPISP_TOTAL_THR) {
		ret = IMP_ISP_EnableSensor(IMPVI_FOUR, &sensor_info[3]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_EnableSensor(%d) failed\n", IMPVI_SEC);
			return -1;
		}
	}

	ret = IMP_System_Init();
	if(ret < 0){
		IMP_LOG_ERR(TAG, "IMP_System_Init failed\n");
		return -1;
	}

	ret = IMP_ISP_EnableTuning();
	if(ret < 0){
		IMP_LOG_ERR(TAG, "IMP_ISP_EnableTuning failed\n");
		return -1;
	}
	unsigned char value = 128;
	IMP_ISP_Tuning_SetContrast(IMPVI_MAIN, &value);
	IMP_ISP_Tuning_SetSharpness(IMPVI_MAIN, &value);
	IMP_ISP_Tuning_SetSaturation(IMPVI_MAIN, &value);
	IMP_ISP_Tuning_SetBrightness(IMPVI_MAIN, &value);
	IMPISPRunningMode mode = IMPISP_RUNNING_MODE_DAY;
	ret = IMP_ISP_Tuning_SetISPRunningMode(IMPVI_MAIN, &mode);
	if (ret < 0){
		IMP_LOG_ERR(TAG, "failed to set running mode\n");
		return -1;
	}
	IMPISPSensorFps setFps = { FIRST_SENSOR_FRAME_RATE_NUM, FIRST_SENSOR_FRAME_RATE_DEN };
	ret = IMP_ISP_Tuning_SetSensorFPS(IMPVI_MAIN, &setFps);
	if (ret < 0){
		IMP_LOG_ERR(TAG, "failed to set sensor fps\n");
		return -1;
	}
	if (SENSOR_NUM > IMPISP_TOTAL_ONE) {
		setFps.num = SECOND_SENSOR_FRAME_RATE_NUM;
		setFps.den = SECOND_SENSOR_FRAME_RATE_DEN;
		ret = IMP_ISP_Tuning_SetSensorFPS(IMPVI_SEC, &setFps);
		if (ret < 0){
			IMP_LOG_ERR(TAG, "failed to set sensor fps\n");
			return -1;
		}
	}
	if (SENSOR_NUM > IMPISP_TOTAL_TWO) {
		setFps.num = THIRD_SENSOR_FRAME_RATE_NUM;
		setFps.den = THIRD_SENSOR_FRAME_RATE_DEN;
		ret = IMP_ISP_Tuning_SetSensorFPS(IMPVI_THR, &setFps);
		if (ret < 0){
			IMP_LOG_ERR(TAG, "failed to set sensor fps\n");
			return -1;
		}
	}
	if (SENSOR_NUM > IMPISP_TOTAL_THR) {
		setFps.num = FOURTH_SENSOR_FRAME_RATE_NUM;
		setFps.den = FOURTH_SENSOR_FRAME_RATE_DEN;
		ret = IMP_ISP_Tuning_SetSensorFPS(IMPVI_FOUR, &setFps);
		if (ret < 0){
			IMP_LOG_ERR(TAG, "failed to set sensor fps\n");
			return -1;
		}
	}

	IMP_LOG_DBG(TAG, "ImpSystemInit success\n");

	return 0;
}

int sample_system_exit()
{
	int ret = 0;

	IMP_LOG_DBG(TAG, "sample_system_exit start\n");

	IMP_System_Exit();

	ret = IMP_ISP_DisableSensor(IMPVI_MAIN);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_ISP_DisableSensor(%d) failed\n", IMPVI_MAIN);
		return -1;
	}
	if (mode.sensor_num > IMPISP_TOTAL_ONE) {
		ret = IMP_ISP_DisableSensor(IMPVI_SEC);
		if(ret < 0){
			IMP_LOG_ERR(TAG, "IMP_ISP_DisableSensor(%d) failed\n", IMPVI_SEC);
			return -1;
		}
	}
	if (mode.sensor_num > IMPISP_TOTAL_TWO) {
		ret = IMP_ISP_DisableSensor(IMPVI_THR);
		if(ret < 0){
			IMP_LOG_ERR(TAG, "IMP_ISP_DisableSensor(%d) failed\n", IMPVI_THR);
			return -1;
		}
	}
	if (mode.sensor_num > IMPISP_TOTAL_THR) {
		ret = IMP_ISP_DisableSensor(IMPVI_FOUR);
		if(ret < 0){
			IMP_LOG_ERR(TAG, "IMP_ISP_DisableSensor(%d) failed\n", IMPVI_FOUR);
			return -1;
		}
	}

	ret = IMP_ISP_DelSensor(IMPVI_MAIN, &sensor_info[0]);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_ISP_DelSensor(%d) failed\n", IMPVI_MAIN);
		return -1;
	}
	if (mode.sensor_num > IMPISP_TOTAL_ONE) {
		ret = IMP_ISP_DelSensor(IMPVI_SEC, &sensor_info[1]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_DelSensor(%d) failed\n", IMPVI_SEC);
			return -1;
		}
	}
	if (mode.sensor_num > IMPISP_TOTAL_TWO) {
		ret = IMP_ISP_DelSensor(IMPVI_THR, &sensor_info[2]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_DelSensor(%d) failed\n", IMPVI_THR);
			return -1;
		}
	}
	if (mode.sensor_num > IMPISP_TOTAL_THR) {
		ret = IMP_ISP_DelSensor(IMPVI_FOUR, &sensor_info[3]);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_DelSensor(%d) failed\n", IMPVI_FOUR);
			return -1;
		}
	}

	ret = IMP_ISP_DisableTuning();
	if(ret < 0){
		IMP_LOG_ERR(TAG, "IMP_ISP_DisableTuning failed\n");
		return -1;
	}

	if(IMP_ISP_Close()){
		IMP_LOG_ERR(TAG, "failed to open ISP\n");
		return -1;
	}

#ifdef MULTI_ENCODE
	IMP_Encoder_MultiProcessExit();
#endif

	IMP_LOG_DBG(TAG, " sample_system_exit success\n");

	return 0;
}
