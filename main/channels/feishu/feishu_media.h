/* 飞书媒体资源下载辅助。 */

#pragma once

char *feishu_download_message_image(const char *tenant_token,
                                    const char *message_id,
                                    const char *image_key);
