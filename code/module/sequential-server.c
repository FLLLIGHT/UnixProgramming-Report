// 单线程顺序服务器
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils.h"

//协议状态机
typedef enum
{
  WAIT_FOR_MSG,
  IN_MSG
} ProcessingState;

//接收客户端的文件内容并写入服务器端
void serve_connection(int sockfd)
{
  //避免系统优化使得客户端的请求被提前接收
  //更好地展现单线程顺序服务器的特性
  if (send(sockfd, "*", 1, 0) < 1)
  {
    perror_die("send");
  }

  //初始化协议状态机的状态参数
  ProcessingState state = WAIT_FOR_MSG;
  //创建文件
  char filename[20] = "client";
  sprintf(filename, "%s%d", "client", sockfd);
  FILE *fp = fopen(filename, "w+");
  //循环接收客户端的文件内容并写入指定文件
  while (1)
  {
    uint8_t buf[1024];
    //接收文件
    int len = recv(sockfd, buf, sizeof buf, 0);
    if (len < 0)
    {
      perror_die("recv");
    }
    else if (len == 0)
    {
      break;
    }

    for (int i = 0; i < len; ++i)
    {
      //根据文件内的参数，更改协议状态机的状态
      switch (state)
      {
      case WAIT_FOR_MSG:
        if (buf[i] == '^')
        {
          state = IN_MSG;
        }
        break;
      case IN_MSG:
        if (buf[i] == '$')
        {
          state = WAIT_FOR_MSG;
        }
        else
        {
          //写入文件，用标准I/O
          fputc(buf[i], fp);
          buf[i] += 1;
          if (send(sockfd, &buf[i], 1, 0) < 1)
          {
            perror("send error");
            close(sockfd);
            return;
          }
        }
        break;
      }
    }
  }
  fclose(fp);

  close(sockfd);
}

int main(int argc, char **argv)
{
  setvbuf(stdout, NULL, _IONBF, 0);

  //默认在9090端口进行监听
  int portnum = 9090;
  if (argc >= 2)
  {
    portnum = atoi(argv[1]);
  }
  printf("Serving on port %d\n", portnum);

  //初始化socket，绑定并监听
  int sockfd = listen_inet_socket(portnum);

  //开启服务器循环
  while (1)
  {
    struct sockaddr_in peer_addr;
    socklen_t peer_addr_len = sizeof(peer_addr);

    //使用accept等待客户端的请求
    int newsockfd =
        accept(sockfd, (struct sockaddr *)&peer_addr, &peer_addr_len);

    if (newsockfd < 0)
    {
      perror_die("ERROR on accept");
    }
    //提示已经与客户端连接
    report_peer_connected(&peer_addr, peer_addr_len);
    //执行客户端的请求
    serve_connection(newsockfd);
    printf("peer done\n");
  }

  return 0;
}
