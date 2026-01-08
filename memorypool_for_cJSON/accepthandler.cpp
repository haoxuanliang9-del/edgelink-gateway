#include "accepthandler.h"
#include "reactor.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <cerrno>
#include <cstdio>

void AcceptHandler::handleRead(int fd)
{
    while (1)
    {
        struct sockaddr_in clientaddr;
        socklen_t len = sizeof(clientaddr);
        int clientfd = accept(reactor->getSockfd(), (struct sockaddr*)&clientaddr, &len);
        if (clientfd == -1)
        {
            if (clientfd == -1)
            {
                // 关键：区分 "无新连接" 和 "真错误"
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; // 正常：当前无新连接，ET模式下退出
                }
                else {
                    perror("accept error");
                    break; // 真错误：打印日志并退出
                }
            }
        }
        set_nonblocking(clientfd);
        reactor->register_(clientfd, EPOLLIN);
    }
}