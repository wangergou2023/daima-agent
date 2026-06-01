/* tts_player 单元测试 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <assert.h>

/* ---- 从 tts_player.c 复制的被测函数 ---- */

static void apply_gain(int16_t *samples, size_t count, double factor)
{
    for (size_t i = 0; i < count; i++) {
        double s = (double)samples[i] * factor;
        samples[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
    }
}

static void apply_lowpass(int16_t *samples, size_t count, double cutoff, int sample_rate)
{
    if (count < 2) return;
    double rc = 1.0 / (2.0 * 3.141592653589793 * cutoff);
    double dt = 1.0 / sample_rate;
    double alpha = dt / (rc + dt);
    double prev = (double)samples[0];
    for (size_t i = 1; i < count; i++) {
        double current = (double)samples[i];
        double filtered = alpha * current + (1.0 - alpha) * prev;
        samples[i] = (int16_t)(filtered > 32767 ? 32767 : (filtered < -32768 ? -32768 : filtered));
        prev = filtered;
    }
}

/* ---- 测试 ---- */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  %-50s", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define ASSERT_EQ(a, b, msg) do { if ((a) != (b)) { FAIL(msg); return; } } while(0)
#define ASSERT_NEAR(a, b, tol, msg) do { if (fabs((a) - (b)) > (tol)) { FAIL(msg); return; } } while(0)

static void test_gain_amplification(void)
{
    TEST("gain 2x doubles amplitude");
    int16_t samples[] = { 100, -50, 0, 16000 };
    apply_gain(samples, 4, 2.0);
    ASSERT_EQ(samples[0], 200, "100*2 != 200");
    ASSERT_EQ(samples[1], -100, "-50*2 != -100");
    ASSERT_EQ(samples[2], 0, "0*2 != 0");
    ASSERT_EQ(samples[3], 32000, "16000*2 != 32000");
    PASS();
}

static void test_gain_clipping(void)
{
    TEST("gain 2x clips to int16_t range");
    int16_t samples[] = { 20000, -20000, 32767, -32768 };
    apply_gain(samples, 4, 2.0);
    ASSERT_EQ(samples[0], 32767, "20000*2 not clamped to INT16_MAX");
    ASSERT_EQ(samples[1], -32768, "-20000*2 not clamped to INT16_MIN");
    ASSERT_EQ(samples[2], 32767, "max not preserved on clamp");
    ASSERT_EQ(samples[3], -32768, "min not preserved on clamp");
    PASS();
}

static void test_lowpass_noop_on_dc(void)
{
    TEST("lowpass preserves DC signal");
    int16_t samples[100];
    for (int i = 0; i < 100; i++) samples[i] = 1000;
    apply_lowpass(samples, 100, 4000.0, 16000);
    for (int i = 0; i < 100; i++) {
        if (samples[i] != 1000) { FAIL("DC signal changed"); return; }
    }
    PASS();
}

static void test_lowpass_attenuates_high_freq(void)
{
    TEST("lowpass attenuates high frequency");
    int num_samples = 200;
    int16_t *samples = malloc(num_samples * sizeof(int16_t));
    int16_t *original = malloc(num_samples * sizeof(int16_t));
    if (!samples || !original) { FAIL("malloc"); return; }

    /* 6kHz tone sampled at 16kHz (avoid Nyquist: sin(pi*i)=0 for all i) */
    for (int i = 0; i < num_samples; i++) {
        double val = 10000.0 * sin(2.0 * 3.1415926535 * 6000.0 * i / 16000.0);
        samples[i] = (int16_t)val;
        original[i] = samples[i];
    }

    apply_lowpass(samples, num_samples, 4000.0, 16000);

    int peak_before = 0, peak_after = 0;
    for (int i = 0; i < num_samples; i++) {
        if (abs(original[i]) > peak_before) peak_before = abs(original[i]);
        if (abs(samples[i]) > peak_after) peak_after = abs(samples[i]);
    }
    printf(" (peak: %d -> %d)", peak_before, peak_after);

    if (peak_after >= peak_before * 0.85) {
        FAIL("6kHz tone not attenuated by 4kHz lowpass");
        free(samples); free(original);
        return;
    }

    free(samples);
    free(original);
    PASS();
}

static void test_lowpass_short_input(void)
{
    TEST("lowpass handles single sample");
    int16_t s[] = { 42 };
    apply_lowpass(s, 1, 4000.0, 16000);
    ASSERT_EQ(s[0], 42, "single sample changed");
    PASS();
}

/* ---- sentence splitting test ---- */

static void test_sentence_split(void)
{
    TEST("sentence splitting by Chinese delimiters");

    const char *text = "你好。今天天气怎么样？真好！\n走吧";
    char *buf = strdup(text);
    if (!buf) { FAIL("strdup"); return; }

    /* Replace CJK punctuation with \n (same as tts_player_speak) */
    static const char *punct_pairs[][2] = {
        {"。", "\n"}, {"！", "\n"}, {"？", "\n"},
        {".", "\n"}, {"!", "\n"}, {"?", "\n"},
        {NULL, NULL},
    };
    for (int i = 0; punct_pairs[i][0]; i++) {
        const char *needle = punct_pairs[i][0];
        size_t nlen = strlen(needle);
        char *pos = buf;
        while ((pos = strstr(pos, needle)) != NULL) {
            memset(pos, '\n', nlen);
            pos += nlen;
        }
    }

    char *saveptr = NULL;
    char *sentence = strtok_r(buf, "\n", &saveptr);

    const char *expected[] = {"你好", "今天天气怎么样", "真好", "走吧"};
    int idx = 0;

    while (sentence) {
        while (*sentence == ' ' || *sentence == '\t' || *sentence == '\n') sentence++;
        if (!*sentence) { sentence = strtok_r(NULL, "\n", &saveptr); continue; }
        printf(" [%d:'%s']", idx, sentence);
        if (idx >= 4 || strcmp(sentence, expected[idx]) != 0) {
            printf("\n");
            FAIL("sentence mismatch");
            free(buf);
            return;
        }
        idx++;
        sentence = strtok_r(NULL, "\n", &saveptr);
    }

    if (idx != 4) { printf("\n"); FAIL("wrong sentence count"); free(buf); return; }

    free(buf);
    PASS();
}

static void test_empty_sentence_skipped(void)
{
    TEST("whitespace-only sentences are skipped");

    const char *text = "第一句。   。第三句";
    char *buf = strdup(text);
    if (!buf) { FAIL("strdup"); return; }

    static const char *punct_pairs[][2] = {
        {"。", "\n"}, {".", "\n"}, {"！", "\n"}, {"？", "\n"},
        {"!", "\n"}, {"?", "\n"}, {NULL, NULL},
    };
    for (int i = 0; punct_pairs[i][0]; i++) {
        const char *needle = punct_pairs[i][0];
        size_t nlen = strlen(needle);
        char *pos = buf;
        while ((pos = strstr(pos, needle)) != NULL) {
            memset(pos, '\n', nlen);
            pos += nlen;
        }
    }

    char *saveptr = NULL;
    char *sentence = strtok_r(buf, "\n", &saveptr);

    int count = 0;
    while (sentence) {
        while (*sentence == ' ' || *sentence == '\t' || *sentence == '\n') sentence++;
        if (*sentence) { printf(" [%d:'%s']", count, sentence); count++; }
        sentence = strtok_r(NULL, "\n", &saveptr);
    }
    free(buf);

    if (count != 2) {
        printf("\n");
        FAIL("should have 2 non-empty sentences");
        return;
    }
    PASS();
}

int main(void)
{
    printf("=== tts_player unit tests ===\n\n");

    test_gain_amplification();
    test_gain_clipping();
    test_lowpass_noop_on_dc();
    test_lowpass_attenuates_high_freq();
    test_lowpass_short_input();
    test_sentence_split();
    test_empty_sentence_skipped();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
