#include "reactor.h"



Reactor::Reactor(int s, struct mosquitto* m) : sockfd(s), mosq(m)
{
    initWheel(getWheel());
    efd = epoll_create(1);
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = sockfd;
    epoll_ctl(efd, EPOLL_CTL_ADD, sockfd, &ev);
    handler[s] = std::make_shared<AcceptHandler>(this);
}

Reactor::~Reactor()
{
    clearTimeWheel(getWheel());
}

void Reactor::loop() {
    while (1) {
        int nfds = epoll_wait(efd, events, MAX_EVENTS, -1);
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
    }
}


void Reactor::register_(int cfd, uint32_t mode)
{
    struct epoll_event ev;
    ev.events = mode | EPOLLET;
    ev.data.fd = cfd;
    epoll_ctl(efd, EPOLL_CTL_ADD, cfd, &ev);
    handler[cfd] = std::make_shared<ConnectionHandler>(this);
}


void Reactor::remove(int cfd)
{
    epoll_ctl(efd, EPOLL_CTL_DEL, cfd, NULL);
    handler.erase(cfd);
}

void Reactor::update(int cfd, uint32_t mode)
{
    struct epoll_event ev;
    // 关键：mode 是你想要的权限（如 EPOLLIN | EPOLLOUT），
    // 但必须重新加上 EPOLLET，因为 epoll_ctl(MOD) 会覆盖掉之前的设置
    ev.events = mode | EPOLLET;
    ev.data.fd = cfd;

    epoll_ctl(efd, EPOLL_CTL_MOD, cfd, &ev) == -1;
}

void Reactor::mqtt_heartbeat_cb(void* args)
{
    auto p = static_cast<MqttHandler*>(args);
    p->handleMisc();
    TimeWheelNode* node = addNewTimer(getWheel(), mqtt_heartbeat_cb, 60000, p);
    p->setTimer(node);
}

void AcceptHandler::handleRead(int fd)
{
    while (1)
    {
        struct sockaddr_in clientaddr;
        socklen_t len = sizeof(clientaddr);
        int clientfd = accept(reactor->getSockfd(), (struct sockaddr*)&clientaddr, &len);
        if (clientfd == -1)
            break;
        set_nonblocking(clientfd);
        reactor->register_(clientfd, EPOLLIN);
    }
}

void ConnectionHandler::handleRead(int fd)
{
    char tmp[BUFFER_SIZE];
    // 客户端数据可读（ET 模式：必须一次性读完）
    int count;
    while ((count = recv(fd, tmp, BUFFER_SIZE - 1, 0)) > 0)
    {
        recvBuffer.append(tmp, count);
    }
    // 对端断开链接
    if (count == 0)
    {
        close(fd);
        reactor->remove(fd);
    }
    else if (errno != EAGAIN && errno != EWOULDBLOCK)
    {
        close(fd);
        reactor->remove(fd);
    }
    //假设业务逻辑为：获取长度字段，回发对应长度的实际内容
    //若长度字段或内容不完整则留在缓冲区中
    processMessages(fd); // 在其中做相应的业务处理
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

void ConnectionHandler::processMessages(int fd)
{
    while (1)
    {
        if (recvBuffer.size() < 2)
            break;
        uint16_t len; // 使用uint16_t定义无符号16字节整型
        memcpy(&len, recvBuffer.data(), 2);//直接将二进制值拷贝到地址中，避免类型转换
        len = ntohs(len);//网络字节序转换为本地字节序

        if (recvBuffer.size() - 2 < len)
            break;

        if (len == 0 || len > 1024) // 限制最大值，防止内存耗尽攻击（DoS）
        {
            close(fd);
            reactor->remove(fd);
            return;
        }

        std::string prefix = recvBuffer.substr(2, len);//获取recvBuffer中从2开始的len个元素
        recvBuffer.erase(0, len + 2);//删除recvBuffer中从0开始的len+2个数据
        std::cout << prefix;

        sendBuffer.append(prefix);
    }
    handleWrite(fd);
}

void ConnectionHandler::mqttProcessMessages(int fd)
{
    while (true)
    {
        // 1. 检查长度字段是否完整 (2 字节)
        if (recvBuffer.size() < 2)
            break;

        uint16_t len;
        memcpy(&len, recvBuffer.data(), 2);
        len = ntohs(len); 

        // 2. 检查缓冲区是否已经包含完整的传感器数据包
        if (recvBuffer.size() - 2 < len)
            break;

        // 3. 核心：丢弃传感器数据内容
        // 我们已经知道了这个包的总长度是 len + 2，直接从缓冲区抹掉它
        recvBuffer.erase(0, len + 2);

        // 4. 获取 MQTT 句柄并发布 fd 编号
        struct mosquitto* mosq = reactor->getMosq();
        if (mosq)
        {
            // 将套接字编号 fd 转为字符串作为消息内容
            std::string payload = std::to_string(fd);
            
            // 发布到主题 "sensor"
            mosquitto_publish(mosq, NULL, "sensor", payload.length(), payload.c_str(), 0, false);
            
            // 测试用的日志打印
            // std::cout << "Test: Discarded data and sent sensor FD " << fd << " to Broker." << std::endl;
        }

        // 继续循环，防止一次 handleRead 收到多个包（ET 模式要求）
    }
}
//MQTT
void Reactor::mqttRegister(int fd, uint32_t mode, const std::shared_ptr<MqttHandler>& ptr, struct mosquitto* mosq)
{
    struct epoll_event ev;
    ev.events = mode | EPOLLET;
    ev.data.fd = fd;

    // 严谨起见，检查 epoll 操作返回值
    if (epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl add mqtt");
        return;
    }

    std::shared_ptr<MqttHandler> p = ptr;
    if (!p) {
        p = std::make_shared<MqttHandler>(this);
        p->setMosq(mosq); // 只有新创建时才设置，旧对象已经持有了
    }
    else
    {
        cancelTimer(p->getTimer());
    }
    TimeWheelNode* node = addNewTimer(getWheel(), mqtt_heartbeat_cb, 60000, p.get());
    p->setTimer(node);

    handler[fd] = p;

}



void MqttHandler::handleRead(int fd)
{
    // ET模式下循环读取，直到数据读尽
    while (true)
    {
        int rc = mosquitto_loop_read(mosq, 1);

        if (rc != MOSQ_ERR_SUCCESS)
        {
            if (rc == MOSQ_ERR_CONN_LOST || rc == MOSQ_ERR_NO_CONN) {
                printf("MQTT Connection lost. Triggering reconnect logic...\n");

                // 1. 先从 Reactor 移除旧的 fd
                reactor->remove(fd);
                close(fd);

                // 2. 尝试重连 (libmosquitto 会自动创建新 Socket)
                if (mosquitto_reconnect_async(mosq) == MOSQ_ERR_SUCCESS) {
                    int new_fd = mosquitto_socket(mosq);
                    // 3. 将新 fd 重新注册回 Reactor
                    // 注意：这里的 register_ 需要能支持关联当前的 MqttHandler 对象
                    reactor->mqttRegister(new_fd,EPOLLOUT|EPOLLIN ,shared_from_this(),nullptr);
                }
                break;
            }
            else {
                // 处理其他致命错误
                printf("Fatal MQTT error: %s\n", mosquitto_strerror(rc));
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
    while (mosquitto_want_write(mosq))//消息队列非空
    {
        int rc = mosquitto_loop_write(mosq, 1);//每次发一包

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
            else if (rc == MOSQ_ERR_ERRNO && (errno == EAGAIN || errno == EWOULDBLOCK))//缓冲区已满
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

    //消息队已空
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
        int nfds = epoll_wait(efd, events, MAX_EVENTS, 100);
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