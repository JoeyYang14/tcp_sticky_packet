#ifndef PROTOCOL_H
#define PROTOCOL_H

/* 帧格式（线上字节序，统一大端 / network byte order）：
 *
 *   +------+------+----------+----------+-------------+---------+------+
 *   | 0xAA | 0x55 | LEN高8位 | LEN低8位 | DATA[LEN]   | XOR校验 | 0x0D |
 *   +------+------+----------+----------+-------------+---------+------+
 *     1B     1B       1B         1B       0~1024 B       1B       1B
 *
 * 注意：绝不要把 struct 直接 send 出去！不同编译器/平台的字节对齐(padding)
 * 可能不一样，必须手工按字节打包成 unsigned char 数组再发。
 */

#define FRAME_HEAD0   0xAA          /* 帧头第 1 字节 */
#define FRAME_HEAD1   0x55          /* 帧头第 2 字节 */
#define FRAME_TAIL    0x0D          /* 帧尾 */
#define MAX_DATA_LEN  1024          /* 单帧数据区最大长度 */

/* 解析状态机的状态（5 个状态 + 校验/帧尾各占一步） */
typedef enum {
    STATE_IDLE = 0,     /* 等待帧头 0xAA           */
    STATE_HEADER,       /* 已收到 0xAA，等 0x55     */
    STATE_LENGTH_H,     /* 收长度高 8 位            */
    STATE_LENGTH_L,     /* 收长度低 8 位            */
    STATE_DATA,         /* 收数据区                 */
    STATE_CHECK,        /* 收 1 字节校验            */
    STATE_TAIL          /* 收帧尾 0x0D             */
} ParseState;

/* 每个连接一份的解析上下文：原代码用全局变量，多客户端会串味 */
typedef struct {
    ParseState     state;
    unsigned short length;              /* 已从网络序转换过的数据长度 */
    int            data_index;          /* 当前已收数据字节数 */
    unsigned char  data[MAX_DATA_LEN];
    unsigned char  checksum;            /* 收到的校验字节 */
} ParseCtx;

void ctx_reset(ParseCtx *ctx);

#endif
