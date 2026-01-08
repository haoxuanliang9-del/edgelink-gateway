#include "mqtthandler.h"
#include "reactor.h"      // 获取 Reactor 完整定义、epoll 常量和 timewheel
#include <cstdio>
#include <cerrno>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <cstring>

// MqttHandler 成员实现
void MqttHandler::handleRead(int fd)
{
    // ET模式下循环读取，直到数据读尽
    while (true)
    {
        int rc = mosquitto_loop_read(mosq, 1);

        if (rc != MOSQ_ERR_SUCCESS)
        {
            if (rc == MOSQ_ERR_CONN_LOST || rc == MOSQ_ERR_NO_CONN) {
                std::printf("MQTT Connection lost. Triggering reconnect logic...\n");

                // 1. 先从 Reactor 移除旧的 fd
                reactor->remove(fd);
                close(fd);

                // 2. 尝试重连 (libmosquitto 会自动创建新 Socket)
                if (mosquitto_reconnect_async(mosq) == MOSQ_ERR_SUCCESS) {
                    int new_fd = mosquitto_socket(mosq);
                    // 3. 将新 fd 重新注册回 Reactor（Reactor 的定义此处可见）
                    reactor->mqttRegister(new_fd, EPOLLOUT | EPOLLIN, shared_from_this(), nullptr);
                }
                break;
            }
            else {
                // 处理其他致命错误
                std::printf("Fatal MQTT error: %s\n", mosquitto_strerror(rc));
                reactor->remove(fd);
                // mosquitto_destroy 建议放在析构函数里，或者确保不再使用该对象
                break;
            }
        }
        char dummy;
        ssize_t s = recv(fd, &dummy, 1, MSG_PEEK | MSG_DONTWAIT);
        if (s <= 0) {
            // s == 0 表示对端关闭，s == -1 且为 EAGAIN 表示读尽
            break;
        }
    }
}

void MqttHandler::handleWrite(int fd)
{
    while (mosquitto_want_write(mosq)) // 消息队列非空
    {
        int rc = mosquitto_loop_write(mosq, 1); // 每次发一包

        if (rc != MOSQ_ERR_SUCCESS)
        {
            if (rc == MOSQ_ERR_CONN_LOST || rc == MOSQ_ERR_NO_CONN)
            {
                reactor->remove(fd);
                close(fd);
                if (mosquitto_reconnect_async(mosq) == MOSQ_ERR_SUCCESS)
                {
                    int new_fd = mosquitto_socket(mosq);
                    reactor->mqttRegister(new_fd, EPOLLIN | EPOLLOUT, shared_from_this(), nullptr);
                }
                return;
            }
            else if (rc == MOSQ_ERR_ERRNO && (errno == EAGAIN || errno == EWOULDBLOCK)) // 缓冲区已满
            {
                reactor->update(fd, EPOLLIN | EPOLLOUT);
                return;
            }
            else
            {
                reactor->remove(fd);
                return;
            }
        }
    }

    // 消息队已空
    reactor->update(fd, EPOLLIN);
}

void MqttHandler::handleMisc()
{
    if (!mosq) return;

    // 内部会处理心跳包的发送和异步重连的状态维护
    int rc = mosquitto_loop_misc(mosq);

    // 如果 misc 发现连接彻底没救了，也可以在这里触发 reconnect
    if (rc == MOSQ_ERR_NO_CONN) {
        mosquitto_reconnect_async(mosq);
    }
}

MqttHandler::~MqttHandler()
{
    if (timer) {
        cancelTimer(timer); // 将 active 设为 false，防止回调野指针
    }
    if (mosq) {
        mosquitto_destroy(mosq);
    }
}

void MqttHandler::setMosq(mosquitto* m)
{
    mosq = m;
}
void Reactor::mqttLoop()
{
    while (1) {
        int nfds = epoll_wait(efd, events, MAX_EVENTS, 10);
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            uint32_t revents = events[i].events;

            // 1. 检查 handler 是否存在（防止之前的循环已经将其删除）
            if (handler.find(fd) == handler.end()) continue;

            // 2. 处理读
            if (revents & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
                handler[fd]->handleRead(fd);
            }

            // 3. 再次检查，handleRead 可能触发了 remove
            if (handler.find(fd) != handler.end() && (revents & EPOLLOUT)) {
                handler[fd]->handleWrite(fd);
            }

            // 4. 处理错误
            if (handler.find(fd) != handler.end() && (revents & (EPOLLERR | EPOLLHUP))) {
                close(fd);
                remove(fd);
            }
        }

        expireTimer(getWheel());
    }
}