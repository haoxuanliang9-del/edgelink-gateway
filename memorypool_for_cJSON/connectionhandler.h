#pragma once
#include <string>
#include "eventhandler.h"



class ConnectionHandler : public EventHandler
{
private:
    std::string recvBuffer;
    std::string sendBuffer;

public:
    void handleRead(int fd) override;
    void handleWrite(int fd) override;
    explicit ConnectionHandler(Reactor* r, Protocol* p) : EventHandler(r,p) {}


};
