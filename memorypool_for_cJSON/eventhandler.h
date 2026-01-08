#pragma once

class Reactor;
class Protocol;

class EventHandler
{
public:
	Protocol* protocol;
    Reactor* reactor;
    virtual ~EventHandler() = default;
    virtual void handleRead(int fd) = 0;
    virtual void handleWrite(int fd) = 0;
    explicit EventHandler(Reactor* r, Protocol* p) :reactor(r),protocol(p) {}
};