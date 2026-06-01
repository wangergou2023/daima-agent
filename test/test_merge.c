/* 验证均衡合并 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static size_t rune_len(const char *s) {
    size_t n = 0;
    while (*s) { if ((*s & 0xC0) != 0x80) n++; s++; }
    return n;
}

int main(void) {
    struct { char t[512]; } groups[] = {
        {"菜市场卖鱼的周伯，砧板旁边总放着一小碟鱼鳔"},
        {"不是卖的——旁边修鞋的哑巴老吴爱吃这个"},
        {"每天收摊前，周伯就把鱼鳔用油纸包好，搁在老吴的工具箱上"},
        {"二十年了，两个老头从来没说过一句话"},
        {"老吴去世后，周伯的砧板边还是放着那碟鱼鳔，没动过"},
    };
    int g_n = 5;
    int MAX = 3;

    printf("=== Before merge (%d groups) ===\n", g_n);
    for (int i=0; i<g_n; i++) printf("  [%d] %2zu字 \"%s\"\n", i, rune_len(groups[i].t), groups[i].t);

    while (g_n > MAX) {
        int best = 0;
        size_t best_len = (size_t)-1;
        for (int i = 0; i < g_n - 1; i++) {
            size_t combined = rune_len(groups[i].t) + rune_len(groups[i+1].t);
            if (combined < best_len) { best = i; best_len = combined; }
        }
        printf("  merge [%d]+[%d] (%zu chars)\n", best, best+1, best_len);
        char merged[1024];
        snprintf(merged, sizeof(merged), "%s。%s", groups[best].t, groups[best+1].t);
        snprintf(groups[best].t, sizeof(groups[0].t), "%s", merged);
        for (int i = best + 1; i < g_n - 1; i++)
            snprintf(groups[i].t, sizeof(groups[0].t), "%s", groups[i+1].t);
        g_n--;
    }

    printf("\n=== After balanced merge (%d groups) ===\n", g_n);
    for (int i=0; i<g_n; i++) printf("  [%d] %2zu字 \"%s\"\n", i, rune_len(groups[i].t), groups[i].t);

    return 0;
}
