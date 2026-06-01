/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>

/* 定义ioctl命令，命令是固定，不要修改! */
/* define ioctl command. they are fixed. don't modify! */
#define ADC_ENABLE		0
#define ADC_DISABLE		1
#define ADC_SET_VREF	2
#define ADC_PATH_LEN	32

#define ADC_PATH	"/dev/ingenic_adc_aux_" /* adc channal 0; adc通道为0*/
#define STD_VAL_VOLTAGE 1800 /* The unit is mv/1000. T10/T20 VREF=3300; T30/T21/T31/T40 VREF=1800; 参考电压，单位是mv */

#define ENABLE2DISABLE_TIME_DEBUG

int fd;
int vol_n;

void *adc_get_voltage_thread(void *argc)
{
	long int value = 0;
	int ret = 0;
	while(1)
	{
#ifdef ENABLE2DISABLE_TIME_DEBUG
		struct	timeval   tv_begin, tv_end;
		gettimeofday(&tv_begin, NULL);
#endif
		/* step.4 使能 adc */
		/* step.4 enable adc */
		ret = ioctl(fd, ADC_ENABLE);
		if(ret){
			printf("Failed to enable adc!\n");
			break;
		}

		/* step.5 获取adc的检测值 */
		/* step.5 get adc value */
		ret = read(fd, &value, sizeof(int));
		if(!ret){
			printf("Failed to read adc value!\n");
			break;
		}

		/* step.6 打印adc获取的检测值 */
		/* step.6 printf get value */
		printf("### adc value is : %ldmV ###\n", value/10);

		/* step.7 关闭adc */
		/* strp.7 disable adc */
		ret = ioctl(fd, ADC_DISABLE);
		if(ret){
		    printf("Failed to disable adc!\n");
		    break;
		}

#ifdef ENABLE2DISABLE_TIME_DEBUG
		gettimeofday(&tv_end, NULL);
		printf("result time = %ld usec !!\n", (tv_end.tv_sec * 1000 + tv_end.tv_usec) - (tv_begin.tv_sec * 1000 + tv_begin.tv_usec));
#endif

		sleep(1);
	}

	/* step.8 关闭adc 句柄*/
	/* step.8 close fd */
	close(fd);

	return NULL;
}

int main(int argc ,char *argv[])
{
	int ret = 0;
	int ch_id = 0; /* change test channal; adc的通道号 */
	char path[ADC_PATH_LEN];

	if(ch_id > 1) {
		printf("Error: ADC only supports chn0 and chn1 !!!!\n");
	}
	sprintf(path, "%s%d", ADC_PATH, ch_id);

	/* step.1 获取 adc 句柄 */
	/* step.1 get file fd of adc */
	fd = open(path, O_RDONLY);
	if(fd < 0) {
		printf("sample_adc:open error !\n");
		ret = -1;
		goto error_open;
	}

	/* step.2 设置adc的参考电压 */
	/* step.2 set reference voltage of adc */
	vol_n = STD_VAL_VOLTAGE;
	ret = ioctl(fd, ADC_SET_VREF, &vol_n);
	if(ret){
		printf("Failed to set reference voltage!\n");
		goto error_vol;
	}

	/* step.3 获取 adc 检测值的线程 */
	/* step.3 get value thread */
	pthread_t adc_thread_id;
	ret = pthread_create(&adc_thread_id, NULL, adc_get_voltage_thread, NULL);
	if (ret != 0) {
		printf("error: pthread_create error!!!!!!");
		return -1;
	}

	/* step.9 关闭 adc */
	/* step.9 disable adc */
	ret = ioctl(fd, ADC_DISABLE);
	if(ret) {
		printf("Failed to disable adc!\n");
		pthread_join(adc_thread_id, NULL);
		return -1;
	}

	pthread_join(adc_thread_id, NULL);

	return 0;

error_vol:
	close(fd);
error_open:
	return ret;
}
