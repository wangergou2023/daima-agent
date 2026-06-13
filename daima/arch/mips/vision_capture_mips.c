/* 图像抓拍（MIPS/IMP 实现）。
 * 依赖第三方 sample-common 代码，封装为简单抓拍接口。
 */

#include "drivers/vision/vision_capture.h"
#include "autoconf.h"
#include "env.h"
#include "linux/printk.h"
#include "text.h"

#include "sample-common.h"
#include "sample-common-framesource.h"
#include "sample-common-encoder.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "vision_capture";

#ifndef BUILD_FOR_MIPS
#error "vision_capture_mips.c must be built only when BUILD_FOR_MIPS is enabled"
#endif

extern struct chn_conf chn[];
extern void *get_jpeg_stream(void *args);

/* 资源状态标记：保证初始化/释放顺序正确 */
static bool s_system_ready = false;
static bool s_framesource_ready = false;
static bool s_groups_ready = false;
static bool s_jpeg_ready = false;
static bool s_bound = false;
static bool s_stream_on = false;
static bool s_inited = false;

/* 选择一个可用的 JPEG 通道（基于 sample 的 chn[] 配置） */
static int pick_jpeg_channel(void)
{
    return chn[0].enable ? FS_CHN_NUM + (int)(chn[0].index / 3) : -1;
}

/* 生成 sample 默认抓拍文件路径（与 sample-common 保持一致） */
static bool build_sample_snap_path(int chnNum, int idx, char *out_path, size_t out_path_len)
{
    if (!out_path || out_path_len == 0) return false;
    int fs_chn = (chnNum - FS_CHN_NUM) * 3;
    if (fs_chn < 0 || fs_chn >= FS_CHN_NUM) return false;

    IMPFSI2DAttr i2d_attr;
    memset(&i2d_attr, 0, sizeof(i2d_attr));
    if (IMP_FrameSource_GetI2dAttr(fs_chn, &i2d_attr) < 0) return false;

    int w = chn[fs_chn].fs_chn_attr.picWidth;
    int h = chn[fs_chn].fs_chn_attr.picHeight;
    if (i2d_attr.i2d_enable && i2d_attr.rotate_enable &&
        (i2d_attr.rotate_angle == 90 || i2d_attr.rotate_angle == 270)) {
        int tmp = w;
        w = h;
        h = tmp;
    }

    snprintf(out_path, out_path_len, "%s/snap-%d-%dx%d-%d.jpg",
             SNAP_FILE_PATH_PREFIX, chnNum, w, h, idx);
    return true;
}

/* 解绑 FrameSource 与 Encoder */
static void unbind_channels(void)
{
    for (int i = 0; i < FS_CHN_NUM; i++) {
        if (chn[i].enable) {
            int ret = IMP_System_UnBind(&chn[i].framesource_chn, &chn[i].imp_encoder);
            if (ret < 0) {
                DAIMA_LOGW(TAG, "UnBind FrameSource%d and Encoder%d failed",
                          chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
            }
        }
    }
}

/* 销毁 Encoder Group */
static void destroy_groups(void)
{
    for (int i = 0; i < FS_CHN_NUM; i++) {
        if (chn[i].enable) {
            IMP_Encoder_DestroyGroup(chn[i].index);
        }
    }
}

void vision_capture_shutdown(void)
{
    /* 依次关闭：stream -> bind -> jpeg -> group -> framesource -> system */
    if (s_stream_on) {
        if (sample_framesource_streamoff() < 0) {
            DAIMA_LOGW(TAG, "FrameSource StreamOff failed");
        }
        s_stream_on = false;
    }

    if (s_bound) {
        unbind_channels();
        s_bound = false;
    }

    if (s_jpeg_ready) {
        if (sample_jpeg_exit() < 0) {
            DAIMA_LOGW(TAG, "Jpeg exit failed");
        }
        s_jpeg_ready = false;
    }

    if (s_groups_ready) {
        destroy_groups();
        s_groups_ready = false;
    }

    if (s_framesource_ready) {
        if (sample_framesource_exit() < 0) {
            DAIMA_LOGW(TAG, "FrameSource exit failed");
        }
        s_framesource_ready = false;
    }

    if (s_system_ready) {
        if (sample_system_exit() < 0) {
            DAIMA_LOGW(TAG, "System exit failed");
        }
        s_system_ready = false;
    }

    s_inited = false;
}

daima_err_t vision_capture_init(void)
{
    if (s_inited) return DAIMA_OK;

    /* 1) 系统/FrameSource 初始化 */
    if (sample_system_init() < 0) {
        DAIMA_LOGE(TAG, "System init failed");
        goto fail;
    }
    s_system_ready = true;

    if (sample_framesource_init() < 0) {
        DAIMA_LOGE(TAG, "FrameSource init failed");
        goto fail;
    }
    s_framesource_ready = true;

    /* 2) 创建编码组（每个通道一个 group） */
    for (int i = 0; i < FS_CHN_NUM; i++) {
        if (chn[i].enable) {
            if (IMP_Encoder_CreateGroup(chn[i].index) < 0) {
                DAIMA_LOGE(TAG, "Encoder CreateGroup(%d) failed", chn[i].index);
                goto fail;
            }
        }
    }
    s_groups_ready = true;

    /* 3) JPEG 编码初始化 */
    if (sample_jpeg_init() < 0) {
        DAIMA_LOGE(TAG, "Jpeg init failed");
        goto fail;
    }
    s_jpeg_ready = true;

    /* 4) 可选 JPEG QP 覆盖（环境变量） */
    int jpeg_qp = daima_env_int_or_default("DAIMA_VISION_JPEG_QP", DAIMA_VISION_JPEG_QP);
    if (jpeg_qp != DAIMA_VISION_JPEG_QP) DAIMA_LOGI(TAG, "JPEG QP override: %d", jpeg_qp);
    if (jpeg_qp > 0) {
        for (int i = 0; i < FS_CHN_NUM; i++) {
            if (chn[i].enable) {
                IMP_Encoder_SetJpegQp(12 + (int)(chn[i].index / 3), jpeg_qp);
            }
        }
    }

    /* 5) 绑定 FrameSource -> Encoder */
    for (int i = 0; i < FS_CHN_NUM; i++) {
        if (chn[i].enable) {
            if (IMP_System_Bind(&chn[i].framesource_chn, &chn[i].imp_encoder) < 0) {
                DAIMA_LOGE(TAG, "Bind FrameSource%d and Encoder%d failed",
                          chn[i].framesource_chn.groupID, chn[i].imp_encoder.groupID);
                goto fail;
            }
        }
    }
    s_bound = true;

    /* 6) 启动采集流 */
    if (sample_framesource_streamon() < 0) {
        DAIMA_LOGE(TAG, "FrameSource StreamOn failed");
        goto fail;
    }
    s_stream_on = true;

    /* 7) 预热等待，确保首帧可用 */
    int warm_ms = daima_env_int_or_default("DAIMA_VISION_JPEG_WARMUP_MS", DAIMA_VISION_JPEG_WARMUP_MS);
    if (warm_ms != DAIMA_VISION_JPEG_WARMUP_MS) DAIMA_LOGI(TAG, "Warmup override: %d ms", warm_ms);
    if (warm_ms > 0) {
        usleep((useconds_t)warm_ms * 1000);
    }

    s_inited = true;
    DAIMA_LOGI(TAG, "Vision capture initialized");
    return DAIMA_OK;

fail:
    vision_capture_shutdown();
    return DAIMA_FAIL;
}

daima_err_t vision_capture_jpeg(const char *output_path,
                               char *out_path,
                               size_t out_path_len)
{
    /* output_path 暂未接入 sample-common */
    (void)output_path;
    if (out_path && out_path_len > 0) out_path[0] = '\0';

    daima_err_t err = vision_capture_init();
    if (err != DAIMA_OK) return err;

    /* 选择 JPEG 通道并触发抓拍 */
    int chnNum = pick_jpeg_channel();
    if (chnNum < 0) {
        DAIMA_LOGE(TAG, "No enabled JPEG channel");
        return DAIMA_ERR_INVALID_STATE;
    }

    get_jpeg_stream((void *)((PT_JPEG << 16) | (chnNum & 0xffff)));

    int last = NR_JPEG_TO_SAVE - 1;
    if (last < 0) return DAIMA_FAIL;

    char snap_path[256];
    if (!build_sample_snap_path(chnNum, last, snap_path, sizeof(snap_path))) {
        return DAIMA_FAIL;
    }

    /* 清理旧的历史抓拍，保留最后一张 */
    for (int i = 0; i < last; i++) {
        char tmp[256];
        if (build_sample_snap_path(chnNum, i, tmp, sizeof(tmp))) {
            unlink(tmp);
        }
    }

    /* 返回最终路径 */
    if (out_path && out_path_len > 0) {
        daima_safe_copy(out_path, out_path_len, snap_path);
    }
    return DAIMA_OK;
}
