#pragma once
#include "eventhandler.h"
#include <memory>

// 前向声明，避免不必要的头文件包含导致的循环依赖
struct TimeWheelNode;

class MqttHandler : public EventHandler, public std::enable_shared_from_this<MqttHandler>
{
private:
    TimeWheelNode* timer;
    struct mosquitto* mosq;
public:
    void handleRead(int fd);
    void handleWrite(int fd);
    void handleMisc();
    explicit MqttHandler(Reactor* r, Protocol* p) : EventHandler(r, p), timer(nullptr), mosq(nullptr) {}
    ~MqttHandler();
    void setMosq(struct mosquitto* m);

    void setTimer(TimeWheelNode* t) { timer = t; }
    TimeWheelNode* getTimer() { return timer; }
};

