CC ?= cc
CFLAGS = -Wall -Wextra -std=c11 -O0 -g -I../main -I../third_party -D_GNU_SOURCE
LDLIBS = -lm

.PHONY: test clean

test: test_tts_player test_work_item
	./test_tts_player
	./test_work_item

test_tts_player: test_tts_player.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

test_work_item: test_work_item.c ../main/tools/tool_work_item.c ../main/work_items/work_item_store.c ../main/app/daima_paths.c ../main/app/daima_fs.c ../third_party/cjson/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f test_tts_player test_work_item
