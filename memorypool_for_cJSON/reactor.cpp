#include "reactor.h"


void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}


Reactor::Reactor(int s, struct mosquitto* m) : sockfd(s), mosq(m)
{
    globalMemoryPool = new MemoryPool(65536, 512, 16);
    initWheel(getWheel());
    hooks.malloc_fn = myMalloc;
    hooks.free_fn = myFree;
	cJSON_InitHooks(&hooks);

    efd = epoll_create(1);
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = sockfd;
    epoll_ctl(efd, EPOLL_CTL_ADD, sockfd, &ev);
    handler[s] = std::make_shared<AcceptHandler>(this);
}

Reactor::~Reactor()
{
    clearTimeWheel(getWheel());
    delete globalMemoryPool;
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

    epoll_ctl(efd, EPOLL_CTL_MOD, cfd, &ev);
}

void Reactor::mqtt_heartbeat_cb(void* args)
{
    auto p = static_cast<MqttHandler*>(args);
    p->handleMisc();
    TimeWheelNode* node = addNewTimer(getWheel(), mqtt_heartbeat_cb, 60000, p);
    p->setTimer(node);
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



