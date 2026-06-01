/*
 *	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <sysutils/su_base.h>
#include "sample-common.h"

#define TAG "sample-Ai"

#define AI_BASIC_TEST_RECORD_FILE "ai_record.pcm"
#define AI_BASIC_TEST_RECORD_NUM 500
int sleep_time = 1; //Set AOV cycle time,default is 1s. unit: seconds.
int verbose = 1; //Enable verbose mode
static void *_ai_basic_record_test_thread(void *argv);
static int enter_suspend(void);
const char* bind_path = "/sys/devices/platform/jz-dsp/audio_bind";
const char* unbind_path = "/sys/devices/platform/jz-dsp/audio_unbind";
static pthread_mutex_t suspend_mutex = PTHREAD_MUTEX_INITIALIZER;
int main(void)
{
	int ret = -1;
    int cnt = 0;
	int state_fd = -1;
	pthread_t record_thread_id;
	printf("[INFO] Test : Start audio record test.\n");
	printf("[INFO]        : Can create the %s file.\n", AI_BASIC_TEST_RECORD_FILE);
	printf("[INFO]        : Please input any key to continue.\n");
	getchar();
	while(1){
		/* Step.1 Start audio recording thread */
		ret = pthread_create(&record_thread_id, NULL, _ai_basic_record_test_thread, NULL);
		if(ret != 0) {
			IMP_LOG_ERR(TAG, "[ERROR] %s: pthread_create Audio Record failed\n", __func__);
			return -1;
		}
		pthread_join(record_thread_id, NULL);
		//bind
		//system("echo suspend > /sys/devices/platform/jz-dsp/audio_bind");
		state_fd = open(bind_path, O_WRONLY);
		if (state_fd < 0) {
			IMP_LOG_ERR(TAG, "when open %s\n", bind_path);
			return -1;
		}
        ret = write(state_fd, "suspend", 7);
        if (ret != 7) {
            IMP_LOG_ERR(TAG, "Error when bind\n");
            return -1;
        }
        close(state_fd);
		//睡眠
		enter_suspend();
		//system("echo resume > /sys/devices/platform/jz-dsp/audio_unbind");
		//unbind
		state_fd = open(unbind_path, O_WRONLY);
		if (state_fd < 0) {
			IMP_LOG_ERR(TAG, "when open %s\n", unbind_path);
			return -1;
		}
        ret = write(state_fd, "resume", 6);
        if (ret != 6) {
            IMP_LOG_ERR(TAG, "Error when bind\n");
            return  -1;
        }
        close(state_fd);
        printf("cnt = %d\n",cnt++);
	}
	return 0;
}

static int enter_suspend(void)
{
	int ret;
	int set_alarm_try_cnt = 0;
	SUTime now_tm;
	pthread_mutex_lock(&suspend_mutex);

set_alarm_try:
	ret = SU_Base_GetTime(&now_tm);
	if (ret < 0) {
		printf("SU_Base_GetTime() error\n");
		ret = -1;
		goto err;
	}
	if(verbose){
		printf("Now time: %d.%d.%d %02d:%02d:%02d\n",
		       now_tm.year, now_tm.mon, now_tm.mday,
		       now_tm.hour , now_tm.min, now_tm.sec);
	}
	SUTime alarm_tm = now_tm;
	alarm_tm.sec += sleep_time;
	if (alarm_tm.sec >= 60) {
		alarm_tm.sec %= 60;
		alarm_tm.min++;
	}
	if (alarm_tm.min >= 60) {
		alarm_tm.min %= 60;
		alarm_tm.hour++;
	}
	if (alarm_tm.hour == 24)
		alarm_tm.hour = 0;

	ret = SU_Base_SetAlarm(&alarm_tm);
	if (ret < 0) {
		printf("SU_Base_SetAlarm() error\n");
		ret = -1;
		goto err;
	}

	SUTime alarm_tm_check;
	ret = SU_Base_GetAlarm(&alarm_tm_check);
	if (ret < 0) {
		printf("SU_Base_GetAlarm() error\n");
		ret = -1;
		goto err;
	}
	if(verbose){
		printf("Set alarm Time: %d.%d.%d %02d:%02d:%02d\n",
		       alarm_tm_check.year, alarm_tm_check.mon, alarm_tm_check.mday,
		       alarm_tm_check.hour , alarm_tm_check.min, alarm_tm_check.sec);
	}
	//Configure the alarm wake-up method.
	ret = SU_Base_SetWkupMode(WKUP_ALARM);
	if(ret < 0){
		printf("SU_Base_SetWkupMode() error\n");
		ret = -1;
		goto err;
	}
	ret = SU_Base_EnableAlarm();
	if (ret < 0) {
		printf("SU_Base_EnableAlarm() error\n");
		if(set_alarm_try_cnt){
			goto err;
		}else{
			set_alarm_try_cnt++;
			goto set_alarm_try;
		}
	}
	//enter sleep
	ret = SU_Base_Suspend();
	if (ret < 0) {
		printf("SU_Base_Suspend error\n");
		ret = -1;
		goto err;
	}

	//Sleep completed, continue running the code context.
	ret = SU_Base_GetTime(&now_tm);
	if (ret < 0) {
		printf("SU_Base_GetTime() error\n");
		goto err;
	}
	if(verbose){
		printf("Wake-up time: %d.%d.%d %02d:%02d:%02d\n",
		       now_tm.year, now_tm.mon, now_tm.mday,
		       now_tm.hour , now_tm.min, now_tm.sec);
	}

	ret = SU_Base_DisableAlarm();
	if (ret < 0) {
		printf("SU_Base_DisableAlarm() error\n");
	}
err:
	pthread_mutex_unlock(&suspend_mutex);
	return ret;
}
static void *_ai_basic_record_test_thread(void *argv)
{
	int ret = -1;
	int record_num = 0;

	FILE *record_file = fopen(AI_BASIC_TEST_RECORD_FILE, "wb");
	if(record_file == NULL) {
		IMP_LOG_ERR(TAG, "fopen %s failed\n", AI_BASIC_TEST_RECORD_FILE);
		return NULL;
	}

	/* Step.2 set public attribute of AI device */
	int devID = 1;
	IMPAudioIOAttr attr;
	attr.samplerate = AUDIO_SAMPLE_RATE_16000;
	attr.bitwidth = AUDIO_BIT_WIDTH_16;
	attr.soundmode = AUDIO_SOUND_MODE_MONO;
	attr.frmNum = 40;
	attr.numPerFrm = 640;
	attr.chnCnt = 1;
	ret = IMP_AI_SetPubAttr(devID, &attr);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "set ai %d attr err: %d\n", devID, ret);
		return NULL;
	}
	memset(&attr, 0x0, sizeof(attr));
	ret = IMP_AI_GetPubAttr(devID, &attr);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "get ai %d attr err: %d\n", devID, ret);
		return NULL;
	}
	IMP_LOG_INFO(TAG, "Audio In GetPubAttr samplerate : %d\n", attr.samplerate);
	IMP_LOG_INFO(TAG, "Audio In GetPubAttr   bitwidth : %d\n", attr.bitwidth);
	IMP_LOG_INFO(TAG, "Audio In GetPubAttr  soundmode : %d\n", attr.soundmode);
	IMP_LOG_INFO(TAG, "Audio In GetPubAttr     frmNum : %d\n", attr.frmNum);
	IMP_LOG_INFO(TAG, "Audio In GetPubAttr  numPerFrm : %d\n", attr.numPerFrm);
	IMP_LOG_INFO(TAG, "Audio In GetPubAttr     chnCnt : %d\n", attr.chnCnt);

	/* Step.3 enable AI device */
	ret = IMP_AI_Enable(devID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "enable ai %d err\n", devID);
		return NULL;
	}

	/* Step.4  set audio channel attribute of AI device */
	int chnID = 0;
	IMPAudioIChnParam chnParam;
	chnParam.usrFrmDepth = 40;
	chnParam.aecChn = 0;
	ret = IMP_AI_SetChnParam(devID, chnID, &chnParam);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "set ai %d channel %d attr err: %d\n", devID, chnID, ret);
		return NULL;
	}
	memset(&chnParam, 0x0, sizeof(chnParam));
	ret = IMP_AI_GetChnParam(devID, chnID, &chnParam);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "get ai %d channel %d attr err: %d\n", devID, chnID, ret);
		return NULL;
	}
	IMP_LOG_INFO(TAG, "Audio In GetChnParam usrFrmDepth : %d\n", chnParam.usrFrmDepth);

	/* Step 4: enable AI channel. */
	ret = IMP_AI_EnableChn(devID, chnID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Record enable channel failed\n");
		return NULL;
	}

	/* Step.5 Set audio channel volume */
	int chnVol = 60;
	ret = IMP_AI_SetVol(devID, chnID, chnVol);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Record set volume failed\n");
		return NULL;
	}
	ret = IMP_AI_GetVol(devID, chnID, &chnVol);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Record get volume failed\n");
		return NULL;
	}
	IMP_LOG_INFO(TAG, "Audio In GetVol    vol : %d\n", chnVol);

	/* Step.6 Set audio channel gain */
	int aigain = 23;
	ret = IMP_AI_SetGain(devID, chnID, aigain);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Record Set Gain failed\n");
		return NULL;
	}
	ret = IMP_AI_GetGain(devID, chnID, &aigain);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Record Get Gain failed\n");
		return NULL;
	}
	IMP_LOG_INFO(TAG, "Audio In GetGain    gain : %d\n", aigain);
#if 0
	/* Step.7  enable AI algorithm */
	ret = IMP_AI_EnableAlgo(devID, chnID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_AI_EnableAlgo failed\n");
		return NULL;
	}
#endif
	while(1) {
		/* Step.8  get audio record frame */
		ret = IMP_AI_PollingFrame(devID, chnID, 1000);
		if (ret != 0 ) {
			IMP_LOG_ERR(TAG, "Audio Polling Frame Data error\n");
		}
		IMPAudioFrame frm;
		ret = IMP_AI_GetFrame(devID, chnID, &frm, BLOCK);
		if(ret != 0) {
			IMP_LOG_ERR(TAG, "Audio Get Frame Data error\n");
			return NULL;
		}

		/* Step.9 Save the recording data to a file */
		fwrite(frm.virAddr, 1, frm.len, record_file);

		/* Step.10  release the audio record frame */
		ret = IMP_AI_ReleaseFrame(devID, chnID, &frm);
		if(ret != 0) {
			IMP_LOG_ERR(TAG, "Audio release frame data error\n");
			return NULL;
		}

		if(++record_num >= AI_BASIC_TEST_RECORD_NUM)
			break;
	}

	sleep(3);
#if 0
	/* Step.11 disable AI algorithm */
	ret = IMP_AI_DisableAlgo(devID, chnID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_AI_DisableAlgo failed\n");
		return NULL;
	}
#endif
	/* Step.12 disable the audio channel */
	ret = IMP_AI_DisableChn(devID, chnID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio channel disable error\n");
		return NULL;
	}

	/* Step.13 disable the audio devices */
	ret = IMP_AI_Disable(devID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio device disable error\n");
		return NULL;
	}

	fclose(record_file);
	pthread_exit(0);
}

