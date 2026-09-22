#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "protocol.h"

static int build(unsigned char *b, const char *m) {
    int n = strlen(m), s = 0;
    for (int i = 0; i < n; i++) s ^= (unsigned char)m[i];
    b[0] = 0xAA; b[1] = 0x55;
    b[2] = n >> 8; b[3] = n & 0xFF;
    memcpy(b + 4, m, n);
    b[4 + n] = s; b[5 + n] = 0x0D;
    return n + 6;
}

int main(int argc, char **argv) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(8888);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) { perror("connect"); return 1; }

    unsigned char buf[4096];
    if (argc > 1 && strcmp(argv[1], "split") == 0) {
        int len = build(buf, "This is a split frame");
        send(fd, buf, 6, 0);
        puts("send 6 bytes (half frame), sleep 1s");
        sleep(1);
        send(fd, buf + 6, len - 6, 0);
        printf("send rest %d bytes\n", len - 6);
    } else {
        int off = 0;
        off += build(buf + off, "Hello");
        off += build(buf + off, "World");
        off += build(buf + off, "Sticky");
        send(fd, buf, off, 0);
        printf("send %d bytes = 3 frames\n", off);
    }
    sleep(1);
    close(fd);
    return 0;
}
