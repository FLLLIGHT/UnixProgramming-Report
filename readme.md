Author: Zitao Xuan

Contact: shxuanzitao@gmail.com

仅供学习交流使用，部分实现参考开源博客。

## 测试环境

CPU：Intel(R) Core(TM) i7-8750H CPU @ 2.20GHz 12 核

内存：8GB

操作系统：Ubuntu 20.04 LTS

源代码控制系统的版本截图请见/image/environment.png

## 目录结构及测试说明

/code/module：各个模块及测试

测试说明：在该目录下执行 make 指令，可获得 4 个模块的可执行文件，分别是单线程顺序服务器、多线程服务器、select 服务器和 epoll 服务器。可以将这些可执行文件放入对应的文件夹中后再进行测试（便于文件管理，否则模拟客户端发送的文件会全都直接存储在/code/module 目录下）。使用./指令可以直接运行这四个模块，默认在 9090 端口上进行监听。进入/code/module/client-test 目录，可执行指令模拟客户端同时发送文件，具体指令如下：

```shell
python simple-client.py -n 100 localhost 9090
```

其中 100 代表了会生成 100 个客户端同时发送文件，发送的文件大小可以通过设置 simple-client.py 代码中第 50 行的 for 语句循环数进行调整。

/code/system：根据论文中的说明，将选取各模块好的部分组装成的系统，主要参考已有的好的实现

测试说明，在/code/system 目录下执行 make 指令，可以获得可执行文件 server，使用./指令可以直接运行该文件系统，默认在 10000 端口上进行监听。进入/code/system/client-test 目录，执行 make 指令，可获得可执行文件 mock，使用./指令运行该文件，即可模拟客户端向服务器发送文件。

/image：实验截图

/image/environment.png：源代码控制系统的版本截图

/image/xxx-server-test：对应模块的测试截图

/image/xxx-server-test/xxx-server-status.png：对应模块服务器接收文件时的典型状态和接收顺序

/image/xxx-server-test/4Mb：对应模块接收 4Mb 大小文件的实验结果截图

/image/xxx-server-test/200Kb：对应模块接收 200Kb 大小文件的实验结果截图

/image/xxx-server-test/xxb/xxx-time.png：对应模块接收对应大小文件，在不同同时请求的客户数的情况下的响应时间

/image/xxx-server-test/xxb/xxx-with-**yy**-client-cpu-use.png：对应模块接收对应大小文件，在**yy**个不同客户同时发送请求时，服务器的 CPU 负载。

/image/xxx-server-test/xxb/xxx-with-**yy**-client-io-use.png：对应模块接收对应大小文件，在**yy**个不同客户同时发送请求时，服务器的磁盘 IO 写入速率。
