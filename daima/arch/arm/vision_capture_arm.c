/* 图像抓拍（Host 占位实现）。
 * Host 平台不提供真实抓拍能力，统一返回不支持。
 */

#include "drivers/vision/vision_capture.h"
#include "linux/printk.h"
err_t vision_capture_init(void)
{
    /* Host 平台无 IMP 支持 */
    pr_warn("vision_capture_init not implemented");
    return ERR_INVALID_STATE;
}

err_t vision_capture_jpeg(const char *output_path,
                               char *out_path,
                               size_t out_path_len)
{
    (void)output_path;
    /* 返回空路径，提示调用方失败 */
    if (out_path && out_path_len > 0) {
        out_path[0] = '\0';
    }
    pr_warn("vision_capture_jpeg not implemented");
    return ERR_INVALID_STATE;
}

void vision_capture_shutdown(void)
{
    /* 无资源需要释放，仅打印提示 */
    pr_warn("vision_capture_shutdown not implemented");
}
