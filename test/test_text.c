#include <assert.h>
#include <string.h>
#include "text.h"

int main(void)
{
    /* Test safe_copy */
    char buf[16];
    safe_copy(buf, sizeof(buf), "hello");
    assert(strcmp(buf, "hello") == 0);
    safe_copy(buf, sizeof(buf), "this is way too long for the buffer");
    assert(strlen(buf) == 15);
    assert(buf[15] == '\0');
    
    /* Test str_ends_with */
    assert(str_ends_with("hello.txt", ".txt") == true);
    assert(str_ends_with("hello.txt", ".md") == false);
    
    /* Test text_shorten */
    char out[32];
    text_shorten("short", out, sizeof(out), 10);
    assert(strcmp(out, "short") == 0);
    text_shorten("this is a very long string that needs truncation", out, sizeof(out), 10);
    assert(strncmp(out, "this is a ", 10) == 0);
    assert(strstr(out, "...") != NULL);
    
    return 0;
}
