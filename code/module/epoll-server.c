//I/O多路复用技术（事件驱动编程）：epoll服务器
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils.h"

//监听的fd数量上限
#define MAXFDS 16 * 1024

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
  assert(sockfd < MAXFDS);
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
  assert(sockfd < MAXFDS);
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
  // Report reading readiness iff there's nothing to send to the peer as a
  // result of the latest recv.
  return (fd_status_t){.want_read = !ready_to_send,
                       .want_write = ready_to_send};
}

fd_status_t on_peer_ready_send(int sockfd)
{
  assert(sockfd < MAXFDS);
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

int main(int argc, const char **argv)
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

  //epoll函数可能返回不可读的fd，因此设置nonblock模式可以避免永远阻塞
  make_socket_non_blocking(listener_sockfd);

  int epollfd = epoll_create1(0);
  if (epollfd < 0)
  {
    perror_die("epoll_create1");
  }

  //设置epoll等待的事件，最初时只有监听描述符
  struct epoll_event accept_event;
  accept_event.data.fd = listener_sockfd;
  accept_event.events = EPOLLIN;
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listener_sockfd, &accept_event) < 0)
  {
    perror_die("epoll_ctl EPOLL_CTL_ADD");
  }

  //分配一个就绪事件缓冲区，以便于传递给epoll进行修改
  struct epoll_event *events = calloc(MAXFDS, sizeof(struct epoll_event));
  if (events == NULL)
  {
    die("Unable to allocate memory for epoll_events");
  }

  //开启服务器循环
  while (1)
  {
    //使用epoll_wait等待至少有一个事件准备好
    int nready = epoll_wait(epollfd, events, MAXFDS, -1);
    //遍历所有准备好的事件，准备好的事件数量为nready，存放在之前分配的event中
    for (int i = 0; i < nready; i++)
    {
      if (events[i].events & EPOLLERR)
      {
        perror_die("epoll_wait returned EPOLLERR");
      }

      //监听描述符已经准备好，则说明有客户端发送请求
      if (events[i].data.fd == listener_sockfd)
      {
        struct sockaddr_in peer_addr;
        socklen_t peer_addr_len = sizeof(peer_addr);
        //使用accept函数接收客户端发送的请求
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
          if (newsockfd >= MAXFDS)
          {
            die("socket fd (%d) >= MAXFDS (%d)", newsockfd, MAXFDS);
          }

          //初始化fd的服务器内部状态
          fd_status_t status =
              on_peer_connected(newsockfd, &peer_addr, peer_addr_len);
          struct epoll_event event = {0};
          //将新的fd加入epoll监听的event集合
          event.data.fd = newsockfd;
          //若fd等待读取，则监听EPOLLIN事件
          if (status.want_read)
          {
            event.events |= EPOLLIN;
          }
          //若fd等待写入，则监听EPOLLOUT事件
          if (status.want_write)
          {
            event.events |= EPOLLOUT;
          }
          //将新的fd加入epoll监听的event集合
          if (epoll_ctl(epollfd, EPOLL_CTL_ADD, newsockfd, &event) < 0)
          {
            perror_die("epoll_ctl EPOLL_CTL_ADD");
          }
        }
      }
      //如果不是监听描述符已经准备好，而是其他描述符准备好
      else
      {
        //如果是读取准备好
        if (events[i].events & EPOLLIN)
        {
          //获得描述符
          int fd = events[i].data.fd;
          //接收信息，设置并获得fd最新的状态（待读取/待写入等）
          fd_status_t status = on_peer_ready_recv(fd);
          //重置event中的监听内容
          struct epoll_event event = {0};
          event.data.fd = fd;
          //若fd等待读取，则监听EPOLLIN事件
          if (status.want_read)
          {
            event.events |= EPOLLIN;
          }
          //若fd等待写入，则监听EPOLLOUT事件
          if (status.want_write)
          {
            event.events |= EPOLLOUT;
          }
          //若已完成，则关闭描述符
          if (event.events == 0)
          {
            printf("socket %d closing\n", fd);
            if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) < 0)
            {
              perror_die("epoll_ctl EPOLL_CTL_DEL");
            }
            close(fd);
          }
          else if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event) < 0)
          {
            perror_die("epoll_ctl EPOLL_CTL_MOD");
          }
        }
        //如果是写入准备好
        else if (events[i].events & EPOLLOUT)
        {
          //获得描述符
          int fd = events[i].data.fd;
          //发送信息，设置并获得fd最新的状态（待读取/待写入等）
          fd_status_t status = on_peer_ready_send(fd);
          //重置event中的监听内容
          struct epoll_event event = {0};
          event.data.fd = fd;

          //若fd等待读取，则监听EPOLLIN事件
          if (status.want_read)
          {
            event.events |= EPOLLIN;
          }
          //若fd等待写入，则监听EPOLLOUT事件
          if (status.want_write)
          {
            event.events |= EPOLLOUT;
          }
          //若已完成，则关闭描述符
          if (event.events == 0)
          {
            printf("socket %d closing\n", fd);
            if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) < 0)
            {
              perror_die("epoll_ctl EPOLL_CTL_DEL");
            }
            close(fd);
          }
          else if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event) < 0)
          {
            perror_die("epoll_ctl EPOLL_CTL_MOD");
          }
        }
      }
    }
  }

  return 0;
}
