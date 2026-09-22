/* server.c —— 修正版：带状态机的 TCP 拆包/粘包解析服务端
 * 编译：gcc -Wall -Wextra -O2 -std=c11 -o server server.c
 * 运行：./server 8888
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

void ctx_reset(ParseCtx *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = STATE_IDLE;
}

/* 简单异或校验：和数据区逐字节异或 */
static unsigned char xor_sum(const unsigned char *p, int n) {
    unsigned char s = 0;
    for (int i = 0; i < n; i++) s ^= p[i];
    return s;
}

/* 状态机解析：一次调用最多产出一帧
 * 返回值： >0 本帧占用的字节数（含帧尾），out/out_len 为解出的数据
 *         =0 数据不完整（半包），等待下次 recv 继续喂
 *         <0 帧错误（校验/长度/帧尾不对），调用方需重新同步
 */
int parse_frame(ParseCtx *ctx, const unsigned char *buf, int len,
                unsigned char *out, int *out_len) {
    int consumed = 0;

    for (int i = 0; i < len; i++) {
        unsigned char c = buf[i];
        consumed++;

        switch (ctx->state) {
        case STATE_IDLE:
            if (c == FRAME_HEAD0) ctx->state = STATE_HEADER;
            break;                                  /* 非帧头字节直接丢弃：重新同步 */

        case STATE_HEADER:
            ctx->state = (c == FRAME_HEAD1) ? STATE_LENGTH_H : STATE_IDLE;
            break;

        case STATE_LENGTH_H:
            ctx->length = c;                        /* 高 8 位先到（大端） */
            ctx->state = STATE_LENGTH_L;
            break;

        case STATE_LENGTH_L:
            ctx->length = (ctx->length << 8) | c;   /* 拼出完整长度 */
            if (ctx->length == 0 || ctx->length > MAX_DATA_LEN) {
                ctx_reset(ctx);
                return -1;                          /* 长度非法，防缓冲区溢出 */
            }
            ctx->data_index = 0;
            ctx->state = STATE_DATA;
            break;

        case STATE_DATA:
            ctx->data[ctx->data_index++] = c;
            if (ctx->data_index >= ctx->length)
                ctx->state = STATE_CHECK;           /* 数据收满，下一个字节才是校验 */
            break;

        case STATE_CHECK:
            ctx->checksum = c;
            ctx->state = STATE_TAIL;
            break;

        case STATE_TAIL:
            if (c == FRAME_TAIL && xor_sum(ctx->data, ctx->length) == ctx->checksum) {
                memcpy(out, ctx->data, ctx->length);
                *out_len = ctx->length;
                ctx_reset(ctx);                     /* 一帧结束，回到等帧头 */
                return consumed;
            }
            ctx_reset(ctx);
            return -1;                              /* 校验或帧尾错，丢弃该帧 */
        }
    }
    return 0;                                       /* 半包：等下一次 recv */
}

int main(int argc, char *argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : 8888;

    /* 对端已关闭时 send() 会触发 SIGPIPE 直接杀死进程，必须忽略 */
    signal(SIGPIPE, SIG_IGN);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }

    /* 端口处于 TIME_WAIT 时 bind 会失败（ Address already in use ） */
    int on = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
        perror("setsockopt"); return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));                 /* 结构体必须清零 */
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(listen_fd, SOMAXCONN) < 0) { perror("listen"); return 1; }
    printf("Server listening on port %d ...\n", port);

    int conn_fd = accept(listen_fd, NULL, NULL);
    if (conn_fd < 0) { perror("accept"); return 1; }
    printf("Client connected.\n");

    ParseCtx ctx;                                   /* 每连接一份上下文，不用全局变量 */
    ctx_reset(&ctx);
    unsigned char recv_buf[4096];

    while (1) {
        ssize_t n = recv(conn_fd, recv_buf, sizeof(recv_buf), 0);
        if (n < 0) {
            if (errno == EINTR) continue;           /* 被信号打断，重试即可 */
            perror("recv"); break;
        }
        if (n == 0) { printf("Client closed.\n"); break; }  /* 0 = 对端 FIN，不是错误 */

        /* 关键：一次 recv 可能含多帧（粘包）或半帧（拆包），必须循环榨干 */
        int off = 0;
        while (off < n) {
            unsigned char payload[MAX_DATA_LEN + 1];
            int plen = 0;
            int used = parse_frame(&ctx, recv_buf + off, n - off, payload, &plen);

            if (used > 0) {
                payload[plen] = '\0';               /* +1 空间，保证不越界 */
                printf("Received (%d bytes): %s\n", plen, payload);
                off += used;
            } else if (used < 0) {
                fprintf(stderr, "bad frame, resync\n");
                off++;                              /* 跳过 1 字节重新找帧头 */
                ctx_reset(&ctx);
            } else {
                break;                              /* 半包，等下一轮 recv */
            }
        }
    }

    close(conn_fd);
    close(listen_fd);
    return 0;
}
