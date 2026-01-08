#pragma once
#include <sys/epoll.h>
#include <unordered_map>
#include <memory>//智能指针
#include "eventhandler.h"
#include "timewheel.h"

#define MAX_EVENTS 1024
#define BUFFER_SIZE 64


class IReactor
{
private:
    int sockfd;//监听套接字
    int efd;//epoll描述符
    epoll_event ev, events[MAX_EVENTS];//epoll事件队列
    std::unordered_map<int, std::shared_ptr<EventHandler>> handler;//连接处理器哈希表
    
public:
    explicit IReactor(int s, struct mosquitto* m);
    ~IReactor();

    virtual void loop();//主事件循环
    virtual void register_(int cfd, uint32_t mode);//注册套接字到epoll
    void remove(int cfd);//关闭套接字
    void update(int cfd, uint32_t mode);//更新epoll监控套接字

    int getSockfd() { return sockfd; }
    static Wheel* getWheel() { static Wheel wheel; return &wheel; }//获取时间轮

};