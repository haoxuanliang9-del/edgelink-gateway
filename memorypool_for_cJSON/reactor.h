#include <iostream>
#include <unordered_map>
#include <memory>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <string>
#include <string.h>
#include <mosquitto.h>
#include "timewheel.h"


#define MAX_EVENTS 1024
#define BUFFER_SIZE 64
// 设置 fd 为非阻塞（ET 模式必需）
void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

class Reactor;
class EventHandler
{
public:
    Reactor* reactor;
    virtual ~EventHandler() = default;
    virtual void handleRead(int fd) = 0;
    virtual void handleWrite(int fd) = 0;
    explicit EventHandler(Reactor* r) :reactor(r) {}
};


class AcceptHandler : public EventHandler
{
public:
    void handleRead(int fd) override;//加上override，编译器才会检查。
    explicit AcceptHandler(Reactor* r) :EventHandler(r) {}
};

class ConnectionHandler : public EventHandler
{
private:
    std::string recvBuffer;
    std::string sendBuffer;
public:
    void handleRead(int fd) override;
    void handleWrite(int fd) override;
    explicit ConnectionHandler(Reactor* r) : EventHandler(r) {}
    void processMessages(int fd);

    void mqttProcessMessages(int fd);

};

class MqttHandler : public EventHandler, public std::enable_shared_from_this<MqttHandler>
{
private:
    TimeWheelNode* timer;
    struct mosquitto* mosq;
public:
    void handleRead(int fd);
    void handleWrite(int fd);
    void handleMisc();
    explicit MqttHandler(Reactor* r) :EventHandler(r) ,mosq(nullptr),timer(nullptr){ }
    ~MqttHandler();
    void setMosq(struct mosquitto* m);

    void setTimer(TimeWheelNode* t) { timer = t; }
    TimeWheelNode* getTimer() { return timer; }
};


class Reactor
{
private:
    struct mosquitto* mosq;
    int sockfd;
    int efd;
    epoll_event ev, events[MAX_EVENTS];
    std::unordered_map<int, std::shared_ptr<EventHandler>> handler;

public:
    explicit Reactor(int s, struct mosquitto* m);
    ~Reactor();

    void loop();
    void mqttLoop();
    void register_(int cfd, uint32_t mode);
    void mqttRegister(int fd, uint32_t mode, const std::shared_ptr<MqttHandler>& ptr, struct mosquitto* mosq);
    void remove(int cfd);
    void update(int cfd, uint32_t mode);
    struct mosquitto* getMosq() { return mosq; }

    int getSockfd() { return sockfd; }
    static Wheel* getWheel() { static Wheel wheel; return &wheel; }

    static void mqtt_heartbeat_cb(void* args);
};

