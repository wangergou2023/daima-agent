/*
	Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
*/

#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/time.h>

#include "sample-common.h"

#define TAG "sample-Ai-Ref"

#define REF_AEC_SAMPLE_RATE 16000
#define REF_AEC_SAMPLE_TIME 20
#define REF_AUDIO_BUF_SIZE (REF_AEC_SAMPLE_RATE * sizeof(short) * REF_AEC_SAMPLE_TIME / 1000)
#define REF_AUDIO_RECORD_NUM 500
#define REF_AUDIO_RECORD_FILE_FOR_PLAY "./ref_test_for_play.pcm"
#define REF_AUDIO_RECORD_FILE "./ref_test_record.pcm"
#define REF_AUDIO_REF_FILE "./ref_test_ref.pcm"

static void *IMP_Audio_Record_Thread(void *argv);
static void * IMP_Audio_Record_Ref_Thread(void *argv);
static void *IMP_Audio_Play_Thread(void *argv);

int main(int argc, char *argv[])
{
	int ret;
	pthread_t tid_record, tid_play, tid_ref;

	/*Record an audio segment first as the source for subsequent playback */
	printf("[INFO] Start audio record test.\n");
	printf("[INFO] Can create the %s file.\n", REF_AUDIO_RECORD_FILE_FOR_PLAY);
	printf("[INFO] Please input any key to continue.\n");
	getchar();
	ret = pthread_create(&tid_record, NULL, IMP_Audio_Record_Thread, REF_AUDIO_RECORD_FILE_FOR_PLAY);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "[ERROR] %s: pthread_create Audio Record failed\n", __func__);
		return -1;
	}
	pthread_join(tid_record, NULL);
	printf("[INFO] Record end.\n");

	/*Start ref verification */
	printf("[INFO] Start audio record ref test.\n");
	printf("[INFO] Can create the %s file.\n", REF_AUDIO_RECORD_FILE);
	printf("[INFO]  Start audio play test.\n");
	printf("[INFO]  Play the %s file.\n", REF_AUDIO_RECORD_FILE_FOR_PLAY);
	printf("[INFO] Please input any key to continue.\n");
	getchar();

	ret = pthread_create(&tid_ref, NULL, IMP_Audio_Record_Ref_Thread, REF_AUDIO_RECORD_FILE);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "[ERROR] %s: pthread_create Audio Record failed\n", __func__);
		return -1;
	}

	ret = pthread_create(&tid_play, NULL, IMP_Audio_Play_Thread, REF_AUDIO_RECORD_FILE_FOR_PLAY);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "[ERROR] %s: pthread_create Audio Play failed\n", __func__);
		return -1;
	}

	pthread_join(tid_play, NULL);
	pthread_join(tid_ref, NULL);

	return 0;
}

static void *IMP_Audio_Record_Thread(void *argv)
{
	int ret = -1;
	int record_num = 0;
	if(argv == NULL) {
		IMP_LOG_ERR(TAG, "Please input the record file name.\n");
		return NULL;
	}
	FILE *record_file = fopen(argv, "wb");
	if(record_file == NULL) {
		IMP_LOG_ERR(TAG, "fopen %s failed\n", argv);
		return NULL;
	}

	/* Step.1 设置AI设备的公共属性 */
	/* Step.1 set public attribute of AI device */
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

	/* Step.2 启用AI设备 */
	/* Step.2 enable AI device */
	ret = IMP_AI_Enable(devID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "enable ai %d err\n", devID);
		return NULL;
	}

	/* Step.3 设置AI设备的音频通道属性 */
	/* Step.3 set audio channel attribute of AI device */
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

	/* Step.4 启用AI通道 */
	/* Step.4 enable AI channel */
	ret = IMP_AI_EnableChn(devID, chnID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Record enable channel failed\n");
		return NULL;
	}

	/* Step.5 设置音频通道音量 */
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

	/* Step.6 设置音频通道增益 */
	/* Step.6 Set audio channel gain */
	int aigain = 28;
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

	/* Step.7 启用AI算法 */
	/* Step.7 enable AI algorithm */
	ret = IMP_AI_EnableAlgo(devID, chnID);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_AI_EnableAlgo failed\n");
		return NULL;
	}

	while(1) {
		/* Step.8 获取音频录制帧 */
		/* Step.8 get audio record frame */
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

		/* Step.9 保存录制数据到文件 */
		/* Step.9 Save the recording data to a file */
		fwrite(frm.virAddr, 1, frm.len, record_file);

		/* Step.10 释放音频录制帧 */
		/* Step.10 release the audio record frame */
		ret = IMP_AI_ReleaseFrame(devID, chnID, &frm);
		if(ret != 0) {
			IMP_LOG_ERR(TAG, "Audio release frame data error\n");
			return NULL;
		}

		if(++record_num >= REF_AUDIO_RECORD_NUM)
			break;
	}

	sleep(3);
	/* Step.11 禁用AI算法 */
	/* Step.11 disable AI algorithm */
	ret = IMP_AI_DisableAlgo(devID, chnID);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_AI_DisableAlgo error\n");
		return NULL;
	}

	/* Step.12 禁用音频通道 */
	/* Step.12 disable the audio channel */
	ret = IMP_AI_DisableChn(devID, chnID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio channel disable error\n");
		return NULL;
	}

	/* Step.13 禁用音频设备 */
	/* Step.13 disable the audio devices */
	ret = IMP_AI_Disable(devID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio device disable error\n");
		return NULL;
	}

	fclose(record_file);
	pthread_exit(0);
}

static void * IMP_Audio_Record_Ref_Thread(void *argv)
{
	int ret = -1;
	int record_num = 0;

	if(argv == NULL) {
		IMP_LOG_ERR(TAG, "Please input the record file name.\n");
		return NULL;
	}

	FILE *record_file = fopen(argv, "wb");
	if(record_file == NULL) {
		IMP_LOG_ERR(TAG, "fopen %s failed\n", REF_AUDIO_RECORD_FILE);
		return NULL;
	}
	FILE *ref_file = fopen(REF_AUDIO_REF_FILE, "wb");
	if(ref_file == NULL) {
		IMP_LOG_ERR(TAG, "fopen %s failed\n", REF_AUDIO_REF_FILE);
		return NULL;
	}

	/* Step.1 设置AI设备的公共属性 */
	/* Step.1 set public attribute of AI device */
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

	/*Step.2 启用AI设备 */
	/* Step.2 enable AI device */
	ret = IMP_AI_Enable(devID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "enable ai %d err\n", devID);
		return NULL;
	}

	/* Step.3 设置AI设备的音频通道属性 */
	/* Step.3 set audio channel attribute of AI device */
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

	/* Step.4 启用AI通道 */
	/* Step.4 enable AI channel */
	ret = IMP_AI_EnableChn(devID, chnID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Record enable channel failed\n");
		return NULL;
	}

	/* Step.5 启用参考帧 */
	/* Step.5 enable ref */
	ret = IMP_AI_EnableAecRefFrame(devID, chnID, 0, 0);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Record enable channel failed\n");
		return NULL;
	}

	/* Step.6 设置音频通道音量 */
	/* Step.6 Set audio channel volume */
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

	/* Step.7 设置音频通道增益 */
	/* Step.7 Set audio channel gain */
	int aigain = 20;
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

	while(1) {
		/* Step.8 获取音频录制帧 */
		/* Step.8 get audio record frame */
		ret = IMP_AI_PollingFrame(devID, chnID, 1000);
		if (ret != 0 ) {
			IMP_LOG_ERR(TAG, "Audio Polling Frame Data error\n");
		}
		IMPAudioFrame frm;
		IMPAudioFrame ref;
		ret = IMP_AI_GetFrameAndRef(devID, chnID, &frm, &ref,BLOCK);
		if(ret != 0) {
			IMP_LOG_ERR(TAG, "Audio Get Frame Data error\n");
			return NULL;
		}

		/* Step.9 保存录制数据到文件 */
		/* Step.9 Save the recording data to a file. */
		fwrite(frm.virAddr, 1, frm.len, record_file);
		fwrite(ref.virAddr, 1, ref.len, ref_file);

		/* Step.10 释放音频录制帧 */
		/* Step.10 release the audio record frame */
		ret = IMP_AI_ReleaseFrame(devID, chnID, &frm);
		if(ret != 0) {
			IMP_LOG_ERR(TAG, "Audio release frame data error\n");
			return NULL;
		}

		if(++record_num >= REF_AUDIO_RECORD_NUM)
			break;
	}

	/* Step.11 禁用参考帧 */
	/* Step.11 disable ref */
	ret = IMP_AI_DisableAecRefFrame(devID, chnID, 0, 0);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_AI_DisableAecRefFrame\n");
		return NULL;
	}
	sleep(3);

	/* Step.12 禁用AI通道 */
	/* Step.12 disable AI chn */
	ret = IMP_AI_DisableChn(devID, chnID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio channel disable error\n");
		return NULL;
	}

	/* Step.13 禁用音频设备 */
	/* Step.13 disable the audio devices */
	ret = IMP_AI_Disable(devID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio device disable error\n");
		return NULL;
	}

	fclose(record_file);
	fclose(ref_file);
	pthread_exit(0);
}

static void *IMP_Audio_Play_Thread(void *argv)
{
	unsigned char *buf = NULL;
	int size = 0;
	int ret = -1;

	if(argv == NULL) {
		IMP_LOG_ERR(TAG, "[ERROR] %s: Please input the play file name.\n", __func__);
		return NULL;
	}

	buf = (unsigned char *)malloc(REF_AUDIO_BUF_SIZE);
	if(buf == NULL) {
		IMP_LOG_ERR(TAG, "[ERROR] %s: malloc audio buf error\n", __func__);
		return NULL;
	}

	FILE *play_file = fopen(argv, "rb");
	if(play_file == NULL) {
		IMP_LOG_ERR(TAG, "[ERROR] %s: fopen %s failed\n", __func__, argv);
		return NULL;
	}

	/* Step.1 设置AO设备的公共属性 */
	/* Step.1 set public attribute of AO device */
	int devID = 0;
	IMPAudioIOAttr attr;
	attr.samplerate = AUDIO_SAMPLE_RATE_16000;
	attr.bitwidth = AUDIO_BIT_WIDTH_16;
	attr.soundmode = AUDIO_SOUND_MODE_MONO;
	attr.frmNum = 20;
	attr.numPerFrm = 640;
	attr.chnCnt = 1;
	ret = IMP_AO_SetPubAttr(devID, &attr);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "set ao %d attr err: %d\n", devID, ret);
		return NULL;
	}
	memset(&attr, 0x0, sizeof(attr));
	ret = IMP_AO_GetPubAttr(devID, &attr);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "get ao %d attr err: %d\n", devID, ret);
		return NULL;
	}
	IMP_LOG_INFO(TAG, "Audio Out GetPubAttr samplerate:%d\n", attr.samplerate);
	IMP_LOG_INFO(TAG, "Audio Out GetPubAttr   bitwidth:%d\n", attr.bitwidth);
	IMP_LOG_INFO(TAG, "Audio Out GetPubAttr  soundmode:%d\n", attr.soundmode);
	IMP_LOG_INFO(TAG, "Audio Out GetPubAttr     frmNum:%d\n", attr.frmNum);
	IMP_LOG_INFO(TAG, "Audio Out GetPubAttr  numPerFrm:%d\n", attr.numPerFrm);
	IMP_LOG_INFO(TAG, "Audio Out GetPubAttr     chnCnt:%d\n", attr.chnCnt);

	/* Step.2 启用AO设备 */
	/* Step.2 enable AO device */
	ret = IMP_AO_Enable(devID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "enable ao %d err\n", devID);
		return NULL;
	}

	/* Step.3 启用AO通道 */
	/* Step.3 enable AO channel */
	int chnID = 0;
	ret = IMP_AO_EnableChn(devID, chnID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio play enable channel failed\n");
		return NULL;
	}

	/* Step.4 设置音频通道音量 */
	/* Step.4 Set audio channel volume */
	int chnVol = 60;
	ret = IMP_AO_SetVol(devID, chnID, chnVol);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Play set volume failed\n");
		return NULL;
	}
	ret = IMP_AO_GetVol(devID, chnID, &chnVol);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Play get volume failed\n");
		return NULL;
	}
	IMP_LOG_INFO(TAG, "Audio Out GetVol    vol:%d\n", chnVol);

	/* Step.5 设置音频通道增益 */
	/* Step.5 Set audio channel gain */
	int aogain = 28;
	ret = IMP_AO_SetGain(devID, chnID, aogain);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Play Set Gain failed\n");
		return NULL;
	}
	ret = IMP_AO_GetGain(devID, chnID, &aogain);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio Play Get Gain failed\n");
		return NULL;
	}
	IMP_LOG_INFO(TAG, "Audio Out GetGain    gain : %d\n", aogain);

	/* Step.6 启用AO算法 */
	/* Step.6 enable AO algorithm */
	ret = IMP_AO_EnableAlgo(devID, chnID);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_AO_EnableAlgo failed\n");
		return NULL;
	}

	while(1) {
		size = fread(buf, 1, REF_AUDIO_BUF_SIZE, play_file);
		if(size < REF_AUDIO_BUF_SIZE)
			break;

		/* Step.7 发送帧数据 */
		/* Step.7 send frame data */
		IMPAudioFrame frm;
		frm.virAddr = (uint32_t *)buf;
		frm.len = size;
		ret = IMP_AO_SendFrame(devID, chnID, &frm, BLOCK);
		if(ret != 0) {
			IMP_LOG_ERR(TAG, "send Frame Data error\n");
			return NULL;
		}

		/* Step.8 查看播放帧状态 */
		/* Step.8 view playback frame status */
		IMPAudioOChnState play_status;
		ret = IMP_AO_QueryChnStat(devID, chnID, &play_status);
		if(ret != 0) {
			IMP_LOG_ERR(TAG, "IMP_AO_QueryChnStat error\n");
			return NULL;
		}

		IMP_LOG_INFO(TAG, "Play: TotalNum %d, FreeNum %d, BusyNum %d\n",
				play_status.chnTotalNum, play_status.chnFreeNum, play_status.chnBusyNum);
	}

	/* Step.9 等待最后音频数据播放完成 */
	/* Step.9 waiting for the last audio data to finish playing */
	ret = IMP_AO_FlushChnBuf(devID,chnID);
	if(ret != 0){
		IMP_LOG_ERR(TAG, "IMP_AO_FlushChnBuf error\n");
		return NULL;
	}
	/* Step.10 禁用AO算法 */
	/* Step.10 disable AO algorithm */
	ret = IMP_AO_DisableAlgo(devID, chnID);
	if (ret != 0) {
		IMP_LOG_ERR(TAG, "IMP_AO_DisableAlgo error\n");
		return NULL;
	}
	/* Step.11 禁用音频通道 */
	/* Step.11 disable the audio channel */
	ret = IMP_AO_DisableChn(devID, chnID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio channel disable error\n");
		return NULL;
	}

	/* Step.12 禁用音频设备 */
	/* Step.12 disable the audio devices */
	ret = IMP_AO_Disable(devID);
	if(ret != 0) {
		IMP_LOG_ERR(TAG, "Audio device disable error\n");
		return NULL;
	}

	fclose(play_file);
	free(buf);
	pthread_exit(0);
}

