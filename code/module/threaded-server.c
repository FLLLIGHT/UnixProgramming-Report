//多线程并发服务器
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils.h"

//线程启动参数
typedef struct
{
  int sockfd;
} thread_config_t;

//协议状态机
typedef enum
{
  WAIT_FOR_MSG,
  IN_MSG
} ProcessingState;

//与单线程顺序服务器中的实现完全相同，详见sequential-server.c中的注释
void serve_connection(int sockfd)
{
  if (send(sockfd, "*", 1, 0) < 1)
  {
    perror_die("send");
  }

  ProcessingState state = WAIT_FOR_MSG;
  char filename[20] = "client";
  sprintf(filename, "%s%d", "client", sockfd);
  FILE *fp = fopen(filename, "w+");
  while (1)
  {
    uint8_t buf[1024];
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
          // putchar(buf[i]);
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

//每个线程
void *server_thread(void *arg)
{
  //接收创建时传递的参数，即套接字
  thread_config_t *config = (thread_config_t *)arg;
  int sockfd = config->sockfd;
  free(config);

  //获取线程id
  unsigned long id = (unsigned long)pthread_self();
  printf("Thread %lu created to handle connection with socket %d\n", id,
         sockfd);
  //接收客户端的文件内容并写入服务器端
  serve_connection(sockfd);
  printf("Thread %lu done\n", id);
  return 0;
}

int main(int argc, char **argv)
{
  setvbuf(stdout, NULL, _IONBF, 0);

  //默认在9090端口监听请求
  int portnum = 9090;
  if (argc >= 2)
  {
    portnum = atoi(argv[1]);
  }
  printf("Serving on port %d\n", portnum);
  fflush(stdout);

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

    pthread_t the_thread;

    //设置线程初始参数（即套接字），并传入线程执行函数中
    thread_config_t *config = (thread_config_t *)malloc(sizeof(*config));
    if (!config)
    {
      die("OOM");
    }
    config->sockfd = newsockfd;
    //创建新线程，将客户端的请求交给该线程执行
    pthread_create(&the_thread, NULL, server_thread, config);

    //线程执行完毕后，系统回收并销毁该线程
    pthread_detach(the_thread);
  }

  return 0;
}
