#pragma once

#include <iostream>
#include <unordered_map>
#include <memory>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <string>
#include <string.h>
#include "timewheel.h"
#include "memorypool.h"
#include "mqtthandler.h"
#include "accepthandler.h"
#include "connectionhandler.h"

#define MAX_EVENTS 1024
#define BUFFER_SIZE 64
// 设置 fd 为非阻塞（ET 模式必需）



class Reactor
{
private:
    int sockfd;
    struct mosquitto* mosq;
    int efd;
    epoll_event ev, events[MAX_EVENTS];
    std::unordered_map<int, std::shared_ptr<EventHandler>> handler;
	
    cJSON_Hooks hooks;

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

void set_nonblocking(int fd);
