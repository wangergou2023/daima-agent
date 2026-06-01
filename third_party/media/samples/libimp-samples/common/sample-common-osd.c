#include "imp/imp_isp.h"
#include "sample-common.h"
#include <stdint.h>
#include <sys/prctl.h>
#include "sample-common-osd.h"
#include "../osd/logodata_100x100_bgra.h"


#ifdef SUPPORT_RGB555LE
#include "../osd/bgramapinfo_rgb555le.h"
#else
#include "../osd/bgramapinfo.h"
#endif

#ifdef SUPPORT_RGB555LE
#include "../osd/bgramapinfo_rgb555le.h"
#else
#include "../osd/bitmapinfo.h"
#endif

#define TAG "sample-Common-Framesource"


#define OSD_LETTER_NUM 20
static char g_osdpath[128] = "/mnt/res/64x64_2.rgba";

typedef enum {
	ISP_OSD_PINUM_0 = 0,
	ISP_OSD_PINUM_1,
	ISP_OSD_PINUM_2,
	ISP_OSD_PINUM_3,
	ISP_OSD_PINUM_4,
	ISP_OSD_PINUM_5,
	ISP_OSD_PINUM_6,
	ISP_OSD_PINUM_7,
	ISP_OSD_PINUM_BUTT
} IMPISPOSD_PINUM;

static int pic_count = 0;
static int g_pichandle[SENSOR_NUM][ISP_OSD_PINUM_BUTT] = {0};
static int g_pichandle_multipe[SENSOR_NUM][ISP_OSD_PINUM_BUTT] = {0};//多osd合并

static void free_memory(void *arg) {
	free(arg);
}

void isposd_multipe_update_time(void *p)
{
	int i = 0, j = 0,k = 0;
	int ret = 0;
	/* generate time */
	char DateStr[40];
	time_t currTime;
	struct tm *currDate;
	void *dateData = NULL;
	uint32_t *data = NULL;
	const int max_lines = 8;//一个区域绘制几行
	const int line_height = OSD_REGION_HEIGHT;
	const int char_width = OSD_REGION_WIDTH;

	prctl(PR_SET_NAME, "sample_isposd_multipe");
#ifdef SUPPORT_RGB555LE
	data = calloc(1,OSD_LETTER_NUM * (max_lines * line_height) * char_width * sizeof(uint16_t));
#else
	data = calloc(1,OSD_LETTER_NUM * (max_lines * line_height) * char_width * sizeof(uint32_t));
#endif
	if (!data) {
		printf("calloc data failed!\n");
		return;
	}
	pthread_cleanup_push(free_memory, data);

	while (1) {
		int penpos_t = 0;
		int fontadv = 0;

		time(&currTime);
		currDate = localtime(&currTime);
		memset(DateStr, 0, 40);
		strftime(DateStr, 40, "%Y-%m-%d %I:%M:%S", currDate);

		for(k = 0;k < max_lines; k++) {
			penpos_t = 0;
			for (i = 0; i < OSD_LETTER_NUM; i++) {
				switch(DateStr[i]) {
					case '0' ... '9':
						dateData = (void *)gBgramap[DateStr[i] - '0'].pdata;
						fontadv = gBgramap[DateStr[i] - '0'].width;
						penpos_t += gBgramap[DateStr[i] - '0'].width;
						break;
					case '-':
						dateData = (void *)gBgramap[10].pdata;
						fontadv = gBgramap[10].width;
						penpos_t += gBgramap[10].width;
						break;
					case ' ':
						dateData = (void *)gBgramap[11].pdata;
						fontadv = gBgramap[11].width;
						penpos_t += gBgramap[11].width;
						break;
					case ':':
						dateData = (void *)gBgramap[12].pdata;
						fontadv = gBgramap[12].width;
						penpos_t += gBgramap[12].width;
						break;
					default:
						break;
				}

	#ifdef SUPPORT_RGB555LE
				for (j = 0; j < line_height; j++) {
					uint32_t *dst = (uint16_t *)data + (k * line_height + j) * (OSD_LETTER_NUM * char_width) + penpos_t;
					uint32_t *src = (uint16_t *)dateData + j * fontadv;
					memcpy(dst, src, fontadv * sizeof(uint16_t));
				}
	#else
				for (j = 0; j < line_height; j++) {
					uint32_t *dst = (uint32_t *)data + (k * line_height + j) * (OSD_LETTER_NUM * char_width) + penpos_t;
					uint32_t *src = (uint32_t *)dateData + j * fontadv;
					memcpy(dst, src, fontadv * sizeof(uint32_t));
				}
	#endif
			}
		}
		/*
		* 1. ISPOSD 图片叠加支持两组配置：pinum 0-3 归为 group0，pinum 4-7 归为 group1。
		* 2. 同组内所有pinum必须使用相同的叠加图片类型(例如group0中pinum0若设为ARGB_8888,该组其它pinum也需采用此类型);不同组可使用不同类型(如group1 可统一设置为ARGB_1555)。
		* 3. 两组间的pinum数据允许重叠，但同一组内的pinum数据不可重叠。
		* */
		/*
		* 1. ISPOSD image overlay supports two groups of configurations: pinum 0-3 are classified into group0, and pinum 4-7 are classified into group1.
		* 2. All pinums within the same group must use the same osd_type (for example, if pinum0 in group0 is set to ARGB_8888, other pinums in this group must also adopt this type); different groups can use different types (such as group1 can be uniformly set to ARGB_1555).
		* 3. Pinum data between the two groups is allowed to overlap, but pinum data within the same group must not overlap.
		* */
#ifdef SUPPORT_RGB555LE
		IMPIspOsdAttrAsm stISPOSDAsm = {0};
		for(i = 0; i < SENSOR_NUM; i++){
			stISPOSDAsm.type = ISP_OSD_REG_PIC;
			stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_type = IMP_ISP_PIC_ARGB_1555;
			stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_argb_type = IMP_ISP_ARGB_TYPE_BGRA;//IMP_ISP_ARGB_TYPE_ARGB;
			stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_pixel_alpha_disable = IMPISP_TUNING_OPS_MODE_DISABLE;
			stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_group = 0;
			stISPOSDAsm.stsinglepicAttr.pic.pinum = g_pichandle_multipe[i][ISP_OSD_PINUM_0];
			stISPOSDAsm.stsinglepicAttr.pic.osd_enable = 1;
			stISPOSDAsm.stsinglepicAttr.pic.osd_left = 10;
			stISPOSDAsm.stsinglepicAttr.pic.osd_top = 10;
			stISPOSDAsm.stsinglepicAttr.pic.osd_width = char_width * OSD_LETTER_NUM;
			stISPOSDAsm.stsinglepicAttr.pic.osd_height = line_height * max_lines;
			stISPOSDAsm.stsinglepicAttr.pic.osd_image = (char*)data;
			stISPOSDAsm.stsinglepicAttr.pic.osd_stride = char_width * OSD_LETTER_NUM * 2;

			ret = IMP_ISP_Tuning_SetOsdRgnAttr(i, g_pichandle_multipe[i][ISP_OSD_PINUM_0], &stISPOSDAsm);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_ISP_SetOSDAttr failed\n");
				return ;
			}

			ret = IMP_ISP_Tuning_ShowOsdRgn(i, g_pichandle_multipe[i][ISP_OSD_PINUM_0], ISPOSD_SHOWON);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn_ISP failed\n");
				return ;
			}
		}
#else
		IMPIspOsdAttrAsm stISPOSDAsm = {0};
		for(i = 0; i < SENSOR_NUM; i++){
			stISPOSDAsm.type = ISP_OSD_REG_PIC;
			stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_type = IMP_ISP_PIC_ARGB_8888;
			stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_argb_type = IMP_ISP_ARGB_TYPE_BGRA;
			stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_pixel_alpha_disable = IMPISP_TUNING_OPS_MODE_DISABLE;
			stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_group = 0;
			stISPOSDAsm.stsinglepicAttr.pic.pinum = g_pichandle_multipe[i][ISP_OSD_PINUM_0];
			stISPOSDAsm.stsinglepicAttr.pic.osd_enable = 1;
			stISPOSDAsm.stsinglepicAttr.pic.osd_left = 10;
			stISPOSDAsm.stsinglepicAttr.pic.osd_top = 10;
			stISPOSDAsm.stsinglepicAttr.pic.osd_width = char_width * OSD_LETTER_NUM;
			stISPOSDAsm.stsinglepicAttr.pic.osd_height = line_height * max_lines;
			stISPOSDAsm.stsinglepicAttr.pic.osd_image = (char*)data;
			stISPOSDAsm.stsinglepicAttr.pic.osd_stride = char_width * OSD_LETTER_NUM * sizeof(uint32_t);

			ret = IMP_ISP_Tuning_SetOsdRgnAttr(i, g_pichandle_multipe[i][ISP_OSD_PINUM_0], &stISPOSDAsm);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_ISP_SetOSDAttr failed,sensor:%d,handle:%d\n",i,g_pichandle_multipe[i][ISP_OSD_PINUM_0]);
				return ;
			}

			ret = IMP_ISP_Tuning_ShowOsdRgn(i, g_pichandle_multipe[i][ISP_OSD_PINUM_0], ISPOSD_SHOWON);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn_ISP failed\n");
				return ;
			}
		}
#endif

		/*更新时间戳*/
		/* Update Timestamp */
		sleep(1);
	}

	pthread_cleanup_pop(0);
	return;
}

void isposd_update_time(void *p)
{
	int i = 0, j = 0;
	int ret = 0;
	/* generate time */
	char DateStr[40];
	time_t currTime;
	struct tm *currDate;
	void *dateData = NULL;
	uint32_t *data = NULL;

	prctl(PR_SET_NAME, "sample_isposd");
#ifdef SUPPORT_RGB555LE
	data = calloc(1,OSD_LETTER_NUM * OSD_REGION_HEIGHT * OSD_REGION_WIDTH * sizeof(uint16_t));
#else
	data = calloc(1,OSD_LETTER_NUM * OSD_REGION_HEIGHT * OSD_REGION_WIDTH * sizeof(uint32_t));
#endif

	pthread_cleanup_push(free_memory, data);

	while (1) {
		int penpos_t = 0;
		int fontadv = 0;

		time(&currTime);
		currDate = localtime(&currTime);
		memset(DateStr, 0, 40);
		strftime(DateStr, 40, "%Y-%m-%d %I:%M:%S", currDate);
		for (i = 0; i < OSD_LETTER_NUM; i++) {
			switch(DateStr[i]) {
				case '0' ... '9':
					dateData = (void *)gBgramap[DateStr[i] - '0'].pdata;
					fontadv = gBgramap[DateStr[i] - '0'].width;
					penpos_t += gBgramap[DateStr[i] - '0'].width;
					break;
				case '-':
					dateData = (void *)gBgramap[10].pdata;
					fontadv = gBgramap[10].width;
					penpos_t += gBgramap[10].width;
					break;
				case ' ':
					dateData = (void *)gBgramap[11].pdata;
					fontadv = gBgramap[11].width;
					penpos_t += gBgramap[11].width;
					break;
				case ':':
					dateData = (void *)gBgramap[12].pdata;
					fontadv = gBgramap[12].width;
					penpos_t += gBgramap[12].width;
					break;
				default:
					break;
			}
#ifdef SUPPORT_RGB555LE
			for (j = 0; j < OSD_REGION_HEIGHT; j++) {
				memcpy((void *)((uint16_t *)data + j*OSD_LETTER_NUM*OSD_REGION_WIDTH + penpos_t),
						(void *)((uint16_t *)dateData + j*fontadv), fontadv*sizeof(uint16_t));
			}
#else
			for (j = 0; j < OSD_REGION_HEIGHT; j++) {
				memcpy((void *)((uint32_t *)data + j*OSD_LETTER_NUM*OSD_REGION_WIDTH + penpos_t),
						(void *)((uint32_t *)dateData + j*fontadv), fontadv*sizeof(uint32_t));
			}

#endif
		}
		/*
		* 1. ISPOSD 图片叠加支持两组配置：pinum 0-3 归为 group0，pinum 4-7 归为 group1。
		* 2. 同组内所有pinum必须使用相同的叠加图片类型(例如group0中pinum0若设为ARGB_8888,该组其它pinum也需采用此类型);不同组可使用不同类型(如group1 可统一设置为ARGB_1555)。
		* 3. 两组间的pinum数据允许重叠，但同一组内的pinum数据不可重叠。
		* */
		/*
		* 1. ISPOSD image overlay supports two groups of configurations: pinum 0-3 are classified into group0, and pinum 4-7 are classified into group1.
		* 2. All pinums within the same group must use the same osd_type (for example, if pinum0 in group0 is set to ARGB_8888, other pinums in this group must also adopt this type); different groups can use different types (such as group1 can be uniformly set to ARGB_1555).
		* 3. Pinum data between the two groups is allowed to overlap, but pinum data within the same group must not overlap.
		* */
#ifdef SUPPORT_RGB555LE
		IMPIspOsdAttrAsm stISPOSDAsm = {0};
		for(i = 0; i < SENSOR_NUM; i++){
			stISPOSDAsm.type = ISP_OSD_REG_PIC;
			stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_type = IMP_ISP_PIC_ARGB_1555;
			stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_argb_type = IMP_ISP_ARGB_TYPE_BGRA;//IMP_ISP_ARGB_TYPE_ARGB;
			stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_pixel_alpha_disable = IMPISP_TUNING_OPS_MODE_DISABLE;
			stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_group = 0;
			stISPOSDAsm.stsinglepicAttr.pic.pinum = g_pichandle[i][ISP_OSD_PINUM_0];
			stISPOSDAsm.stsinglepicAttr.pic.osd_enable = 1;
			stISPOSDAsm.stsinglepicAttr.pic.osd_left = 10;
			stISPOSDAsm.stsinglepicAttr.pic.osd_top = 10;
			stISPOSDAsm.stsinglepicAttr.pic.osd_width = OSD_REGION_WIDTH * OSD_LETTER_NUM;
			stISPOSDAsm.stsinglepicAttr.pic.osd_height = OSD_REGION_HEIGHT;
			stISPOSDAsm.stsinglepicAttr.pic.osd_image = (char*)data;
			stISPOSDAsm.stsinglepicAttr.pic.osd_stride = OSD_REGION_WIDTH * OSD_LETTER_NUM * 2;

			ret = IMP_ISP_Tuning_SetOsdRgnAttr(i, g_pichandle[i][ISP_OSD_PINUM_0], &stISPOSDAsm);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_ISP_SetOSDAttr failed\n");
				return ;
			}

			ret = IMP_ISP_Tuning_ShowOsdRgn(i, g_pichandle[i][ISP_OSD_PINUM_0], ISPOSD_SHOWON);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn_ISP failed\n");
				return ;
			}
		}
#else
		IMPIspOsdAttrAsm stISPOSDAsm = {0};
		for(i = 0; i < SENSOR_NUM; i++){
			stISPOSDAsm.type = ISP_OSD_REG_PIC;
			stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_type = IMP_ISP_PIC_ARGB_8888;
			stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_argb_type = IMP_ISP_ARGB_TYPE_BGRA;
			stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_pixel_alpha_disable = IMPISP_TUNING_OPS_MODE_DISABLE;
			stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_group = 0;
			stISPOSDAsm.stsinglepicAttr.pic.pinum = g_pichandle[i][ISP_OSD_PINUM_0];
			stISPOSDAsm.stsinglepicAttr.pic.osd_enable = 1;
			stISPOSDAsm.stsinglepicAttr.pic.osd_left = 10;
			stISPOSDAsm.stsinglepicAttr.pic.osd_top = 10;
			stISPOSDAsm.stsinglepicAttr.pic.osd_width = OSD_REGION_WIDTH * OSD_LETTER_NUM;
			stISPOSDAsm.stsinglepicAttr.pic.osd_height = OSD_REGION_HEIGHT;
			stISPOSDAsm.stsinglepicAttr.pic.osd_image = (char*)data;
			stISPOSDAsm.stsinglepicAttr.pic.osd_stride = OSD_REGION_WIDTH * OSD_LETTER_NUM * 4;

			ret = IMP_ISP_Tuning_SetOsdRgnAttr(i, g_pichandle[i][ISP_OSD_PINUM_0], &stISPOSDAsm);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_ISP_SetOSDAttr failed,sensor:%d,handle:%d\n",i,g_pichandle[i][ISP_OSD_PINUM_0]);
				return ;
			}

			ret = IMP_ISP_Tuning_ShowOsdRgn(i, g_pichandle[i][ISP_OSD_PINUM_0], ISPOSD_SHOWON);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn_ISP failed\n");
				return ;
			}
		}
#endif

		/*更新时间戳*/
		/* Update Timestamp */
		sleep(1);
	}

	pthread_cleanup_pop(0);
	return;
}

int datainit(void)
{
	int ret = 0;
	/*示例的图案宽度和高度为64*/
	/* The pattern width and height of the example in the sample are 64 */
	int w = 64, h = 64, size = 0;

	if(0 != access(g_osdpath, F_OK)){
		IMP_LOG_WARN(TAG, "%s is not exist,may touch fisrst\n",g_osdpath);
		return 0;
	}

	size = w*h*4;
	if ((g_pdata = calloc(1, size)) == NULL) {
		IMP_LOG_ERR(TAG, "calloc failed\n");
		return -1;
	}

	if ((g_fp = fopen(g_osdpath, "r")) == NULL) {
		IMP_LOG_ERR(TAG, "fopen failed\n");
		return -1;
	}

	ret = fread(g_pdata, 1, size, g_fp);
	if (ret <= 0) {
		IMP_LOG_ERR(TAG, "fread failed\n");
		return -1;
	}

	return 0;
}

int datadeinit(void)
{
	if(0 != access(g_osdpath, F_OK)){
		goto exit;
	}

	fclose(g_fp);
	free(g_pdata);
	g_pdata = NULL;
exit:
	return 0;
}

void draw_pic(void)
{
	int ret = 0,i = 0;
	IMPIspOsdAttrAsm stISPOSDAsm;
	memset(&stISPOSDAsm, 0, sizeof(IMPIspOsdAttrAsm));

	if(0 != access(g_osdpath, F_OK)){
		return ;
	}

	/*
	* 1. ISPOSD 图片叠加支持两组配置：pinum 0-3 归为 group0，pinum 4-7 归为 group1。
	* 2. 同组内所有pinum必须使用相同的叠加图片类型(例如group0中pinum0若设为ARGB_8888,该组其它pinum也需采用此类型);不同组可使用不同类型(如group1 可统一设置为ARGB_1555)。
	* 3. 两组间的pinum数据允许重叠，但同一组内的pinum数据不可重叠。
	* */
	/*
	* 1. ISPOSD image overlay supports two groups of configurations: pinum 0-3 are classified into group0, and pinum 4-7 are classified into group1.
	* 2. All pinums within the same group must use the same osd_type (for example, if pinum0 in group0 is set to ARGB_8888, other pinums in this group must also adopt this type); different groups can use different types (such as group1 can be uniformly set to ARGB_1555).
	* 3. Pinum data between the two groups is allowed to overlap, but pinum data within the same group must not overlap.
	* */
	for(i = 0; i < SENSOR_NUM; i++){
		stISPOSDAsm.type = ISP_OSD_REG_PIC;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_type = IMP_ISP_PIC_ARGB_8888;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_argb_type = IMP_ISP_ARGB_TYPE_BGRA;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_pixel_alpha_disable = IMPISP_TUNING_OPS_MODE_DISABLE;
		stISPOSDAsm.stsinglepicAttr.chnOSDAttr.osd_group = 0;
		stISPOSDAsm.stsinglepicAttr.pic.pinum = g_pichandle[i][ISP_OSD_PINUM_1];
		stISPOSDAsm.stsinglepicAttr.pic.osd_enable = 1;
		stISPOSDAsm.stsinglepicAttr.pic.osd_left = 100;
		stISPOSDAsm.stsinglepicAttr.pic.osd_top = 500;
		stISPOSDAsm.stsinglepicAttr.pic.osd_width = 64;
		stISPOSDAsm.stsinglepicAttr.pic.osd_height = 64;
		stISPOSDAsm.stsinglepicAttr.pic.osd_image = g_pdata;
		stISPOSDAsm.stsinglepicAttr.pic.osd_stride = 64*4;

		ret = IMP_ISP_Tuning_SetOsdRgnAttr(i, g_pichandle[i][ISP_OSD_PINUM_1], &stISPOSDAsm);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_Tuning_SetOsdRgnAttr failed,sensor:%d,handle:%d\n",i,g_pichandle[i][ISP_OSD_PINUM_1]);
			return;
		}

		ret = IMP_ISP_Tuning_ShowOsdRgn(i, g_pichandle[i][ISP_OSD_PINUM_1], ISPOSD_SHOWON);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_ISP_Tuning_ShowOsdRgn failed\n");
			return;
		}
	}
	return;
}

void ISPOSDDraw(IMPOsdRgnType type)
{
	int ret = 0;
	memset(&rIspOsdAttr, 0, sizeof(IMPOSDRgnAttr));

	if (OSD_REG_ISP_LINE_RECT == type) {
		rIspOsdAttr.type = OSD_REG_ISP_LINE_RECT;
		rIspOsdAttr.osdispdraw.stDrawAttr.pinum = 0;
		rIspOsdAttr.osdispdraw.stDrawAttr.type = IMP_ISP_DRAW_LINE;
		rIspOsdAttr.osdispdraw.stDrawAttr.color_type = IMPISP_MASK_TYPE_YUV;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.enable = 1;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.startx = 200; /* Draw vertical lines */
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.starty = 200;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.endx = 800;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.endy = 200;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.color.ayuv.y_value = 255;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.color.ayuv.u_value = 0;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.color.ayuv.v_value = 0;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.width = 5;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.line.alpha = 4; /* The range is [0,4], and the smaller the value, the more transparent it becomes */

		ret = IMP_OSD_SetRgnAttr_ISP(IMPVI_MAIN, &rIspOsdAttr, 1);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr_ISP failed\n");
		}

		if (SENSOR_NUM > IMPISP_TOTAL_ONE) {
			ret = IMP_OSD_SetRgnAttr_ISP(IMPVI_SEC, &rIspOsdAttr, 1);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr_ISP failed\n");
			}
		}

		if (SENSOR_NUM > IMPISP_TOTAL_TWO) {
			ret = IMP_OSD_SetRgnAttr_ISP(IMPVI_THR, &rIspOsdAttr, 1);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr_ISP failed\n");
			}
		}

		if (SENSOR_NUM > IMPISP_TOTAL_THR) {
			ret = IMP_OSD_SetRgnAttr_ISP(IMPVI_FOUR, &rIspOsdAttr, 1);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr_ISP failed\n");
			}
		}
	}

	if (OSD_REG_ISP_LINE_RECT == type) {
		rIspOsdAttr.type = OSD_REG_ISP_LINE_RECT;
		rIspOsdAttr.osdispdraw.stDrawAttr.pinum = 1;
		rIspOsdAttr.osdispdraw.stDrawAttr.type = IMP_ISP_DRAW_RANGE;
		rIspOsdAttr.osdispdraw.stDrawAttr.color_type = IMPISP_MASK_TYPE_YUV;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.rang.enable = 1;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.rang.left = 1200; /* Draw vertical lines */
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.rang.top = 600;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.rang.width = 300;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.rang.height = 200;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.rang.color.ayuv.y_value = 255;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.rang.color.ayuv.u_value = 0;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.rang.color.ayuv.v_value = 0;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.rang.line_width = 5;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.rang.extend = 50;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.rang.alpha = 4; /* The range is [0,4], and the smaller the value, the more transparent it becomes */

		ret = IMP_OSD_SetRgnAttr_ISP(IMPVI_MAIN, &rIspOsdAttr, 1);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr_ISP failed\n");
		}

		if (SENSOR_NUM > IMPISP_TOTAL_ONE) {
			ret = IMP_OSD_SetRgnAttr_ISP(IMPVI_SEC, &rIspOsdAttr, 1);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr_ISP failed\n");
			}
		}

		if (SENSOR_NUM > IMPISP_TOTAL_TWO) {
			ret = IMP_OSD_SetRgnAttr_ISP(IMPVI_THR, &rIspOsdAttr, 1);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr_ISP failed\n");
			}
		}

		if (SENSOR_NUM > IMPISP_TOTAL_THR) {
			ret = IMP_OSD_SetRgnAttr_ISP(IMPVI_FOUR, &rIspOsdAttr, 1);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr_ISP failed\n");
			}
		}
	}

	if (OSD_REG_ISP_LINE_RECT == type) {
		rIspOsdAttr.type = OSD_REG_ISP_LINE_RECT;
		rIspOsdAttr.osdispdraw.stDrawAttr.pinum = 2;
		rIspOsdAttr.osdispdraw.stDrawAttr.type = IMP_ISP_DRAW_WIND;
		rIspOsdAttr.osdispdraw.stDrawAttr.color_type = IMPISP_MASK_TYPE_YUV;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.enable = 1;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.left = 900;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.top = 200;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.width = 300;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.height = 300;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.color.ayuv.y_value = 0;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.color.ayuv.u_value = 255;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.color.ayuv.v_value = 0;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.line_width = 3;
		rIspOsdAttr.osdispdraw.stDrawAttr.cfg.wind.alpha = 4;

		ret = IMP_OSD_SetRgnAttr_ISP(IMPVI_MAIN, &rIspOsdAttr, 1);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr_ISP failed\n");
		}

		if (SENSOR_NUM > IMPISP_TOTAL_ONE) {
			ret = IMP_OSD_SetRgnAttr_ISP(IMPVI_SEC, &rIspOsdAttr, 1);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr_ISP failed\n");
			}
		}

		if (SENSOR_NUM > IMPISP_TOTAL_TWO) {
			ret = IMP_OSD_SetRgnAttr_ISP(IMPVI_THR, &rIspOsdAttr, 1);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr_ISP failed\n");
			}
		}

		if (SENSOR_NUM > IMPISP_TOTAL_THR) {
			ret = IMP_OSD_SetRgnAttr_ISP(IMPVI_FOUR, &rIspOsdAttr, 1);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr_ISP failed\n");
			}
		}
	}

	if (OSD_REG_ISP_COVER == type) {
		rIspOsdAttr.type = OSD_REG_ISP_COVER;
		rIspOsdAttr.osdispdraw.stCoverAttr.chx = 0;
		rIspOsdAttr.osdispdraw.stCoverAttr.pinum = 0;
		rIspOsdAttr.osdispdraw.stCoverAttr.mask_en = 1;
		rIspOsdAttr.osdispdraw.stCoverAttr.mask_pos_top	= 600;
		rIspOsdAttr.osdispdraw.stCoverAttr.mask_pos_left = 600;
		rIspOsdAttr.osdispdraw.stCoverAttr.mask_width = 300;
		rIspOsdAttr.osdispdraw.stCoverAttr.mask_height = 300;
		rIspOsdAttr.osdispdraw.stCoverAttr.mask_type = IMPISP_MASK_TYPE_RGB;
		rIspOsdAttr.osdispdraw.stCoverAttr.mask_value.argb.r_value = 0;
		rIspOsdAttr.osdispdraw.stCoverAttr.mask_value.argb.g_value = 0;
		rIspOsdAttr.osdispdraw.stCoverAttr.mask_value.argb.b_value = 255;

		ret = IMP_OSD_SetRgnAttr_ISP(IMPVI_MAIN, &rIspOsdAttr, 1);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr_ISP failed\n");
		}

		if (SENSOR_NUM > IMPISP_TOTAL_ONE) {
			ret = IMP_OSD_SetRgnAttr_ISP(IMPVI_SEC, &rIspOsdAttr, 1);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr_ISP failed\n");
			}
		}

		if (SENSOR_NUM > IMPISP_TOTAL_TWO) {
			ret = IMP_OSD_SetRgnAttr_ISP(IMPVI_THR, &rIspOsdAttr, 1);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr_ISP failed\n");
			}
		}

		if (SENSOR_NUM > IMPISP_TOTAL_THR) {
			ret = IMP_OSD_SetRgnAttr_ISP(IMPVI_FOUR, &rIspOsdAttr, 1);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr_ISP failed\n");
			}
		}
	}

	return;
}

void* sample_isposd_draw(void *arg)
{
	/*绘制线、框、和矩形遮挡*/
	/* Draw lines, boxes, and rectangles to obscure */
	ISPOSDDraw(OSD_REG_ISP_LINE_RECT);
	ISPOSDDraw(OSD_REG_ISP_COVER);

	/*绘制图像，注意绘制图像类型的ISP接口与绘制线条、方框和矩形遮挡的接口之间的区别*/
	/* Draw an image, paying attention to the difference between the ISP interface for drawing image types and the interface for drawing lines, boxes, and rectangles for occlusion */
	draw_pic();

	/*绘制时间戳*/
	/* Draw timestamp */
	isposd_update_time(NULL);

	return NULL;
}

void* sample_isposd_multipe_draw(void *arg)
{
	/*绘制线、框、和矩形遮挡*/
	/* Draw lines, boxes, and rectangles to obscure */
	ISPOSDDraw(OSD_REG_ISP_LINE_RECT);
	ISPOSDDraw(OSD_REG_ISP_COVER);

	/*绘制图像，注意绘制图像类型的ISP接口与绘制线条、方框和矩形遮挡的接口之间的区别*/
	/* Draw an image, paying attention to the difference between the ISP interface for drawing image types and the interface for drawing lines, boxes, and rectangles for occlusion */
	// draw_pic();

	/*绘制时间戳*/
	/* Draw timestamp */
	isposd_multipe_update_time(NULL);

	return NULL;
}

int sample_isposd_multipe_init(int sensor_num)
{
	g_pichandle_multipe[sensor_num][ISP_OSD_PINUM_0] = IMP_ISP_Tuning_CreateOsdRgn(sensor_num, NULL);
	if (g_pichandle_multipe[sensor_num][ISP_OSD_PINUM_0] < 0) {
		IMP_LOG_ERR(TAG, "IMP_ISP_Tuning_CreateOsdRgn failed\n");
		return -1;
	}

	g_pichandle_multipe[sensor_num][ISP_OSD_PINUM_1] = IMP_ISP_Tuning_CreateOsdRgn(sensor_num, NULL);
	if (g_pichandle_multipe[sensor_num][ISP_OSD_PINUM_1] < 0) {
		IMP_LOG_ERR(TAG, "IMP_ISP_Tuning_CreateOsdRgn failed\n");
		return -1;
	}
	return 0;
}

int sample_isposd_multipe_exit(int sensor_num)
{
	int ret = 0;
	int showflg = 0;

	IMP_ISP_Tuning_ShowOsdRgn(sensor_num, g_pichandle_multipe[sensor_num][ISP_OSD_PINUM_0], showflg);
	IMP_ISP_Tuning_DestroyOsdRgn(sensor_num, g_pichandle_multipe[sensor_num][ISP_OSD_PINUM_0]);

	IMP_ISP_Tuning_ShowOsdRgn(sensor_num, g_pichandle_multipe[sensor_num][ISP_OSD_PINUM_1], showflg);
	IMP_ISP_Tuning_DestroyOsdRgn(sensor_num, g_pichandle_multipe[sensor_num][ISP_OSD_PINUM_1]);

	return ret;
}

int sample_isposd_init(int sensor_num)
{
	int ret = 0;

	if(pic_count == 0)
	{
		ret = datainit();
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "datainit failed\n");
			return -1;
		}
		pic_count++;
	}

	g_pichandle[sensor_num][ISP_OSD_PINUM_0] = IMP_ISP_Tuning_CreateOsdRgn(sensor_num, NULL);
	if (g_pichandle[sensor_num][ISP_OSD_PINUM_0] < 0) {
		IMP_LOG_ERR(TAG, "IMP_ISP_Tuning_CreateOsdRgn failed\n");
		return -1;
	}

	g_pichandle[sensor_num][ISP_OSD_PINUM_1] = IMP_ISP_Tuning_CreateOsdRgn(sensor_num, NULL);
	if (g_pichandle[sensor_num][ISP_OSD_PINUM_1] < 0) {
		IMP_LOG_ERR(TAG, "IMP_ISP_Tuning_CreateOsdRgn failed\n");
		return -1;
	}

	IMP_LOG_ERR(TAG,"ISPOSD sensor:%d handle: pinum0=%d, pinum1=%d\n",sensor_num,
		g_pichandle[sensor_num][ISP_OSD_PINUM_0],
		g_pichandle[sensor_num][ISP_OSD_PINUM_1]);

	return ret;
}

int sample_isposd_exit(int sensor_num)
{
	int ret = 0;
	int showflg = 0;
	if(pic_count != 0){
		ret = datadeinit();
		pic_count = 0;
	}

	IMP_ISP_Tuning_ShowOsdRgn(sensor_num, g_pichandle[sensor_num][ISP_OSD_PINUM_0], showflg);
	IMP_ISP_Tuning_DestroyOsdRgn(sensor_num, g_pichandle[sensor_num][ISP_OSD_PINUM_0]);

	IMP_ISP_Tuning_ShowOsdRgn(sensor_num, g_pichandle[sensor_num][ISP_OSD_PINUM_1], showflg);
	IMP_ISP_Tuning_DestroyOsdRgn(sensor_num, g_pichandle[sensor_num][ISP_OSD_PINUM_1]);

	return ret;
}


static int osd_show(int grpNum)
{
	int ret = 0;

	ret = IMP_OSD_ShowRgn(prHander[grpNum][0], grpNum, 1);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn timeStamp failed\n");
		return -1;
	}

	if(g_buse2bit){
		ret = IMP_OSD_ShowRgn(prHander[grpNum][1], grpNum, 1);
		if (ret != 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn Logo failed\n");
			return -1;
		}
	}

	ret = IMP_OSD_ShowRgn(prHander[grpNum][2], grpNum, 1);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn Logo failed\n");
		return -1;
	}

	ret = IMP_OSD_ShowRgn(prHander[grpNum][3], grpNum, 1);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn Logo failed\n");
		return -1;
	}

	ret = IMP_OSD_ShowRgn(prHander[grpNum][4], grpNum, 1);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn Logo failed\n");
		return -1;
	}

	return 0;
}


static void ipuosd_update_time(int chn)
{
	int j = 0;
	int ret;
	int grpNum = chn;

	char DateStr[40];
	time_t currTime;
	struct tm *currDate;
	void *dateData = NULL;

	char *data = NULL;
	IMPOSDRgnAttr rAttr;

	prctl(PR_SET_NAME, "sample_ipuosd");

	while(1) {
		int penpos = 0;
		int penpos_t = 0;
		int fontadv = 0;
		unsigned int len = 0;
		int i = 0;

		ret = IMP_OSD_GetRgnAttr(prHander[grpNum][0], &rAttr);
		if(ret){
			return;
		}

		data = (void *)rAttr.data.bitmapData;
		time(&currTime);
		currDate = localtime(&currTime);
		memset(DateStr, 0, 40);
		strftime(DateStr, 40, "%Y-%m-%d %I:%M:%S", currDate);
		len = strlen(DateStr);
		for (i = 0; i < len; i++) {
#ifndef SUPPORT_RGB555LE
			switch (DateStr[i]) {
			case '0' ... '9':
#ifdef SUPPORT_COLOR_REVERSE
				if (rAttr.fontData.colType[i] == 1) {
					dateData = (void *)gBitmap_black[DateStr[i] - '0'].pdata;
				} else {
					dateData = (void *)gBitmap[DateStr[i] - '0'].pdata;
				}
#else
				dateData = (void *)gBitmap[DateStr[i] - '0'].pdata;
#endif
				fontadv = gBitmap[DateStr[i] - '0'].width;
				penpos_t += gBitmap[DateStr[i] - '0'].width;
				break;
			case '-':
#ifdef SUPPORT_COLOR_REVERSE
				if (rAttr.fontData.colType[i] == 1) {
					dateData = (void *)gBitmap_black[10].pdata;
				} else {
					dateData = (void *)gBitmap[10].pdata;
				}
#else
				dateData = (void *)gBitmap[10].pdata;
#endif
				fontadv = gBitmap[10].width;
				penpos_t += gBitmap[10].width;
				break;
			case ' ':
				dateData = (void *)gBitmap[11].pdata;
				fontadv = gBitmap[11].width;
				penpos_t += gBitmap[11].width;
				break;
			case ':':
#ifdef SUPPORT_COLOR_REVERSE
				if (rAttr.fontData.colType[i] == 1) {
					dateData = (void *)gBitmap_black[12].pdata;
				} else {
					dateData = (void *)gBitmap[12].pdata;
				}
#else
				dateData = (void *)gBitmap[12].pdata;
#endif
				fontadv = gBitmap[12].width;
				penpos_t += gBitmap[12].width;
				break;
			default:
				break;
			}
#else
		switch(DateStr[i]) {
			case '0' ... '9':
				dateData = (void *)gBgramap[DateStr[i] - '0'].pdata;
				fontadv = gBgramap[DateStr[i] - '0'].width;
				penpos_t += gBgramap[DateStr[i] - '0'].width;
				break;
			case '-':
				dateData = (void *)gBgramap[10].pdata;
				fontadv = gBgramap[10].width;
				penpos_t += gBgramap[10].width;
				break;
			case ' ':
				dateData = (void *)gBgramap[11].pdata;
				fontadv = gBgramap[11].width;
				penpos_t += gBgramap[11].width;
				break;
			case ':':
				dateData = (void *)gBgramap[12].pdata;
				fontadv = gBgramap[12].width;
				penpos_t += gBgramap[12].width;
				break;
			default:
				break;
		}
#endif
#ifdef SUPPORT_RGB555LE
			for (j = 0; j < OSD_REGION_HEIGHT; j++) {
				memcpy((void *)((uint16_t *)data + j*OSD_LETTER_NUM*OSD_REGION_WIDTH + penpos),
						(void *)((uint16_t *)dateData + j*fontadv), fontadv*sizeof(uint16_t));
			}
#else
			for (j = 0; j < gBitmapHight; j++) {
				memcpy((void *)(data + j*OSD_LETTER_NUM*OSD_REGION_WIDTH + penpos),
				       (void *)(dateData + j*fontadv), fontadv);
			}
#endif
			penpos = penpos_t;
		}

		sleep(1);
	}

	return ;
}

void *sample_ipuosd_draw(void *arg)
{
	int ret;
	int grpnum = (int)arg;

	ret = osd_show(grpnum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "OSD show failed\n");
		return NULL;
	}

	ipuosd_update_time(grpnum);

	return "succeed";
}

int sample_ipuosd_init(int grpNum)
{
	int ret = 0;
	IMPRgnHandle rHanderFont = 0;
	IMPRgnHandle rHanderLogo = 0;
	IMPRgnHandle rHanderCover = 0;
	IMPRgnHandle rHanderRect = 0;
	IMPRgnHandle rHanderLine = 0;

	rHanderFont = IMP_OSD_CreateRgn(NULL);
	if (rHanderFont == INVHANDLE) {
		IMP_LOG_ERR(TAG, "IMP_OSD_CreateRgn TimeStamp failed\n");
		return -1;
	}

	IMPOSDRgnCreateStat stStatus;
	memset(&stStatus, 0x0, sizeof(IMPOSDRgnCreateStat));
	ret = IMP_OSD_RgnCreate_Query(rHanderFont, &stStatus);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_RgnCreate_Query failed\n");
		return -1;
	}

	rHanderLogo = IMP_OSD_CreateRgn(NULL);
	if (rHanderLogo == INVHANDLE) {
		IMP_LOG_ERR(TAG, "IMP_OSD_CreateRgn Logo failed\n");
		return -1;
	}

	rHanderCover = IMP_OSD_CreateRgn(NULL);
	if (rHanderCover == INVHANDLE) {
		IMP_LOG_ERR(TAG, "IMP_OSD_CreateRgn Cover failed\n");
		return -1;
	}

	rHanderRect = IMP_OSD_CreateRgn(NULL);
	if (rHanderRect == INVHANDLE) {
		IMP_LOG_ERR(TAG, "IMP_OSD_CreateRgn Rect failed\n");
		return -1;
	}

	rHanderLine = IMP_OSD_CreateRgn(NULL);
	if (rHanderLine == INVHANDLE) {
		IMP_LOG_ERR(TAG, "IMP_OSD_CreateRgn Line failed\n");
		return -1;
	}

	ret = IMP_OSD_RegisterRgn(rHanderFont, grpNum, NULL);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IVS IMP_OSD_RegisterRgn failed\n");
		return -1;
	}

	/* query osd rgn register status */
	IMPOSDRgnRegisterStat stRigStatus;
	memset(&stRigStatus, 0x0, sizeof(IMPOSDRgnRegisterStat));
	ret = IMP_OSD_RgnRegister_Query(rHanderFont, grpNum,&stRigStatus);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_RgnRegister_Query failed\n");
		return -1;
	}

	ret = IMP_OSD_RegisterRgn(rHanderLogo, grpNum, NULL);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IVS IMP_OSD_RegisterRgn failed\n");
		return -1;
	}

	ret = IMP_OSD_RegisterRgn(rHanderCover, grpNum, NULL);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IVS IMP_OSD_RegisterRgn failed\n");
		return -1;
	}

	ret = IMP_OSD_RegisterRgn(rHanderRect, grpNum, NULL);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IVS IMP_OSD_RegisterRgn failed\n");
		return -1;
	}

	ret = IMP_OSD_RegisterRgn(rHanderLine, grpNum, NULL);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IVS IMP_OSD_RegisterRgn failed\n");
		return -1;
	}

	/* Font */
	IMPOSDRgnAttr rAttrFont;
	memset(&rAttrFont, 0, sizeof(IMPOSDRgnAttr));
	rAttrFont.rect.p0.x = 10;
	rAttrFont.rect.p0.y = 100;
	rAttrFont.rect.p1.x = rAttrFont.rect.p0.x + OSD_LETTER_NUM * OSD_REGION_WIDTH- 1; /* p0 is start，and p1 well be epual p0+width(or heigth)-1 */
	rAttrFont.rect.p1.y = rAttrFont.rect.p0.y + OSD_REGION_HEIGHT - 1;
#ifdef SUPPORT_RGB555LE
	rAttrFont.type = OSD_REG_PIC;
	rAttrFont.fmt = PIX_FMT_RGB555LE;
#else
	rAttrFont.type = OSD_REG_BITMAP;
	rAttrFont.fmt = PIX_FMT_MONOWHITE;
#endif

	rAttrFont.data.bitmapData = calloc(1,OSD_LETTER_NUM * OSD_REGION_WIDTH * OSD_REGION_HEIGHT * sizeof(uint32_t));
	if (rAttrFont.data.bitmapData == NULL) {
		IMP_LOG_ERR(TAG, "alloc rAttr.data.bitmapData TimeStamp failed\n");
		return -1;
	}
	ret = IMP_OSD_SetRgnAttr(rHanderFont, &rAttrFont);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr TimeStamp failed\n");
		return -1;
	}
	IMPOSDGrpRgnAttr grAttrFont;
	if (IMP_OSD_GetGrpRgnAttr(rHanderFont, grpNum, &grAttrFont) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_GetGrpRgnAttr Logo failed\n");
		return -1;
	}
	memset(&grAttrFont, 0, sizeof(IMPOSDGrpRgnAttr));
	grAttrFont.show = 0;
	/* Disable Font global alpha, only use pixel alpha. */
	grAttrFont.gAlphaEn = 0;
	grAttrFont.fgAlhpa = 0xff;
	grAttrFont.layer = 3;
	if (IMP_OSD_SetGrpRgnAttr(rHanderFont, grpNum, &grAttrFont) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetGrpRgnAttr Logo failed\n");
		return -1;
	}


	if(g_buse2bit){
		int j = 0;
		for(j = 0; j < 20; j++) {
			memset(bitdata_100x100 + j * 200, 0xff, 100);
		}

		/* Logo */
		IMPOSDRgnAttr rAttrLogo;
		memset(&rAttrLogo, 0, sizeof(IMPOSDRgnAttr));
		int picw = 100;
		int pich = 100;
		rAttrLogo.type = OSD_REG_PIC;
		rAttrLogo.rect.p0.x = 100;
		rAttrLogo.rect.p0.y = 100;
		/* p0 is start，and p1 well be epual p0+width(or heigth)-1 */
		rAttrLogo.rect.p1.x = rAttrLogo.rect.p0.x+picw-1;
		rAttrLogo.rect.p1.y = rAttrLogo.rect.p0.y+pich-1;
		rAttrLogo.fmt = PIX_FMT_2BIT;
		rAttrLogo.data.picData.pData = bitdata_100x100;

		rAttrLogo.bitAttr.twobit_mask = 0x0;
		rAttrLogo.bitAttr.twobit_bit0_fmt = 0x7F0000FF;
		rAttrLogo.bitAttr.twobit_bit1_fmt = 0x7F00FF00;

		ret = IMP_OSD_SetRgnAttr(rHanderLogo, &rAttrLogo);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr Logo failed\n");
			return -1;
		}
		IMPOSDGrpRgnAttr grAttrLogo;
		if (IMP_OSD_GetGrpRgnAttr(rHanderLogo, grpNum, &grAttrLogo) < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_GetGrpRgnAttr Logo failed\n");
			return -1;
		}
		memset(&grAttrLogo, 0, sizeof(IMPOSDGrpRgnAttr));
		grAttrLogo.show = 0;
		grAttrLogo.gAlphaEn = 1;
		grAttrLogo.fgAlhpa = 0x7f;
		grAttrLogo.layer = 2;
		if (IMP_OSD_SetGrpRgnAttr(rHanderLogo, grpNum, &grAttrLogo) < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_SetGrpRgnAttr Logo failed\n");
			return -1;
		}
	}

	/* Cover */
	IMPOSDRgnAttr rAttrCover;
	memset(&rAttrCover, 0, sizeof(IMPOSDRgnAttr));
	rAttrCover.type = OSD_REG_COVER;
	rAttrCover.rect.p0.x = 300;
	rAttrCover.rect.p0.y = 300;
	rAttrCover.rect.p1.x = rAttrCover.rect.p0.x+300 -1;
	rAttrCover.rect.p1.y = rAttrCover.rect.p0.y+280 -1 ;
	rAttrCover.fmt = PIX_FMT_BGRA;
	rAttrCover.data.coverData.color = OSD_IPU_RED;
	ret = IMP_OSD_SetRgnAttr(rHanderCover, &rAttrCover);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr Cover failed\n");
		return -1;
	}
	IMPOSDGrpRgnAttr grAttrCover;
	if (IMP_OSD_GetGrpRgnAttr(rHanderCover, grpNum, &grAttrCover) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_GetGrpRgnAttr Cover failed\n");
		return -1;
	}
	memset(&grAttrCover, 0, sizeof(IMPOSDGrpRgnAttr));
	grAttrCover.show = 0;
	grAttrCover.gAlphaEn = 1;
	grAttrCover.fgAlhpa = 0x7f;
	grAttrCover.layer = 2;
	if (IMP_OSD_SetGrpRgnAttr(rHanderCover, grpNum, &grAttrCover) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetGrpRgnAttr Cover failed\n");
		return -1;
	}

	/* Rect */
	IMPOSDRgnAttr rAttrRect;
	memset(&rAttrRect, 0, sizeof(IMPOSDRgnAttr));
	rAttrRect.type = OSD_REG_RECT;
	rAttrRect.rect.p0.x = 400;
	rAttrRect.rect.p0.y = 400;
	rAttrRect.rect.p1.x = rAttrRect.rect.p0.x + 300 - 1;
	rAttrRect.rect.p1.y = rAttrRect.rect.p0.y + 300 - 1;
	rAttrRect.fmt = PIX_FMT_MONOWHITE;
	rAttrRect.data.lineRectData.color = OSD_IPU_RED;
	rAttrRect.data.lineRectData.linewidth = 5;
	ret = IMP_OSD_SetRgnAttr(rHanderRect, &rAttrRect);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr Rect failed\n");
		return -1;
	}
	IMPOSDGrpRgnAttr grAttrRect;
	if (IMP_OSD_GetGrpRgnAttr(rHanderRect, grpNum, &grAttrRect) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_GetGrpRgnAttr Rect failed\n");
		return -1;
	}
	memset(&grAttrRect, 0, sizeof(IMPOSDGrpRgnAttr));
	grAttrRect.show = 0;
	grAttrRect.layer = 1;
	grAttrRect.scalex = 1;
	grAttrRect.scaley = 1;
	if (IMP_OSD_SetGrpRgnAttr(rHanderRect, grpNum, &grAttrRect) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetGrpRgnAttr Rect failed\n");
		return -1;
	}

	/* Line */
	IMPOSDRgnAttr rAttrLine;
	memset(&rAttrLine, 0, sizeof(IMPOSDRgnAttr));
	rAttrLine.type = OSD_REG_HORIZONTAL_LINE;
	rAttrLine.rect.p0.x = 300;
	rAttrLine.rect.p0.y = 300;
	rAttrLine.rect.p1.x = rAttrLine.rect.p0.x + 300 - 1;
	rAttrLine.rect.p1.y = rAttrLine.rect.p0.y;
	rAttrLine.fmt = PIX_FMT_MONOWHITE;
	rAttrLine.data.lineRectData.color = OSD_IPU_GREEN;
	rAttrLine.data.lineRectData.linewidth = 5;
	ret = IMP_OSD_SetRgnAttr(rHanderLine, &rAttrLine);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetRgnAttr Line failed\n");
		return -1;
	}
	IMPOSDGrpRgnAttr grAttrLine;
	if (IMP_OSD_GetGrpRgnAttr(rHanderLine, grpNum, &grAttrLine) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_GetGrpRgnAttr Line failed\n");
		return -1;
	}
	memset(&grAttrLine, 0, sizeof(IMPOSDGrpRgnAttr));
	grAttrLine.show = 0;
	grAttrLine.layer = 1;
	grAttrLine.scalex = 1;
	grAttrLine.scaley = 1;
	if (IMP_OSD_SetGrpRgnAttr(rHanderLine, grpNum, &grAttrLine) < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_SetGrpRgnAttr Line failed\n");
		return -1;
	}

	ret = IMP_OSD_Start(grpNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_Start TimeStamp, Logo, Cover and Rect failed\n");
		return -1;
	}

	prHander[grpNum][0] = rHanderFont;
	if(g_buse2bit){
		prHander[grpNum][1] = rHanderLogo;
	}
	prHander[grpNum][2] = rHanderCover;
	prHander[grpNum][3] = rHanderRect;
	prHander[grpNum][4] = rHanderLine;

	return 0;
}

int sample_ipuosd_exit(int grpNum)
{
	int ret = 0;

	ret = IMP_OSD_ShowRgn(prHander[grpNum][0], grpNum, 0);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn close timeStamp failed\n");
	}

	if(g_buse2bit){
		ret = IMP_OSD_ShowRgn(prHander[grpNum][1], grpNum, 0);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn close Logo failed\n");
		}
	}

	ret = IMP_OSD_ShowRgn(prHander[grpNum][2], grpNum, 0);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn close cover failed\n");
	}

	ret = IMP_OSD_ShowRgn(prHander[grpNum][3], grpNum, 0);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn close Rect failed\n");
	}

	ret = IMP_OSD_ShowRgn(prHander[grpNum][4], grpNum, 0);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_ShowRgn close Rect failed\n");
	}

	ret = IMP_OSD_UnRegisterRgn(prHander[grpNum][0], grpNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_UnRegisterRgn timeStamp failed\n");
	}

	if(g_buse2bit){
		ret = IMP_OSD_UnRegisterRgn(prHander[grpNum][1], grpNum);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_OSD_UnRegisterRgn logo failed\n");
		}
	}


	ret = IMP_OSD_UnRegisterRgn(prHander[grpNum][2], grpNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_UnRegisterRgn Cover failed\n");
	}

	ret = IMP_OSD_UnRegisterRgn(prHander[grpNum][3], grpNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_UnRegisterRgn Rect failed\n");
	}

	ret = IMP_OSD_UnRegisterRgn(prHander[grpNum][4], grpNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_UnRegisterRgn Rect failed\n");
	}

	IMP_OSD_DestroyRgn(prHander[grpNum][0]);
	if(g_buse2bit){
		IMP_OSD_DestroyRgn(prHander[grpNum][1]);
	}
	IMP_OSD_DestroyRgn(prHander[grpNum][2]);
	IMP_OSD_DestroyRgn(prHander[grpNum][3]);
	IMP_OSD_DestroyRgn(prHander[grpNum][4]);

	ret = IMP_OSD_DestroyGroup(grpNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_OSD_DestroyGroup failed\n");
		return -1;
	}

	return 0;
}
