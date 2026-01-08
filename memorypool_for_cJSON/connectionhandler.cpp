#include "connectionhandler.h"
#include "reactor.h"

#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <iostream>
#include <cstring>
#include <arpa/inet.h>

void ConnectionHandler::handleRead(int fd)//只负责读数据到缓冲区，不做协议解析
{
    char tmp[BUFFER_SIZE];
    int count;
    while ((count = recv(fd, tmp, BUFFER_SIZE - 1, 0)) > 0)
    {
        // 核心保护 1：如果单个连接积压超过 1KB 数据还没被解析，认为协议出错
        if (recvBuffer.size() > 10240) {
            std::cerr << "Flood protection: fd " << fd << " exceeded buffer limit. Closing." << std::endl;
            reactor->remove(fd);
            close(fd);
            return;
        }
        recvBuffer.append(tmp, count);
    }

    if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
    {
        reactor->remove(fd);
        close(fd);
        return;
    }

    mqttProcessMessages(fd);
}

void ConnectionHandler::handleWrite(int fd)
{
    ssize_t count = 0;
    while (sendBuffer.size() > 0)
    {
        count = send(fd, sendBuffer.data(), sendBuffer.size(), 0);
        if (count > 0)
        {
            sendBuffer.erase(0, count);
        }
        else
        {
            // 此时 count 可能是 0 (对端关闭) 或 -1 (出错或 EAGAIN)
            break;
        }
    }

    // 判断“发完”的最标准做法是看缓冲区是否为空
    if (sendBuffer.empty())
    {
        reactor->update(fd, EPOLLIN); // 发完了，取消写监控
    }
    else
    {
        // 没发完，判断是因为满了还是出错了
        if (count == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            // 内核缓冲区满，确保监控了写事件
            reactor->update(fd, EPOLLIN | EPOLLOUT);
        }
        else
        {
            // count == 0 或者真正的 errno 错误
            close(fd);
            reactor->remove(fd);
        }
    }
}

