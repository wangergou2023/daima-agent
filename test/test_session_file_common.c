#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "drivers/memory/session_store_file_common.h"

int main(void)
{
    const char *tmp_path = "/tmp/test_session_file_common.tmp";
    unlink(tmp_path);
    
    /* Test write + read */
    assert(session_file_write_all(tmp_path, "hello world") == true);
    char buf[256];
    size_t len;
    assert(session_file_read_all(tmp_path, buf, sizeof(buf), &len) == true);
    assert(len == 11);
    assert(strcmp(buf, "hello world") == 0);
    
    /* Test overwrite */
    assert(session_file_write_all(tmp_path, "new content") == true);
    assert(session_file_read_all(tmp_path, buf, sizeof(buf), &len) == true);
    assert(len == 11);
    assert(strcmp(buf, "new content") == 0);
    
    /* Test read non-existent file */
    unlink(tmp_path);
    assert(session_file_read_all(tmp_path, buf, sizeof(buf), &len) == false);
    
    return 0;
}
