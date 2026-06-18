/* LLM 端到端集成测试 — 纯 socket，零项目依赖
 *
 * 用法:
 *   ./build-kbuild/agent &
 *   PORT=1234 make test_llm_integration
 *   # 默认 port=1234
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int connect_agent(int port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port) };
	a.sin_addr.s_addr = inet_addr("127.0.0.1");
	if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) { close(fd); return -1; }
	return fd;
}

static int recv_line(int fd, char *buf, int size, int timeout_sec)
{
	struct timeval tv = { .tv_sec = timeout_sec };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	int total = 0;
	while (total < size - 1) {
		ssize_t n = read(fd, buf + total, 1);
		if (n <= 0) break;
		if (buf[total] == '\n') { buf[total] = '\0'; return total; }
		total++;
	}
	buf[total] = '\0';
	return total > 0 ? total : -1;
}

int main(void)
{
	const char *port_s = getenv("PORT");
	int port = port_s ? atoi(port_s) : 1234;
	int total = 0, passed = 0;

	printf("--- LLM integration test (port %d) ---\n", port);

	int fd = connect_agent(port);
	if (fd < 0) {
		printf("SKIP: agent not running on port %d\n", port);
		return 0;
	}

	/* WebSocket 握手 */
	char hs[512];
	snprintf(hs, sizeof(hs),
		"GET / HTTP/1.1\r\n"
		"Host: 127.0.0.1:%d\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
		"Sec-WebSocket-Version: 13\r\n\r\n", port);
	write(fd, hs, strlen(hs));
	recv_line(fd, hs, sizeof(hs), 5);
	printf("  handshake: %s\n", strstr(hs, "101") ? "OK" : "FAIL");

	/* 构造 WebSocket text frame: "!test" */
	unsigned char frame[64];
	int pos = 0;
	frame[pos++] = 0x81;        /* FIN + text opcode */
	frame[pos++] = 0x05;        /* payload length = 5 */
	memcpy(frame + pos, "!test", 5);
	pos += 5;
	write(fd, frame, pos);

	/* 读取回复 */
	char buf[65536];
	int n = recv_line(fd, buf, sizeof(buf), 120);
	total++;
	if (n > 0) {
		printf("  self_test reply: %d bytes\n", n);
		/* 跳过 WS header (2-8 bytes depending on len) */
		char *body = buf;
		if ((unsigned char)body[1] == 0x7E) body += 4;
		else if ((unsigned char)body[1] == 0x7F) body += 10;
		else body += 2;

		/* 简单检查 JSON 包含 "passed" */
		if (strstr(body, "passed"))
			passed++;
		else
			printf("  FAIL: no 'passed' in response\n");
	} else {
		printf("  FAIL: no response within 120s\n");
	}

	close(fd);
	printf("=== %d/%d passed ===\n", passed, total);
	return passed == total ? 0 : 1;
}
