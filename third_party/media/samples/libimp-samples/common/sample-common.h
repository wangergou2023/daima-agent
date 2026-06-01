/*
 * Copyright (C) 2014 Ingenic Semiconductor Co.,Ltd
 */

#ifndef __SAMPLE_COMMON_H__
#define __SAMPLE_COMMON_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>

#include <imp/imp_system.h>
#include <imp/imp_isp.h>
#include <imp/imp_framesource.h>
#include <imp/imp_osd.h>
#include <imp/imp_ivs.h>
#include <imp/imp_encoder.h>
#include <imp/imp_audio.h>
#include <imp/imp_dmic.h>
#include <imp/imp_log.h>
#include <imp/imp_common.h>
#include <imp/imp_utils.h>
#include <imp/imp_ldc.h>

#ifdef __cplusplus
#if __cplusplus
extern "C"
{
#endif
#endif /* __cplusplus */
/********************************************     Sensor属性表格     *********************************************/
/******************************************** Sensor Attribute Table *********************************************/
/* 		NAME		            I2C_ADDR		RESOLUTION		Default_Boot			        				*/
/* 		名称		             I2C地址		    分辨率		    默认Boot			        				*/
/* 		gc2063/gc2063s1		      0x37 			1920*1080		0:30fps_mipi 1:15fps_mipi						*/
/* 		gc2063s2/gc2063s3		  0x3f 			1920*1080		0:30fps_mipi 1:15fps_mipi						*/
/* 		gc3003		              0x37 			2304*1296		0:30fps_mipi						            */
/* 		gc4023		              0x29 			2560*1440		0:30fps_mipi						            */
/* 		gc4653		              0x29 			2560*1440		0:30fps_mipi						            */
/* 		gc5603		              0x31 			2880*1620		0:30fps_mipi						            */
/* 		gc5613		              0x31 			2880*1620		0:30fps_mipi 1:20fps_mipi_hdr 2:25fps_mipi      */
/* 		gc8613		              0x31 			3840*2160		0:25fps_mipi 1:15fps_mipi                       */
/* 		gc8613a		              0x31 			3840*2160		0:20fps_mipi                                    */
/* 		sc200ai/sc200ais1         0x30 			1920*1080		0:30fps_mipi_1lane 1:60fps_mipi_2lane			*/
/* 		sc231hai/sc231hais1       0x30 			1920*1080		0:30fps_mipi								    */
/* 		sc401ai		              0x30 			2560*1440		0:30fps_mipi								    */
/* 		sc500ai		              0x32 			2880*1620		0:40fps_mipi								    */
/* 		sc8238		              0x30 			3840*2160		0:30fps_mipi_4lane 								*/
/********************************************     Sensor属性表格     *********************************************/
/******************************************** Sensor Attribute Table *********************************************/


#define SENSOR_NUM                  1

#if (SENSOR_NUM == 1)
#define SENSOR_GC2083
#elif (SENSOR_NUM == 2)
#define SENSOR_SC4336P
#define SENSOR1_GC5613S1
#elif (SENSOR_NUM == 3)
#define SENSOR_GC2063
#define SENSOR1_GC2063
#define SENSOR2_GC2063
#elif (SENSOR_NUM == 4)
#define SENSOR_GC2063
#define SENSOR1_GC2063
#define SENSOR2_GC2063
#define SENSOR3_GC2063
#endif


#if defined SENSOR_GC2063
#define FIRST_SNESOR_NAME                   "gc2063"                    //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FIRST_I2C_ADDR                      0x37                        //[sensor i2c地址][sensor i2c address]
#define FIRST_SENSOR_WIDTH                  1920                        //[sensor宽度][sensor width]
#define FIRST_SENSOR_HEIGHT                 1080                        //[sensor高度][sensor height]
#define FIRST_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN0_EN                             1                           //[sensor0，output0]
#define CHN1_EN                             1                           //[sensor0，output1]
#define CHN2_EN                             0                           //[sensor0, output2]
#define FIRST_CROP_EN                       0
#elif defined SENSOR_SC231HAI
#define FIRST_SNESOR_NAME                   "sc231hai"                    //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FIRST_I2C_ADDR                      0x30                        //[sensor i2c地址][sensor i2c address]
#define FIRST_SENSOR_WIDTH                  1920                        //[sensor宽度][sensor width]
#define FIRST_SENSOR_HEIGHT                 1080                        //[sensor高度][sensor height]
#define FIRST_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN0_EN                             1                           //[sensor0，output0]
#define CHN1_EN                             1                           //[sensor0，output1]
#define CHN2_EN                             1                           //[sensor0, output2]
#define FIRST_CROP_EN                       0
#elif defined SENSOR_GC2083
#define FIRST_SNESOR_NAME                   "gc2083"                    //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FIRST_I2C_ADDR                      0x37                        //[sensor i2c地址][sensor i2c address]
#define FIRST_SENSOR_WIDTH                  1920                        //[sensor宽度][sensor width]
#define FIRST_SENSOR_HEIGHT                 1080                        //[sensor高度][sensor height]
#define FIRST_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN0_EN                             1                           //[sensor0，output0]
#define CHN1_EN                             0                           //[sensor0，output1]
#define CHN2_EN                             0                           //[sensor0, output2]
#define FIRST_CROP_EN                       0
#elif defined SENSOR_GC3003
#define FIRST_SNESOR_NAME                   "gc3003"                    //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FIRST_I2C_ADDR                      0x37                        //[sensor i2c地址][sensor i2c address]
#define FIRST_SENSOR_WIDTH                  2304                        //[sensor宽度][sensor width]
#define FIRST_SENSOR_HEIGHT                 1296                        //[sensor高度][sensor height]
#define FIRST_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN0_EN                             1                           //[sensor0，output0]
#define CHN1_EN                             1                           //[sensor0，output1]
#define CHN2_EN                             0                           //[sensor0, output2]
#define FIRST_CROP_EN                       0
#elif defined SENSOR_SC4336P
#define FIRST_SNESOR_NAME                   "sc4336p"                   //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FIRST_I2C_ADDR                      0x30                        //[sensor i2c地址][sensor i2c address]
#define FIRST_SENSOR_WIDTH                  2560                        //[sensor宽度][sensor width]
#define FIRST_SENSOR_HEIGHT                 1440                        //[sensor高度][sensor height]
#define FIRST_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN0_EN                             1                           //[sensor0，output0]
#define CHN1_EN                             1                           //[sensor0，output1]
#define CHN2_EN                             0                           //[sensor0, output2]
#define FIRST_CROP_EN                       0
#elif defined SENSOR_GC4023
#define FIRST_SNESOR_NAME                   "gc4023"                    //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FIRST_I2C_ADDR                      0x29                        //[sensor i2c地址][sensor i2c address]
#define FIRST_SENSOR_WIDTH                  2560                        //[sensor宽度][sensor width]
#define FIRST_SENSOR_HEIGHT                 1440                        //[sensor高度][sensor height]
#define FIRST_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN0_EN                             1                           //[sensor0，output0]
#define CHN1_EN                             1                           //[sensor0，output1]
#define CHN2_EN                             0                           //[sensor0, output2]
#define FIRST_CROP_EN                       0
#elif defined SENSOR_GC4653
#define FIRST_SNESOR_NAME                   "gc4653"                    //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FIRST_I2C_ADDR                      0x29                        //[sensor i2c地址][sensor i2c address]
#define FIRST_SENSOR_WIDTH                  2560                        //[sensor宽度][sensor width]
#define FIRST_SENSOR_HEIGHT                 1440                        //[sensor高度][sensor height]
#define FIRST_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN0_EN                             1                           //[sensor0，output0]
#define CHN1_EN                             1                           //[sensor0，output1]
#define CHN2_EN                             0                           //[sensor0, output2]
#define FIRST_CROP_EN                       0
#elif defined SENSOR_GC5603
#define FIRST_SNESOR_NAME                   "gc5603"                    //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FIRST_I2C_ADDR                      0x31                        //[sensor i2c地址][sensor i2c address]
#define FIRST_SENSOR_WIDTH                  2880                        //[sensor宽度][sensor width]
#define FIRST_SENSOR_HEIGHT                 1620                        //[sensor高度][sensor height]
#define FIRST_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN0_EN                             1                           //[sensor0，output0]
#define CHN1_EN                             1                           //[sensor0，output1]
#define CHN2_EN                             0                           //[sensor0, output2]
#define FIRST_CROP_EN                       0
#elif defined SENSOR_GC5613
#define FIRST_SNESOR_NAME                   "gc5613"                    //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FIRST_I2C_ADDR                      0x31                        //[sensor i2c地址][sensor i2c address]
#define FIRST_SENSOR_WIDTH                  2880                        //[sensor宽度][sensor width]
#define FIRST_SENSOR_HEIGHT                 1620                        //[sensor高度][sensor height]
#define FIRST_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN0_EN                             1                           //[sensor0，output0]
#define CHN1_EN                             1                           //[sensor0，output1]
#define CHN2_EN                             0                           //[sensor0, output2]
#define FIRST_CROP_EN                       0
#elif defined SENSOR_GC8613
#define FIRST_SNESOR_NAME                   "gc8613"                    //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FIRST_I2C_ADDR                      0x31                        //[sensor i2c地址][sensor i2c address]
#define FIRST_SENSOR_WIDTH                  3840                        //[sensor宽度][sensor width]
#define FIRST_SENSOR_HEIGHT                 2160                        //[sensor高度][sensor height]
#define FIRST_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN0_EN                             1                           //[sensor0，output0]
#define CHN1_EN                             1                           //[sensor0，output1]
#define CHN2_EN                             0                           //[sensor0, output2]
#define FIRST_CROP_EN                       0
#elif defined SENSOR_GC8613A
#define FIRST_SNESOR_NAME                   "gc8613a"                   //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FIRST_I2C_ADDR                      0x31                        //[sensor i2c地址][sensor i2c address]
#define FIRST_SENSOR_WIDTH                  3840                        //[sensor宽度][sensor width]
#define FIRST_SENSOR_HEIGHT                 2160                        //[sensor高度][sensor height]
#define FIRST_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN0_EN                             1                           //[sensor0，output0]
#define CHN1_EN                             1                           //[sensor0，output1]
#define CHN2_EN                             0                           //[sensor0, output2]
#define FIRST_CROP_EN                       0
#elif defined SENSOR_SC200AI
#define FIRST_SNESOR_NAME                   "sc200ai"                   //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FIRST_I2C_ADDR                      0x30                        //[sensor i2c地址][sensor i2c address]
#define FIRST_SENSOR_WIDTH                  1920                        //[sensor宽度][sensor width]
#define FIRST_SENSOR_HEIGHT                 1080                        //[sensor高度][sensor height]
#define FIRST_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN0_EN                             1                           //[sensor0，output0]
#define CHN1_EN                             1                           //[sensor0，output1]
#define CHN2_EN                             0                           //[sensor0, output2]
#define FIRST_CROP_EN                       0
#elif defined SENSOR_SC235HAI
#define FIRST_SNESOR_NAME                   "sc231hai"                  //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FIRST_I2C_ADDR                      0x30                        //[sensor i2c地址][sensor i2c address]
#define FIRST_SENSOR_WIDTH                  1920                        //[sensor宽度][sensor width]
#define FIRST_SENSOR_HEIGHT                 1080                        //[sensor高度][sensor height]
#define FIRST_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN0_EN                             1                           //[sensor0，output0]
#define CHN1_EN                             1                           //[sensor0，output1]
#define CHN2_EN                             0                           //[sensor0, output2]
#define FIRST_CROP_EN                       0
#elif defined SENSOR_SC401AI
#define FIRST_SNESOR_NAME                   "sc401ai"                   //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FIRST_I2C_ADDR                      0x30                        //[sensor i2c地址][sensor i2c address]
#define FIRST_SENSOR_WIDTH                  2560                        //[sensor宽度][sensor width]
#define FIRST_SENSOR_HEIGHT                 1440                        //[sensor高度][sensor height]
#define FIRST_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN0_EN                             1                           //[sensor0，output0]
#define CHN1_EN                             1                           //[sensor0，output1]
#define CHN2_EN                             0                           //[sensor0, output2]
#define FIRST_CROP_EN                       0
#elif defined SENSOR_SC500AI
#define FIRST_SNESOR_NAME                   "sc500ai"                   //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FIRST_I2C_ADDR                      0x32                        //[sensor i2c地址][sensor i2c address]
#define FIRST_SENSOR_WIDTH                  2880                        //[sensor宽度][sensor width]
#define FIRST_SENSOR_HEIGHT                 1620                        //[sensor高度][sensor height]
#define FIRST_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN0_EN                             1                           //[sensor0，output0]
#define CHN1_EN                             1                           //[sensor0，output1]
#define CHN2_EN                             0                           //[sensor0, output2]
#define FIRST_CROP_EN                       0
#elif defined SENSOR_OS05L10
#define FIRST_SNESOR_NAME                   "os05l10"                   //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FIRST_I2C_ADDR                      0x3c                        //[sensor i2c地址][sensor i2c address]
#define FIRST_SENSOR_WIDTH                  2880                        //[sensor宽度][sensor width]
#define FIRST_SENSOR_HEIGHT                 1620                        //[sensor高度][sensor height]
#define FIRST_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN0_EN                             1                           //[sensor0，output0]
#define CHN1_EN                             1                           //[sensor0，output1]
#define CHN2_EN                             0                           //[sensor0, output2]
#define FIRST_CROP_EN                       0
#endif

#if defined SENSOR1_GC2063
#define SECOND_SNESOR_NAME                  "gc2063s1"                  //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define SECOND_I2C_ADDR                     0x37                        //[sensor i2c地址][sensor i2c address]
#define SECOND_SENSOR_WIDTH                 1920                        //[sensor宽度][sensor width]
#define SECOND_SENSOR_HEIGHT                1080                        //[sensor高度][sensor height]
#define SECOND_DEFAULT_BOOT                 0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN3_EN                             1                           //sensor1，output0
#define CHN4_EN                             1                           //sensor1，output1
#define CHN5_EN                             0                           //sensor1，output2
#define SECOND_CROP_EN                      0
#elif defined SENSOR1_SC231HAI
#define SECOND_SNESOR_NAME                  "sc231hais1"                  //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define SECOND_I2C_ADDR                     0x30                        //[sensor i2c地址][sensor i2c address]
#define SECOND_SENSOR_WIDTH                 1920                        //[sensor宽度][sensor width]
#define SECOND_SENSOR_HEIGHT                1080                        //[sensor高度][sensor height]
#define SECOND_DEFAULT_BOOT                 0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN3_EN                             1                           //sensor1，output0
#define CHN4_EN                             1                           //sensor1，output1
#define CHN5_EN                             1                           //sensor1，output2
#define SECOND_CROP_EN                      0
#elif defined SENSOR1_GC2083
#define SECOND_SNESOR_NAME                  "gc2083s1"                  //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define SECOND_I2C_ADDR                     0x37                        //[sensor i2c地址][sensor i2c address]
#define SECOND_SENSOR_WIDTH                 1920                        //[sensor宽度][sensor width]
#define SECOND_SENSOR_HEIGHT                1080                        //[sensor高度][sensor height]
#define SECOND_DEFAULT_BOOT                 0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN3_EN                             1                           //sensor1，output0
#define CHN4_EN                             1                           //sensor1，output1
#define CHN5_EN                             0                           //sensor1，output2
#define SECOND_CROP_EN                      0
#elif defined SENSOR1_SC200AI
#define SECOND_SNESOR_NAME                  "sc200ais1"                 //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define SECOND_I2C_ADDR                     0x30                        //[sensor i2c地址][sensor i2c address]
#define SECOND_SENSOR_WIDTH                 1920                        //[sensor宽度][sensor width]
#define SECOND_SENSOR_HEIGHT                1080                        //[sensor高度][sensor height]
#define SECOND_DEFAULT_BOOT                 0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN3_EN                             1                           //sensor1，output0
#define CHN4_EN                             1                           //sensor1，output1
#define CHN5_EN                             0                           //sensor1，output2
#define SECOND_CROP_EN                      0
#elif defined SENSOR_SC235HAI
#define SECOND_SNESOR_NAME                 "sc231hais1"                //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define SECOND_I2C_ADDR                     0x30                        //[sensor i2c地址][sensor i2c address]
#define SECOND_SENSOR_WIDTH                 1920                        //[sensor宽度][sensor width]
#define SECOND_SENSOR_HEIGHT                1080                        //[sensor高度][sensor height]
#define SECOND_DEFAULT_BOOT                 0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN3_EN                             1                           //sensor1，output0
#define CHN4_EN                             1                           //sensor1，output1
#define CHN5_EN                             0                           //sensor1，output2
#define SECOND_CROP_EN                      0
#elif defined SENSOR1_GC5613
#define SECOND_SNESOR_NAME                  "gc5613s1"                   //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define SECOND_I2C_ADDR                     0x31                       //[sensor i2c地址][sensor i2c address]
#define SECOND_SENSOR_WIDTH                 2880                       //[sensor宽度][sensor width]
#define SECOND_SENSOR_HEIGHT                1620                       //[sensor高度][sensor height]
#define SECOND_DEFAULT_BOOT                 0                          //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN3_EN                             1                           //sensor1，output0
#define CHN4_EN                             1                           //sensor1，output1
#define CHN5_EN                             0                           //sensor1，output2
#define SECOND_CROP_EN                      0
#elif defined SENSOR1_GC5613S1
#define SECOND_SNESOR_NAME                  "gc5613"                   //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define SECOND_I2C_ADDR                     0x31                       //[sensor i2c地址][sensor i2c address]
#define SECOND_SENSOR_WIDTH                 2880                       //[sensor宽度][sensor width]
#define SECOND_SENSOR_HEIGHT                1620                       //[sensor高度][sensor height]
#define SECOND_DEFAULT_BOOT                 0                          //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN3_EN                             1                           //sensor1，output0
#define CHN4_EN                             1                           //sensor1，output1
#define CHN5_EN                             0                           //sensor1，output2
#define SECOND_CROP_EN                      0
#elif defined SENSOR1_SC3336P
#define SECOND_SNESOR_NAME                   "sc3336ps1"                 //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define SECOND_I2C_ADDR                      0x30                        //[sensor i2c地址][sensor i2c address]
#define SECOND_SENSOR_WIDTH                  2304                        //[sensor宽度][sensor width]
#define SECOND_SENSOR_HEIGHT                 1296                        //[sensor高度][sensor height]
#define SECOND_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN3_EN                              1                           //[sensor0，output0]
#define CHN4_EN                              1                           //[sensor0，output1]
#define CHN5_EN                              0                           //[sensor0, output2]
#define SECOND_CROP_EN                       0
#endif

#if defined SENSOR2_GC2063
#define THIRD_SNESOR_NAME                   "gc2063s2"                  //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define THIRD_I2C_ADDR                      0x3f                        //[sensor i2c地址][sensor i2c address]
#define THIRD_SENSOR_WIDTH                  1920                        //[sensor宽度][sensor width]
#define THIRD_SENSOR_HEIGHT                 1080                        //[sensor高度][sensor height]
#define THIRD_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN6_EN                             1                           //sensor3，output0
#define CHN7_EN                             1                           //sensor3，output1
#define CHN8_EN                             0                           //sensor3，output2
#define THIRD_CROP_EN                       0
#elif defined SENSOR2_GC2083
#define THIRD_SNESOR_NAME                   "gc2083s2"                  //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define THIRD_I2C_ADDR                      0x3f                        //[sensor i2c地址][sensor i2c address]
#define THIRD_SENSOR_WIDTH                  1920                        //[sensor宽度][sensor width]
#define THIRD_SENSOR_HEIGHT                 1080                        //[sensor高度][sensor height]
#define THIRD_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN6_EN                             1                           //sensor3，output0
#define CHN7_EN                             1                           //sensor3，output1
#define CHN8_EN                             0                           //sensor3，output2
#define THIRD_CROP_EN                       0
#elif defined SENSOR2_GC5613
#define THIRD_SNESOR_NAME                   "gc5613s2"                  //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define THIRD_I2C_ADDR                      0x10                        //[sensor i2c地址][sensor i2c address]
#define THIRD_SENSOR_WIDTH                  2880                        //[sensor宽度][sensor width]
#define THIRD_SENSOR_HEIGHT                 1620                        //[sensor高度][sensor height]
#define THIRD_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN6_EN                             1                           //sensor3，output0
#define CHN7_EN                             1                           //sensor3，output1
#define CHN8_EN                             0                           //sensor3，output2
#define THIRD_CROP_EN                       0
#elif defined SENSOR2_SC200AI
#define THIRD_SNESOR_NAME                   "sc200ais2"                 //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define THIRD_I2C_ADDR                      0x32                        //[sensor i2c地址][sensor i2c address]
#define THIRD_SENSOR_WIDTH                  1920                        //[sensor宽度][sensor width]
#define THIRD_SENSOR_HEIGHT                 1080                        //[sensor高度][sensor height]
#define THIRD_DEFAULT_BOOT                  0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN6_EN                             1                           //sensor3，output0
#define CHN7_EN                             1                           //sensor3，output1
#define CHN8_EN                             0                           //sensor3，output2
#define THIRD_CROP_EN                       0
#endif

#if defined SENSOR3_GC2063
#define FOURTH_SNESOR_NAME                  "gc2063s3"                  //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FOURTH_I2C_ADDR                     0x3f                        //[sensor i2c地址][sensor i2c address]
#define FOURTH_SENSOR_WIDTH                 1920                        //[sensor宽度][sensor width]
#define FOURTH_SENSOR_HEIGHT                1080                        //[sensor高度][sensor height]
#define FOURTH_DEFAULT_BOOT                 0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN9_EN                             1                           //sensor4，output0
#define CHN10_EN                            1                           //sensor4，output1
#define CHN11_EN                            0                           //sensor4，output2
#define FOURTH_CROP_EN                      0
#elif defined SENSOR3_GC2083
#define FOURTH_SNESOR_NAME                  "gc2083s3"                  //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FOURTH_I2C_ADDR                     0x3f                        //[sensor i2c地址][sensor i2c address]
#define FOURTH_SENSOR_WIDTH                 1920                        //[sensor宽度][sensor width]
#define FOURTH_SENSOR_HEIGHT                1080                        //[sensor高度][sensor height]
#define FOURTH_DEFAULT_BOOT                 0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN9_EN                             1                           //sensor4，output0
#define CHN10_EN                            1                           //sensor4，output1
#define CHN11_EN                            0                           //sensor4，output2
#define FOURTH_CROP_EN                      0
#elif defined SENSOR3_SC200AI
#define FOURTH_SNESOR_NAME                  "sc200ais3"                  //[sensor名称 （与sensor驱动名称匹配）][sensor name (match with sensor driver name)]
#define FOURTH_I2C_ADDR                     0x32                        //[sensor i2c地址][sensor i2c address]
#define FOURTH_SENSOR_WIDTH                 1920                        //[sensor宽度][sensor width]
#define FOURTH_SENSOR_HEIGHT                1080                        //[sensor高度][sensor height]
#define FOURTH_DEFAULT_BOOT                 0                           //[sensor默认模式（0/1/2/3/4）][sensor default mode(0/1/2/3/4)]
#define CHN9_EN                             1                           //sensor4，output0
#define CHN10_EN                            1                           //sensor4，output1
#define CHN11_EN                            0                           //sensor4，output2
#define FOURTH_CROP_EN                      0
#endif

/************************************ first sensor *************************************************/
#define FIRST_SENSOR_ID                     0                           //[sensor编号][sensor index]
#define FIRST_I2C_ADAPTER_ID                0                           //[sensor i2c控制器 （0/1/2/3）][sensor controller number used (0/1/2/3)]
#define FIRST_RST_GPIO                      GPIO_PA(20)                 //[sensor重启gpio][sensor reset gpio]
#define FIRST_PWDN_GPIO                     -1                          //[sensor下电gpio][sensor pwdn gpio]
#define FIRST_POWER_GPIO                    -1                          //[sensor上电gpio][sensor power gpio]
#define FIRST_SWITCH_GPIO                   GPIO_PA(25)                 //[sensor切换gpio][sensor switch gpio]
#define FIRST_VIDEO_INTERFACE               IMPISP_SENSOR_VI_MIPI_CSI0  //[sensor接口类型 （dvp/csi0/csi1）][sensor interface type (dvp/csi0/csi1)]
#define FIRST_MCLK                          IMPISP_SENSOR_MCLK0         //[sensor时钟源（mclk0/mclk1/mclk2）][sensor clk source (mclk0/mclk1/mclk2)]
#define FIRST_SENSOR_FRAME_RATE_NUM         30
#define FIRST_SENSOR_FRAME_RATE_DEN         1
#define FIRST_SENSOR_WIDTH_SECOND           720
#define FIRST_SENSOR_HEIGHT_SECOND          576
#define FIRST_SENSOR_WIDTH_THIRD            640
#define FIRST_SENSOR_HEIGHT_THIRD           360

/************************************ second sensor *************************************************/
#define SECOND_SENSOR_ID                    1                           //[sensor编号][sensor index]
#define SECOND_I2C_ADAPTER_ID               1                           //[sensor i2c控制器 （0/1/2/3）][sensor controller number used (0/1/2/3)]
#define SECOND_RST_GPIO                     -1                          //[sensor重启gpio][sensor reset gpio]
#define SECOND_PWDN_GPIO                    -1                          //[sensor下电gpio][sensor pwdn gpio]
#define SECOND_POWER_GPIO                   -1                          //[sensor上电gpio][sensor power gpio]
#define SECOND_SWITCH_GPIO                  GPIO_PA(26)                 //[sensor切换gpio][sensor switch gpio]
#define SECOND_VIDEO_INTERFACE              IMPISP_SENSOR_VI_MIPI_CSI1  //[sensor接口类型 （dvp/csi0/csi1）][sensor interface type (dvp/csi0/csi1)]
#define SECOND_MCLK                         IMPISP_SENSOR_MCLK1         //[sensor时钟源（mclk0/mclk1/mclk2）][sensor clk source (mclk0/mclk1/mclk2)]
#define SECOND_SENSOR_FRAME_RATE_NUM        15
#define SECOND_SENSOR_FRAME_RATE_DEN        1
#define SECOND_SENSOR_WIDTH_SECOND          720
#define SECOND_SENSOR_HEIGHT_SECOND         576
#define SECOND_SENSOR_WIDTH_THIRD           640
#define SECOND_SENSOR_HEIGHT_THIRD          360

/************************************ third sensor *************************************************/
#define THIRD_SENSOR_ID                     2                           //[sensor编号][sensor index]
#define THIRD_I2C_ADAPTER_ID                0                           //[sensor i2c控制器 （0/1/2/3）][sensor controller number used (0/1/2/3)]
#define THIRD_RST_GPIO                      -1                          //[sensor重启gpio][sensor reset gpio]
#define THIRD_PWDN_GPIO                     -1                          //[sensor下电gpio][sensor pwdn gpio]
#define THIRD_POWER_GPIO                    -1                          //[sensor上电gpio][sensor power gpio]
#define THIRD_SWITCH_GPIO                   GPIO_PA(25)                 //[sensor切换gpio][sensor switch gpio]
#define THIRD_VIDEO_INTERFACE               IMPISP_SENSOR_VI_MIPI_CSI0  //[sensor接口类型 （dvp/csi0/csi1）][sensor interface type (dvp/csi0/csi1)]
#define THIRD_MCLK                          IMPISP_SENSOR_MCLK0         //[sensor时钟源（mclk0/mclk1/mclk2）][sensor clk source (mclk0/mclk1/mclk2)]
#define THIRD_SENSOR_FRAME_RATE_NUM         15
#define THIRD_SENSOR_FRAME_RATE_DEN         1
#define THIRD_SENSOR_WIDTH_SECOND           720
#define THIRD_SENSOR_HEIGHT_SECOND          576
#define THIRD_SENSOR_WIDTH_THIRD            640
#define THIRD_SENSOR_HEIGHT_THIRD           360

/************************************ fourth sensor *************************************************/
#define FOURTH_SENSOR_ID                    3                           //[sensor编号][sensor index]
#define FOURTH_I2C_ADAPTER_ID               1                           //[sensor i2c控制器 （0/1/2/3）][sensor controller number used (0/1/2/3)]
#define FOURTH_RST_GPIO                     -1                          //[sensor重启gpio][sensor reset gpio]
#define FOURTH_PWDN_GPIO                    -1                          //[sensor下电gpio][sensor pwdn gpio]
#define FOURTH_POWER_GPIO                   -1                          //[sensor上电gpio][sensor power gpio]
#define FOURTH_SWITCH_GPIO                  GPIO_PA(26)                 //[sensor切换gpio][sensor switch gpio]
#define FOURTH_VIDEO_INTERFACE              IMPISP_SENSOR_VI_MIPI_CSI1  //[sensor接口类型 （dvp/csi0/csi1）][sensor interface type (dvp/csi0/csi1)]
#define FOURTH_MCLK                         IMPISP_SENSOR_MCLK1         //[sensor时钟源（mclk0/mclk1/mclk2）][sensor clk source (mclk0/mclk1/mclk2)]
#define FOURTH_SENSOR_FRAME_RATE_NUM        15
#define FOURTH_SENSOR_FRAME_RATE_DEN        1
#define FOURTH_SENSOR_WIDTH_SECOND          720
#define FOURTH_SENSOR_HEIGHT_SECOND         576
#define FOURTH_SENSOR_WIDTH_THIRD           640
#define FOURTH_SENSOR_HEIGHT_THIRD          360


#define BITRATE_720P_Kbs        1000

#define NR_FRAMES_TO_SAVE       200
#define NR_JPEG_TO_SAVE         3
#define STREAM_BUFFER_SIZE      (1 * 1024 * 1024)

#define ENC_VIDEO_CHANNEL       0
#define ENC_JPEG_CHANNEL        0

#define STREAM_FILE_PATH_PREFIX     "/tmp"
#define SNAP_FILE_PATH_PREFIX       "/tmp"

#define OSD_REGION_WIDTH            16
#define OSD_REGION_HEIGHT           34
#define OSD_REGION_WIDTH_SEC        8
#define OSD_REGION_HEIGHT_SEC       18


#define SLEEP_TIME          5

#define FS_CHN_NUM          12
#define IVS_CHN_ID          1

#define CHN0_INDEX  0
#define CHN1_INDEX  1
#define CHN2_INDEX  2
#define CHN3_INDEX  3
#define CHN4_INDEX  4
#define CHN5_INDEX  5
#define CHN6_INDEX  6
#define CHN7_INDEX  7
#define CHN8_INDEX  8
#define CHN9_INDEX  9
#define CHN10_INDEX  10
#define CHN11_INDEX  11

#define CHN_ENABLE 1
#define CHN_DISABLE 0

/* #define SUPPORT_RGB555LE */

struct chn_conf{
	unsigned int index;
	unsigned int enable;
	struct {
		unsigned int enable;
		unsigned int output;
		unsigned int width;
		unsigned int height;
	} joint;
	IMPPayloadType  payloadType;
	IMPFSChnAttr fs_chn_attr;
	IMPCell framesource_chn;
	IMPCell imp_encoder;
};

#define  CHN_NUM  ARRAY_SIZE(chn)

int sample_system_init();
int sample_system_exit();

typedef struct sample_osd_param{
	int chn;
	int *phandles;
	uint32_t *ptimestamps;
}IMP_Sample_OsdParam;

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __SAMPLE_COMMON_H__ */
