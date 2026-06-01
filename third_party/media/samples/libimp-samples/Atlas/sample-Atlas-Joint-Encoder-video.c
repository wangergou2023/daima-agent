/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 **/

#include "sample-common.h"
#include "sample-common-framesource.h"
#include "sample-common-encoder.h"
#include <sysutils/su_base.h>
#include <sys/stat.h>
#include <getopt.h>
#define TAG "Atlas-sample"

extern struct chn_conf chn[];
#define FRM_BIT_RATE_TIME 2
#define STREAM_TYPE_NUM 12
static int frmrate_sp[STREAM_TYPE_NUM] = { 0 };
static int statime_sp[STREAM_TYPE_NUM] = { 0 };
static int bitrate_sp[STREAM_TYPE_NUM] = { 0 };
static pthread_mutex_t suspend_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t switch_mutex = PTHREAD_MUTEX_INITIALIZER;
struct init_arguments{
	int sensor_num;
	int enc_chn;
	bool ivdc;
	int sleep_time;
	int sleep_time_ms;
	int enc_count;
	int test;
	int verbose;
	bool close_stream;
}init_args;


void print_usage() {
	printf("Usage: example [options]\n");
	printf("Options:\n");
	printf("  --sensor_num <num>	Select the number of sensors, default is 1.\n");
	printf("  --enc_chn <num>	Select the number of encoding channels, default open main encoder.\n");
	printf("  --ivdc		Enable IVDC function, default close\n");
	printf("  -s, --seconds <num>	Set AOV cycle time,default is 1s. unit: seconds.\n");
	printf("  --msec <num>		Set AOV cycle time ms. unit: msec.\n");
	printf("  -c, --counts <num>	Set AOV operation count.default 1000\n");
	printf("  -t, --test		Add test mode, with automatic switching between continuous flow and AOV mode. default close\n");
	printf("  -h, --help            Show this help message\n");
	printf("  -v, --verbose         Enable verbose mode\n");
	printf("  --close_stream        Close save stream\n");
	printf("E.g: ./sample-Atlas-Encoder-video --seconds=1 --counts=1000 --enc_chn=3 \n");

}
int parse_arguments(int argc, char *argv[])
{
	int opt;
	if(argc == 1){
		print_usage();
		return -1;
	}
	struct option long_options[] = {
		{"sensor_num", required_argument, NULL, 0},
		{"enc_chn", required_argument, NULL, 0},
		{"ivdc", no_argument, NULL, 'i'},
		{"msec", required_argument, NULL, 0},
		{"seconds", required_argument, NULL, 's'},
		{"counts", required_argument, NULL, 'c'},
		{"test", required_argument, NULL, 't'},
		{"help", no_argument, NULL, 'h'},
		{"verbose", no_argument, NULL, 'v'},
		{"close_stream", no_argument, NULL, 0},
		{0, 0, 0, 0}
	};
	int option_index = 0;
	while ((opt = getopt_long_only(argc, argv, "hvis:c:t:", long_options, &option_index)) != -1) {
		switch (opt) {
		case 'h':
			print_usage();
			return -1;
		case 'v':
			init_args.verbose = 1;
			break;
		case 's':
			init_args.sleep_time = atoi(optarg);
			break;
		case 'c':
			init_args.enc_count = atoi(optarg);
			break;
		case 'i':
			init_args.ivdc = true;
			break;
		case 't':
			init_args.test = atoi(optarg);
			break;
		case 0:
			if (strcmp(long_options[option_index].name, "sensor_num") == 0) {
				init_args.sensor_num = atoi(optarg);
			}else if (strcmp(long_options[option_index].name, "enc_chn") == 0) {
				init_args.enc_chn = atoi(optarg);
			}else if (strcmp(long_options[option_index].name, "close_stream") == 0) {
				init_args.close_stream = true;
			} else if (strcmp(long_options[option_index].name, "msec") == 0) {
				init_args.sleep_time_ms = atoi(optarg);
			}
			break;
		default:
			print_usage();
			return -1;
		}
	}
	return 0;
}

static int save_stream(int fd, IMPEncoderStream *stream)
{
	int i = 0;
	int ret = 0;
	int nr_pack = stream->packCount;
	if(init_args.close_stream) {
		return 0;
	}
	for (i = 0; i < nr_pack; i++) {
		ret = write(fd, (void *)stream->pack[i].virAddr, stream->pack[i].length);
		if (ret != stream->pack[i].length) {
			IMP_LOG_ERR(TAG, "stream write failed\n");
			return -1;
		}
	}
	return 0;
}


static int enter_suspend(void)
{
	int ret;
	int set_alarm_try_cnt = 0;
	SUTime now_tm;
	int time_ms = 0;
	int alarm_ms = 0;
	int alarm_ss = 0;
	pthread_mutex_lock(&suspend_mutex);
	alarm_ms = init_args.sleep_time_ms;
	alarm_ss = init_args.sleep_time;

set_alarm_try:
	ret = SU_Base_GetTime(&now_tm);
	if (ret < 0) {
		printf("SU_Base_GetTime() error\n");
		ret = -1;
		goto err;
	}
	if(alarm_ms){
		ret = SU_Base_GetTimeMs(&time_ms);
		if (ret < 0) {
			printf("SU_Base_GetTimeMs() error\n");
			ret = -1;
			goto err;
		}
	}

	if(init_args.verbose){
		printf("Now time: %d.%d.%d %02d:%02d:%02d.%d\n",
		       now_tm.year, now_tm.mon, now_tm.mday,
		       now_tm.hour , now_tm.min, now_tm.sec, time_ms);
	}

	if(0 != alarm_ms){
		if((alarm_ms + time_ms) >= 1000){
			alarm_ss++;
			alarm_ms = (init_args.sleep_time_ms + time_ms) % 1000;
		}else{
			alarm_ms = init_args.sleep_time_ms;
		}
	}

	SUTime alarm_tm = now_tm;
	alarm_tm.sec += alarm_ss;

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

	if(alarm_ms){
		ret = SU_Base_SetAlarmTimeMs(alarm_ms);
		if (ret < 0) {
			printf("SU_Base_SetAlarmTimeMs() error\n");
			ret = -1;
			goto err;
		}
	}

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
	int alarm_tms_check;
	if(alarm_ms){
		ret = SU_Base_GetAlarmTimeMs(&alarm_tms_check);
		if (ret < 0) {
			printf("SU_Base_GetAlarm() error\n");
			ret = -1;
			goto err;
		}

	}

	if(init_args.verbose){
		printf("Set alarm Time: %d.%d.%d %02d:%02d:%02d.%d\n",
		       alarm_tm_check.year, alarm_tm_check.mon, alarm_tm_check.mday,
		       alarm_tm_check.hour , alarm_tm_check.min, alarm_tm_check.sec,alarm_tms_check);
	}
	//Configure the alarm wake-up method.
	ret = SU_Base_SetWkupMode(WKUP_ALARM);
	if(ret < 0){
		printf("SU_Base_SetWkupMode() error\n");
		ret = -1;
		goto err;
	}

	if(alarm_ms){
		ret = SU_Base_EnableAlarmTimeMs();
		if (ret < 0) {
			printf("SU_Base_EnableAlarmTimeMs() error\n");
			ret = -1;
			goto err;
		}
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
	if(alarm_ms){
		ret = SU_Base_GetTimeMs(&time_ms);
		if (ret < 0) {
			printf("SU_Base_GetTime() error\n");
			goto err;
		}
	}

	if(init_args.verbose){
		printf("Wake-up time: %d.%d.%d %02d:%02d:%02d.%d\n",
		       now_tm.year, now_tm.mon, now_tm.mday,
		       now_tm.hour , now_tm.min, now_tm.sec, time_ms);
	}

	if(alarm_ms){
		ret = SU_Base_DisableAlarmTimeMs();
		if (ret < 0) {
			printf("SU_Base_DisableAlarmTimeMs() error\n");
		}
	}
	ret = SU_Base_DisableAlarm();
	if (ret < 0) {
		printf("SU_Base_DisableAlarm() error\n");
	}
err:
	pthread_mutex_unlock(&suspend_mutex);
	return ret;
}


static void *zboost_video_thread(void *args)
{
	int val, i, chnNum, ret;
	IMPPayloadType payloadType;
	val = (int)args;
	payloadType = (val >> 16) & 0xffff;
	chnNum = val & 0xffff;

	char stream_path[64];
	int stream_fd = -1;

	struct stat buffer;
	sprintf(stream_path, "%s/stream-%d.%s", STREAM_FILE_PATH_PREFIX, chnNum,
		(payloadType == PT_H264) ? "h264" : "h265");

	IMP_LOG_DBG(TAG, "Video ChnNum=%d Open Stream file %s ", chnNum, stream_path);
	if (stat(stream_path, &buffer) == 0) {
		stream_fd = open(stream_path, O_RDWR | O_APPEND , 0777);
		if (stream_fd < 0) {
			IMP_LOG_ERR(TAG, "open %s failed\n", stream_path);
			return NULL;
		}
	}else{
		stream_fd = open(stream_path, O_RDWR | O_CREAT | O_TRUNC, 0777);
		if (stream_fd < 0) {
			IMP_LOG_ERR(TAG, "open %s failed\n", stream_path);
			return NULL;
		}
	}

	/*
	 * 添加监控
	 * Add listen
	 * */
	int tpid = SU_PM_AddThreadListen();
	if (tpid < 0) {
		IMP_LOG_ERR(TAG, "SU_PM_AddThreadListen failed\n", chnNum);
		return ((void *)-1);
	}


	ret = IMP_Encoder_StartRecvPic(chnNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_StartRecvPic(%d) failed\n", chnNum);
		return ((void *)-1);
	}


	for (i = 0; i < init_args.enc_count; i++) {
		ret = IMP_Encoder_PollingStream(chnNum, 1000);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_PollingStream(%d) timeout\n", chnNum);
			printf("IMP_Encoder_PollingStream(%d) timeout\n", chnNum);
			continue;
		}

		if(init_args.verbose){
			printf("[chn%d]enc count %d\n",chnNum,i);
		}


		IMPEncoderStream stream;
		/* Get H264 or H265 Stream */
		ret = IMP_Encoder_GetStream(chnNum, &stream, 1);
		if (ret < 0) {
			IMP_LOG_ERR(TAG, "IMP_Encoder_GetStream(%d) failed\n", chnNum);
			break;
		}
		ret = save_stream(stream_fd, &stream);
		if (ret < 0) {
			close(stream_fd);
			break;
		}
		if(init_args.verbose) {
			int i, len = 0;
			for (i = 0; i < stream.packCount; i++) {
				len += stream.pack[i].length;
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
		}
		IMP_Encoder_ReleaseStream(chnNum, &stream);

		/*
		 * 若启用休眠接口，调用 SU_PM_ThreadSuspend 后线程将被挂起.
		 * If the sleep interface is enabled, the thread will be suspended after calling SU_PM_TheadSuspend.
		 * */
		ret = SU_PM_TheadSuspend();
		if(ret == -2){
			IMP_LOG_ERR(TAG, "SU_PM_TheadSuspend failed\n", chnNum);
			break;
		}
	}
	printf("stream off\n");

	/*
	 * 移除线程监听
	 * Remove the thread listener.
	 * */
	ret = SU_PM_DelThreadListen(tpid);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "SU_PM_DelThreadListen failed\n", chnNum);
		return ((void *)-1);
	}
	ret = IMP_Encoder_StopRecvPic(chnNum);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_Encoder_StopRecvPic(%d) failed\n", chnNum);
		return ((void *)-1);
	}
	return ((void *)0);
}


pthread_t tid_switch_test;
static void *test_switch(void *args)
{
	IMPISPPMAttr mode = 0;
	int i = 0;
	int ret= 0;
	int now_state = 0;
	int normal_mode_cnt = 0;
	while(1){
		for(i = IMPVI_MAIN;i < SENSOR_NUM;i++){
			ret = IMP_ISP_PM_GetMode(i, &mode);
			if(ret != 0){
				IMP_LOG_ERR(TAG, "IMP_ISP_PM_SetMode set normal mode error\n");
			}
		}
		if(init_args.test == 1){
			if(mode == TISP_NORMAL_MODE){
				sleep(1);
				normal_mode_cnt++;
				if(normal_mode_cnt > 10){
					now_state = 1;
				}else{
					continue;
				}
			}else{
				normal_mode_cnt = 0;
				now_state = 0;
			}
			if(now_state){
				printf("switch AOV mode\n");
				ret = SU_PM_EventSend(PM_ENABLE_PAUSE ,PM_EVENT_SYNC);
				if(ret != 0){
					IMP_LOG_ERR(TAG, "pm event send failed\n");
				}
				mode = TISP_AOV_MODE;
				for(i = IMPVI_MAIN;i < SENSOR_NUM;i++){
					ret = IMP_ISP_PM_SetMode(i, &mode);
					if(ret != 0){
						IMP_LOG_ERR(TAG, "IMP_ISP_PM_SetMode set normal mode error\n");
					}
				}
			}else{
				usleep(100*1000);
			}
		}else if(init_args.test == 2){
			if(mode == TISP_NORMAL_MODE){
				sleep(5);
				printf("Switch AOV mode\n");
				pthread_mutex_lock(&switch_mutex);
				ret = SU_PM_EventSend(PM_ENABLE_PAUSE ,PM_EVENT_SYNC);
				if(ret != 0){
					IMP_LOG_ERR(TAG, "pm event send failed\n");
				}
				pthread_mutex_unlock(&switch_mutex);
			}else if(mode == TISP_AOV_MODE){
				sleep(5);
				pthread_mutex_lock(&switch_mutex);
				printf("Switch normal mode\n");
				ret = SU_PM_EventSend(PM_DISABLE_PAUSE ,PM_EVENT_SYNC);
				if(ret != 0){
					IMP_LOG_ERR(TAG, "pm event send failed\n");
				}
				mode = TISP_NORMAL_MODE;
				for(i = IMPVI_MAIN;i < SENSOR_NUM;i++){
					ret = IMP_ISP_PM_SetMode(i, &mode);
					if(ret != 0){
						IMP_LOG_ERR(TAG, "IMP_ISP_PM_SetMode set normal mode error\n");
					}
				}
				pthread_mutex_unlock(&switch_mutex);
			}
		}
	}
	pthread_exit(NULL);
}

static int test_switch_mode()
{
	int ret;

	ret = pthread_create(&tid_switch_test, NULL, test_switch, NULL);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Create test_thread failed\n");
	}

	return 0;
}



static int sample_zboost_stream()
{
	unsigned int i;
	int ret;
	pthread_t tid[FS_CHN_NUM];

	for (i = 0; i < FS_CHN_NUM; i++) {
		if ((!chn[i].joint.enable && chn[i].enable) || chn[i].joint.output) {
			int arg = ((chn[i].payloadType << 16) | chn[i].index);
			ret = pthread_create(&tid[chn[i].index], NULL, zboost_video_thread, (void *)arg);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Create ChnNum%d zboost_video_thread failed\n", chn[i].index);
			}
		}
	}
	for (i = 0; i < FS_CHN_NUM; i++) {
		if ((!chn[i].joint.enable && chn[i].enable) || chn[i].joint.output) {
			pthread_join(tid[i],NULL);
		}
	}

	return 0;
}


pthread_t suspend_tid;
static void* sample_suspend(void *arg)
{
	int ret = 0;
	int i = 0;
	IMPEncoderCHNStat stat;
	IMPEncoderStream stream;
	int leftStreamFrames = 0;
	IMPISPPMAttr mode = 0;

	while(1){
		/*
		 * 等待所有监听线程进入休眠状态，-1 表示无限期等待
		 * Wait for all listener threads to sleep, -1 wait indefinitely.
		 * */
		ret = SU_PM_WaitThreadSuspend(-1);
		if(ret != 0){
			IMP_LOG_ERR(TAG, "wait thread suspend faied\n");
			pthread_exit(NULL);
		}
		pthread_mutex_lock(&switch_mutex);
		/*
		 * 首先，关闭Framesource
		 * First, turn off the frame source
		 * */
		for (i = 0; i < FS_CHN_NUM; i++) {
			if (chn[i].enable && !chn[i].joint.output) {
				ret = IMP_FrameSource_PM_Suspend(i);
				if(ret != 0){
					IMP_LOG_ERR(TAG, "Framesource suspend return error\n");
					goto pm_exit;
				}
			}
		}
		for (i = 0; i < FS_CHN_NUM; i++) {
			if (chn[i].enable && chn[i].joint.output) {
				ret = IMP_FrameSource_PM_Suspend(i);
				if(ret != 0){
					IMP_LOG_ERR(TAG, "Framesource suspend return error\n");
					goto pm_exit;
				}
			}
		}

		/*
		 * 每个sensor设置AOV模式, 每个sensor状态必须相同
		 * Set each sensor to the same AOV mode. The status must be the same.
		 * */
		for(i = IMPVI_MAIN;i < SENSOR_NUM;i++) {
			ret = IMP_ISP_PM_GetMode(i, &mode);
			if(ret != 0){
				IMP_LOG_ERR(TAG, "IMP_ISP_PM_GetMode set mode failed\n");
			}
			if(mode != TISP_AOV_MODE){
				mode = TISP_AOV_MODE;
				IMP_ISP_PM_SetMode(i, &mode);
				if(ret != 0){
					IMP_LOG_ERR(TAG, "IMP_ISP_PM_SetMode set aov mode error\n");
				}
			}
		}

		/*
		 * 等待ISP 帧结束
		 * Wait ISP frame done
		 * */
		IMPISPOpsMode ops = IMPISP_OPS_MODE_ENABLE;
		ret = IMP_ISP_PM_WaitAllDone(&ops);
		if(ret != 0){
			IMP_LOG_ERR(TAG, "IMP_ISP_PM_WaitAllDone return error\n");
		}

		/*
		 * 刷新缓存并在休眠前检查 wakelock 的释放
		 * Refresh the cache before sleep and check the release of wakelock.
		 * */
		sync();
		fflush(stdout);
		ret = SU_Base_GetWakeupCount();
		if(ret != 0){
			IMP_LOG_ERR(TAG, "Get wakeup count failed\n");
		}

		/*
		 * 进入睡眠状态
		 * Enter suspend status
		 * */
		ret = enter_suspend();
		if(ret != 0){
			IMP_LOG_ERR(TAG, "enter_suspend faied\n");
		}
		/*
		 * 获取当前唤醒方式（定时唤醒或按键唤醒）
		 * Get the current wake-up method, either timed or key wake-up.
		 * */
		ret = SU_Base_GetWkupMode();
		if(ret == WKUP_KEY){
			mode = TISP_NORMAL_MODE;
			for(i = IMPVI_MAIN;i < SENSOR_NUM;i++){
				ret = IMP_ISP_PM_SetMode(i, &mode);
				if(ret != 0){
					IMP_LOG_ERR(TAG, "IMP_ISP_PM_SetMode set normal mode error\n");
				}
			}
			/*
			 * 当触发外部事件时，应停用监控线程的睡眠模式，并使系统进入长录像模式.
			 * When an external event is triggered during simulation,
			 * the sleep mode of the monitoring thread should be deactivated, and the system should enter long recording mode.
			 * */
			ret = SU_PM_EventSend(PM_DISABLE_PAUSE ,PM_EVENT_SYNC);
			if(ret != 0){
				IMP_LOG_ERR(TAG, "pm event send failed\n");
			}
		}

		/*
		 * FrameSource 恢复
		 * FrameSource is resume
		 * */
		for (i = 0; i < FS_CHN_NUM; i++) {
			if ((!chn[i].joint.enable && chn[i].enable) || chn[i].joint.output) {
				int encChn = i;
				if ((ret = IMP_Encoder_Query(encChn, &stat)) < 0) {
					IMP_LOG_ERR(TAG, "IMP_Encoder_Query(%d) failed\n", encChn);
				}
				leftStreamFrames = stat.leftStreamFrames;
				while (leftStreamFrames-- > 0) {
					if (0 == IMP_Encoder_PollingStream(encChn, 1000)) {
						if (IMP_Encoder_GetStream(encChn, &stream, 1) >= 0) {
							IMP_Encoder_ReleaseStream(encChn, &stream);
						}
					}else{
						IMP_LOG_ERR(TAG,"IMP_Encoder_PollingStream chn%d failed\n",encChn);
					}
				}
				if(stat.leftStreamFrames > 0){
					if ((ret = IMP_Encoder_RequestIDR(encChn)) < 0) {
						IMP_LOG_ERR(TAG, "IMP_Encoder_RequestIDR(%d) failed\n", encChn);
					}
					IMP_LOG_WARN(TAG, "%s flush chnNum:%d encoder video frame%d, need request IDR!\n", __func__, i, stat.leftStreamFrames);
				}
			}
			if (chn[i].enable) {
				ret = IMP_FrameSource_PM_Resume(i);
				if(ret != 0){
					IMP_LOG_ERR(TAG, "Framesource resume return error\n");
				}
			}
		}
		/*
		 * 唤醒处于休眠状态的监控线程.
		 * 注意：未被监控的线程应早于此线程被唤醒.
		 * Wake up the sleeping monitoring thread.
		 * Note: Threads that are not being monitored should be awakened earlier than this.
		 * */
		ret = SU_PM_ThreadResume();
		if(ret != 0){
			IMP_LOG_ERR(TAG, "thread resume failed\n");
		}
		pthread_mutex_unlock(&switch_mutex);
	}
pm_exit:
	pthread_mutex_unlock(&switch_mutex);
	pthread_exit(NULL);
}

static int suspend_thread_create()
{
	int ret;
	ret = pthread_create(&suspend_tid, NULL, sample_suspend, NULL);
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "suspend_thread_create failed\n");
		return -1;
	}
	return 0;
}

extern int direct_switch;

int main(int argc, char *argv[])
{
	int i, ret, listen_num = 0;
	IMPFSJointAttr attr[2];
	memset(attr, 0xff, sizeof(attr));
	memset(&init_args, 0, sizeof(init_args));

	ret = parse_arguments(argc,argv);
	if(ret != 0){
		exit(0);
	}
	init_args.sensor_num = SENSOR_NUM;
	printf("--sensor_num:	%d\n",init_args.sensor_num);
	printf("--enc_chn:	%d\n",init_args.enc_chn);
	printf("--ivdc:		%d\n",init_args.ivdc);
	printf("--seconds:	%d\n",init_args.sleep_time);
	printf("--count:	%d\n",init_args.enc_count);
	printf("--test:		%d\n",init_args.test);

	for (i = 0; i < FS_CHN_NUM; i++) {
		chn[i].enable = 0;
	}
	for(i = 0;i < init_args.enc_chn;i++){
		chn[i].enable = 1;
	}
	if(init_args.ivdc){
		direct_switch = 1;
	}
	if (SENSOR_NUM > IMPISP_TOTAL_ONE) {
		for(i = 0;i < init_args.enc_chn;i++){
			chn[3 + i].enable = 1;
		}
		if(init_args.ivdc){
			direct_switch = 2;
		}
	}
	if (SENSOR_NUM > IMPISP_TOTAL_TWO) {
		for(i = 0;i < init_args.enc_chn;i++){
			chn[6 + i].enable = 1;
		}
		if(init_args.ivdc){
			direct_switch = 3;
		}
	}
	if (SENSOR_NUM > IMPISP_TOTAL_THR) {
		for(i = 0;i < init_args.enc_chn;i++){
			chn[9 + i].enable = 1;
		}
		if(init_args.ivdc){
			direct_switch = 4;
		}
	}

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

	for (i = 0; i < FS_CHN_NUM; i++) {
		if ((!chn[i].joint.enable && chn[i].enable) || chn[i].joint.output) {
			listen_num++;
		}
	}

	/* Step.1 系统初始化 */
	/* Step.1 System init */
	ret = sample_system_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "IMP_System_Init() failed\n");
		return -1;
	}
	/* Step.2 电源控制相关接口初始化*/
	/* Step.2 Initialization of PM related interfaces.*/
	ret = SU_PM_Init(NULL);
	if(ret != 0){
		IMP_LOG_ERR(TAG, "SU_PM_Init() failed\n");
		return -1;
	}
	/* Step.3 监听线程初始化*/
	/* Step.3 listening threads corresponding to three-way stream lines.*/
	ret = SU_PM_InitListenLock(listen_num);
	if(ret != 0){
		IMP_LOG_ERR(TAG, "SU_PM_ListenThreadNums() failed\n");
		return -1;
	}

	/* Step.4 获取监听线程*/
	ret = SU_PM_GetListenLockNums();
	if(ret < 0){
		IMP_LOG_ERR(TAG, "SU_PM_GetListenLockNums failed\n");
		return -1;
	}
	/* Step.5 FrameSource初始化 */
	/* Step.5 FrameSource init */
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

	for (i = 0; i < FS_CHN_NUM; i++) {
		if ((!chn[i].joint.enable && chn[i].enable) || chn[i].joint.output) {
			ret = IMP_Encoder_CreateGroup(chn[i].index);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "IMP_Encoder_CreateGroup(%d) error !\n", chn[i].index);
				return -1;
			}
		}
	}
	/* Step.6 Encoder初始化 */
	/* Step.6 Encoder init */
	ret = sample_video_init();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Video init failed\n");
		return -1;
	}
	/* Step.7 绑定 */
	/* Step.7 Bind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if ((!chn[i].joint.enable && chn[i].enable) || chn[i].joint.output) {
			ret = IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "Bind FrameSource channel%d and Encoder failed\n",i);
				return -1;
			}
		}
	}
	/* Step.8 开启数据流 */
	/* Step.8 Stream On */
	ret = sample_framesource_streamon();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "ImpStreamOn failed\n");
		return -1;
	}

	/* Step.9 等待视频流画面稳定*/
	/* Step.9 Waiting for the video stream to stabilize.*/
	sleep(5);

	/* Step.10 创建休眠控制线程*/
	/* Step.10 Create dormancy control thread*/
	ret = suspend_thread_create();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "suspend_thread_create failed\n");
		return -1;
	}

	if(init_args.test){
		test_switch_mode();
	}
	/* Step.11 创建线程获取编码流*/
	/* Step.11 Create thread to acquire encoding stream*/
	ret = sample_zboost_stream();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "sample_zboost_stream failed\n");
		return -1;
	}

	/* Step.12 等待所有线程结束、回收*/
	/* Step.12 Block until all threads terminate and reclaim resources */
	pthread_cancel(suspend_tid);
	if(init_args.test){
		pthread_cancel(tid_switch_test);
	}
	pthread_join(suspend_tid,NULL);
	if(init_args.test){
		pthread_join(tid_switch_test,NULL);
	}

	/*
	* 每个sensor设置NORMAL模式, 每个sensor状态必须相同
	* 以免程序退出后仍处于NORMAL模式影响下一次运行
	* Set each sensor to the same NORMAL mode. The status must be the same.
	* */
	IMPISPPMAttr exit_mode = 0;
	for(i = IMPVI_MAIN;i < SENSOR_NUM;i++) {
		ret = IMP_ISP_PM_GetMode(i, &exit_mode);
		if(ret != 0){
			IMP_LOG_ERR(TAG, "IMP_ISP_PM_GetMode set mode failed\n");
		}
		if(exit_mode != TISP_NORMAL_MODE){
			exit_mode = TISP_NORMAL_MODE;
			IMP_ISP_PM_SetMode(i, &exit_mode);
			if(ret != 0){
				IMP_LOG_ERR(TAG, "IMP_ISP_PM_SetMode set normal mode error\n");
			}
		}
	}

	/* 下面是反初始化流程: */
	/* The deinitialization process follows: */
	/* Step.13 关流 */
	/* Step.13 Stream Off */
	ret = sample_framesource_streamoff();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource StreamOff failed\n");
		return -1;
	}

	/* Step.14 解绑 */
	/* Step.14 UnBind */
	for (i = 0; i < FS_CHN_NUM; i++) {
		if ((!chn[i].joint.enable && chn[i].enable) || chn[i].joint.output) {
			ret = IMP_System_UnBind(&chn[i].framesource_chn, &chn[i].imp_encoder);
			if (ret < 0) {
				IMP_LOG_ERR(TAG, "UnBind FrameSource channel%d and Encoder failed\n",i);
				return -1;
			}
		}
	}


	/* Step.15 Encoder反初始化 */
	/* Step.15 Encoder exit */
	ret = sample_video_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "Video exit failed\n");
		return -1;
	}

	/* Step.16 FrameSource反初始化 */
	/* Step.16 FrameSource exit */
	ret = sample_framesource_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "FrameSource exit failed\n");
		return -1;
	}

	/* Step.17 电源控制反初始化 */
	/* Step.17 PM exit */
	ret = SU_PM_DeInit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "SU_PM_DeInit failed\n");
		return -1;
	}

	/* Step.18 系统反初始化 */
	/* Step.18 System exit */
	ret = sample_system_exit();
	if (ret < 0) {
		IMP_LOG_ERR(TAG, "sample_system_exit() failed\n");
		return -1;
	}
	return 0;
}
