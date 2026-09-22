# tcp_sticky_packet
# tcp_sticky_packet（main 改的标题）
=======
# tcp_sticky_packet（dev 改的标题）
>>>>>>> dev

TCP 粘包与拆包的状态机解析实现。

## 帧格式
AA 55 | LEN(2B, 大端) | DATA | XOR | 0D

## 编译与运行
make            # 编译
make run-server # 启动服务端（另开终端）
make run-sticky # 发送 3 帧合成一次的粘包数据
make run-split  # 发送分两次写的拆包数据
