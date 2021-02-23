//I/O多路复用技术（事件驱动编程）：select服务器
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils.h"

//select可以监听的fd数量上限就是1024,这也是我为什么要选用epoll的原因之一
#define MAXFDS 1000

//协议状态机
typedef enum
{
  INITIAL_ACK,
  WAIT_FOR_MSG,
  IN_MSG
} ProcessingState;

#define SENDBUF_SIZE 1024

typedef struct
{
  //协议状态机
  ProcessingState state;
  // sendbuf中存放了客户端发送给服务器的信息
  uint8_t sendbuf[SENDBUF_SIZE];
  int sendbuf_end;
  int sendptr;
} peer_state_t;

//连接状态
peer_state_t global_state[MAXFDS];

//当want_read是true时，说明fd待读取
//当want_write是true时，说明fd待写入
//若两个都是false，则该fd应该被释放
typedef struct
{
  bool want_read;
  bool want_write;
} fd_status_t;

// 设置监听集合参数
const fd_status_t fd_status_R = {.want_read = true, .want_write = false};
const fd_status_t fd_status_W = {.want_read = false, .want_write = true};
const fd_status_t fd_status_RW = {.want_read = true, .want_write = true};
const fd_status_t fd_status_NORW = {.want_read = false, .want_write = false};

fd_status_t on_peer_connected(int sockfd, const struct sockaddr_in *peer_addr,
                              socklen_t peer_addr_len)
{
  report_peer_connected(peer_addr, peer_addr_len);

  // 初始化fd的状态
  peer_state_t *peerstate = &global_state[sockfd];
  peerstate->state = INITIAL_ACK;
  peerstate->sendbuf[0] = '*';
  peerstate->sendptr = 0;
  peerstate->sendbuf_end = 1;

  return fd_status_W;
}

fd_status_t on_peer_ready_recv(int sockfd)
{
  peer_state_t *peerstate = &global_state[sockfd];

  if (peerstate->state == INITIAL_ACK ||
      peerstate->sendptr < peerstate->sendbuf_end)
  {
    //若状态为刚初始化，则没有可以接收的，直接返回
    return fd_status_W;
  }

  uint8_t buf[1024];
  int nbytes = recv(sockfd, buf, sizeof buf, 0);
  if (nbytes == 0)
  {
    //若没有可以接收的数据，则返回
    return fd_status_NORW;
  }
  else if (nbytes < 0)
  {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      return fd_status_R;
    }
    else
    {
      perror_die("recv");
    }
  }
  //接受客户端发送的数据，并写入服务器端文件中
  bool ready_to_send = false;
  char filename[20] = "client";
  sprintf(filename, "%s%d", "client", sockfd);
  FILE *fp = fopen(filename, "a+");
  //循环遍历客户端发送的文件的所有数据
  for (int i = 0; i < nbytes; ++i)
  {
    //根据协议状态机执行操作
    switch (peerstate->state)
    {
    case INITIAL_ACK:
      assert(0 && "can't reach here");
      break;
    //若是等待信息且收到^，则开始接收信息
    case WAIT_FOR_MSG:
      if (buf[i] == '^')
      {
        peerstate->state = IN_MSG;
      }
      break;
    //若在接收信息而收到了$，则停止接受信息
    case IN_MSG:
      if (buf[i] == '$')
      {
        peerstate->state = WAIT_FOR_MSG;
      }
      //否则，写入文件，用标准I/O
      else
      {
        assert(peerstate->sendbuf_end < SENDBUF_SIZE);
        fputc(buf[i], fp);
        peerstate->sendbuf[peerstate->sendbuf_end++] = buf[i] + 1;
        ready_to_send = true;
      }
      break;
    }
  }
  fclose(fp);

  return (fd_status_t){.want_read = !ready_to_send,
                       .want_write = ready_to_send};
}

fd_status_t on_peer_ready_send(int sockfd)
{
  peer_state_t *peerstate = &global_state[sockfd];

  if (peerstate->sendptr >= peerstate->sendbuf_end)
  {
    // 没东西等待发送，直接返回
    return fd_status_RW;
  }
  //回复客户端
  int sendlen = peerstate->sendbuf_end - peerstate->sendptr;
  int nsent = send(sockfd, &peerstate->sendbuf[peerstate->sendptr], sendlen, 0);
  if (nsent == -1)
  {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      return fd_status_W;
    }
    else
    {
      perror_die("send");
    }
  }
  if (nsent < sendlen)
  {
    peerstate->sendptr += nsent;
    return fd_status_W;
  }
  else
  {
    // 成功发送，则重置状态值
    peerstate->sendptr = 0;
    peerstate->sendbuf_end = 0;

    if (peerstate->state == INITIAL_ACK)
    {
      peerstate->state = WAIT_FOR_MSG;
    }

    return fd_status_R;
  }
}

int main(int argc, char **argv)
{
  setvbuf(stdout, NULL, _IONBF, 0);

  //默认在9090端口监听
  int portnum = 9090;
  if (argc >= 2)
  {
    portnum = atoi(argv[1]);
  }
  printf("Serving on port %d\n", portnum);

  //初始化socket，绑定并监听
  int listener_sockfd = listen_inet_socket(portnum);

  //select函数可能返回不可读的fd，因此设置nonblock模式可以避免永远阻塞
  make_socket_non_blocking(listener_sockfd);

  //限定fd数，select的上限是1024,在这里我设置为1000
  if (listener_sockfd >= FD_SETSIZE)
  {
    die("listener socket fd (%d) >= FD_SETSIZE (%d)", listener_sockfd,
        FD_SETSIZE);
  }

  //设置select函数要监听的fd集合
  fd_set readfds_master;
  FD_ZERO(&readfds_master);
  fd_set writefds_master;
  FD_ZERO(&writefds_master);
  FD_SET(listener_sockfd, &readfds_master);

  //设置每次遍历的数量
  int fdset_max = listener_sockfd;

  while (1)
  {
    //select函数有一个副作用，就是会设置已读集合（readfds），所以我们要设置一个备份，在下一次循环时修改回来
    fd_set readfds = readfds_master;
    fd_set writefds = writefds_master;

    //阻塞，等待客户端请求的到来
    int nready = select(fdset_max + 1, &readfds, &writefds, NULL, NULL);
    if (nready < 0)
    {
      perror_die("select");
    }

    //遍历fd集合，看看是哪个可读/可写了
    for (int fd = 0; fd <= fdset_max && nready > 0; fd++)
    {
      // 如果fd可读
      if (FD_ISSET(fd, &readfds))
      {
        //准备好-1,可以减少循环的次数，优化性能
        nready--;

        if (fd == listener_sockfd)
        {
          //监听fd已经准备好
          struct sockaddr_in peer_addr;
          socklen_t peer_addr_len = sizeof(peer_addr);
          //接收客户端发送的文件数据
          int newsockfd = accept(listener_sockfd, (struct sockaddr *)&peer_addr,
                                 &peer_addr_len);
          if (newsockfd < 0)
          {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
              printf("accept returned EAGAIN or EWOULDBLOCK\n");
            }
            else
            {
              perror_die("accept");
            }
          }
          else
          {
            //设置fd为非阻塞模式，防止永久阻塞
            make_socket_non_blocking(newsockfd);
            //保证fd数量不超过限制
            if (newsockfd > fdset_max)
            {
              if (newsockfd >= FD_SETSIZE)
              {
                die("socket fd (%d) >= FD_SETSIZE (%d)", newsockfd, FD_SETSIZE);
              }
              fdset_max = newsockfd;
            }

            //初始化fd状态
            fd_status_t status =
                on_peer_connected(newsockfd, &peer_addr, peer_addr_len);
            //若fd等待读取，则将其将如待读取的集合，用select监听其读取I/O是否准备好
            if (status.want_read)
            {
              FD_SET(newsockfd, &readfds_master);
            }
            else
            {
              FD_CLR(newsockfd, &readfds_master);
            }
            //若fd等待写入，则将其将如待写入的集合，用select监听其写入I/O是否准备好
            if (status.want_write)
            {
              FD_SET(newsockfd, &writefds_master);
            }
            else
            {
              FD_CLR(newsockfd, &writefds_master);
            }
          }
        }
        //如果不是监听描述符已经准备好，而是读集合中的其他描述符准备好
        else
        {
          //接收信息，设置并获得fd最新的状态（待读取/待写入等）
          fd_status_t status = on_peer_ready_recv(fd);
          //若fd等待读取，则将其将如待读取的集合，用select监听其读取I/O是否准备好
          if (status.want_read)
          {
            FD_SET(fd, &readfds_master);
          }
          else
          {
            FD_CLR(fd, &readfds_master);
          }
          //若fd等待写入，则将其将如待写入的集合，用select监听其写入I/O是否准备好
          if (status.want_write)
          {
            FD_SET(fd, &writefds_master);
          }
          else
          {
            FD_CLR(fd, &writefds_master);
          }
          //如果此时还是既不等待读取，也不等待写入，则关闭该fd
          if (!status.want_read && !status.want_write)
          {
            printf("socket %d closing\n", fd);
            close(fd);
          }
        }
      }

      //如果fd可写
      if (FD_ISSET(fd, &writefds))
      {
        //准备好-1,可以减少循环的次数，优化性能
        nready--;
        //发送信息，设置并获得fd最新的状态（待读取/待写入等）
        fd_status_t status = on_peer_ready_send(fd);
        //若fd等待读取，则将其将如待读取的集合，用select监听其读取I/O是否准备好
        if (status.want_read)
        {
          FD_SET(fd, &readfds_master);
        }
        else
        {
          FD_CLR(fd, &readfds_master);
        }
        //若fd等待写入，则将其将如待写入的集合，用select监听其写入I/O是否准备好
        if (status.want_write)
        {
          FD_SET(fd, &writefds_master);
        }
        else
        {
          FD_CLR(fd, &writefds_master);
        }
        //如果此时还是既不等待读取，也不等待写入，则关闭该fd
        if (!status.want_read && !status.want_write)
        {
          printf("socket %d closing\n", fd);
          close(fd);
        }
      }
    }
  }

  return 0;
}
