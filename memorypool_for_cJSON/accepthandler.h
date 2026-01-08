#pragma once
#include "eventhandler.h"



class AcceptHandler : public EventHandler
{
public:
    void handleRead(int fd) override;//加上override，编译器才会检查。
    explicit AcceptHandler(Reactor* r, Protocol* p) : EventHandler(r, p) {}
    void handleWrite(int fd) override {}
};