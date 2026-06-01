CC ?= cc
CFLAGS = -Wall -Wextra -std=c11 -O0 -g -lm

.PHONY: test clean

test: test_tts_player
	./test_tts_player

test_tts_player: test_tts_player.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f test_tts_player
