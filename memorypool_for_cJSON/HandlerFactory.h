#pragma once
#include "protocol.h"
#include "eventhandler.h"
enum class Type:char{Accept,Connection,Mqtt};
class HandlerFactory
{
private:
	Protocol* protocol;
public:
	EventHandler* creatHandler(Type type,Reactor *r, Protocol* p);
};

