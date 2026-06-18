#include "drivers/pet/pet_event.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void assert_pet_chat_id(const char *input, const char *expected)
{
    char out[64] = {0};
    assert(pet_build_chat_id(input, out, sizeof(out)));
    assert(strcmp(out, expected) == 0);
}

int main(void)
{
    assert_pet_chat_id("web_34tyra", "pet_web_34tyra");
    assert_pet_chat_id("pet_web_34tyra", "pet_web_34tyra");
    assert_pet_chat_id("pet_pet_web_34tyra", "pet_web_34tyra");

    char ws_chat_id[64] = {0};
    assert(pet_chat_id_to_ws_chat_id("pet_web_34tyra", ws_chat_id, sizeof(ws_chat_id)));
    assert(strcmp(ws_chat_id, "web_34tyra") == 0);

    printf("pet_event tests passed\n");
    return 0;
}
