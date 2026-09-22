/* client.c —— 修正版：按协议手工打包字节流后发送
 * 编译：gcc -Wall -Wextra -O2 -std=c11 -o client client.c
 * 运行：./client 127.0.0.1 8888
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "protocol.h"

/* send() 不保证一次发完（短写），必须循环到全部写出 */
static int send_all(int sock, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, p + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("send");
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

/* 按协议封装一帧：AA 55 | LEN(大端2B) | DATA | XOR | 0D */
int send_frame(int sock, const char *msg) {
    size_t len = strlen(msg);
    if (len == 0 || len > MAX_DATA_LEN) {
        fprintf(stderr, "invalid length %zu\n", len);
        return -1;
    }

    unsigned char buf[4 + MAX_DATA_LEN + 2];        /* 头2 + 长2 + 数据 + 校验1 + 尾1 */
    unsigned char sum = 0;
    for (size_t i = 0; i < len; i++) sum ^= (unsigned char)msg[i];

    buf[0] = FRAME_HEAD0;
    buf[1] = FRAME_HEAD1;

    /* 字节序：多字节整数上网络必须转大端，否则对端解析出天文数字 */
    unsigned short nlen = htons((unsigned short)len);
    memcpy(buf + 2, &nlen, 2);

    memcpy(buf + 4, msg, len);
    buf[4 + len]     = sum;
    buf[4 + len + 1] = FRAME_TAIL;

    return send_all(sock, buf, 4 + len + 2);
}

int main(int argc, char *argv[]) {
    const char *ip   = (argc > 1) ? argv[1] : "127.0.0.1";
    int         port = (argc > 2) ? atoi(argv[2]) : 8888;

    signal(SIGPIPE, SIG_IGN);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {   /* 比 inet_addr 更安全 */
        fprintf(stderr, "invalid ip: %s\n", ip);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); return 1;
    }
    printf("Connected to %s:%d\n", ip, port);

    /* 连着发 3 条短消息 —— Nagle 算法会把它们合并成一个 TCP 段，
     * 服务端一次 recv 就会读到多帧，正好复现“粘包” */
    send_frame(sock, "Hello");
    send_frame(sock, "World");
    send_frame(sock, "This is a test");

    /* 再发一条大消息，超过一个 MSS 时会被切成多个 TCP 段 —— 复现“拆包” */
    char big[900];
    memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    send_frame(sock, big);

    printf("Sent 4 frames.\n");
    close(sock);
    return 0;
}
