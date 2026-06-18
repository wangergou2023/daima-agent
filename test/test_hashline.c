#include "drivers/tool/tool_hashline.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_hash_is_stable_and_four_hex_chars(void)
{
    char first[5] = {0};
    char second[5] = {0};

    hashline_hash_line("function hello() {", first);
    hashline_hash_line("function hello() {", second);

    assert(strcmp(first, second) == 0);
    assert(strlen(first) == 4);
    for (int i = 0; i < 4; i++) {
        assert((first[i] >= '0' && first[i] <= '9') ||
               (first[i] >= 'a' && first[i] <= 'f'));
    }
}

static void test_make_prefix_includes_line_number_hash_and_separator(void)
{
    char hash[5] = {0};
    char prefix[HASHLINE_PREFIX_MAX] = {0};
    char expected[HASHLINE_PREFIX_MAX] = {0};

    hashline_hash_line("#include <stdio.h>", hash);
    hashline_make_prefix(1, "#include <stdio.h>", prefix, sizeof(prefix));
    snprintf(expected, sizeof(expected), "1#%s|", hash);

    assert(strcmp(prefix, expected) == 0);
}

static void test_strip_prefix_returns_original_content(void)
{
    char hash[5] = {0};
    char line[128] = {0};

    hashline_hash_line("int main() {", hash);
    snprintf(line, sizeof(line), "3#%s| int main() {", hash);

    assert(strcmp(hashline_strip_prefix(line), " int main() {") == 0);
    assert(strcmp(hashline_strip_prefix("no prefix"), "no prefix") == 0);
    assert(strcmp(hashline_strip_prefix("12#bad|too short hash"), "12#bad|too short hash") == 0);
}

static void test_verify_line_matches_content_hash(void)
{
    char hash[5] = {0};

    hashline_hash_line("    return 0;", hash);

    assert(hashline_verify_line(4, "    return 0;", hash));
    assert(!hashline_verify_line(4, "    return 1;", hash));
    assert(!hashline_verify_line(0, "    return 0;", hash));
    assert(!hashline_verify_line(4, "    return 0;", "zzzz"));
}

int main(void)
{
    test_hash_is_stable_and_four_hex_chars();
    test_make_prefix_includes_line_number_hash_and_separator();
    test_strip_prefix_returns_original_content();
    test_verify_line_matches_content_hash();
    printf("test_hashline: OK\n");
    return 0;
}
