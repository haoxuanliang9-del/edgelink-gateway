#include "HandlerFactory.h"
#include "accepthandler.h"
#include "connectionhandler.h"
#include "mqtthandler.h"

EventHandler* HandlerFactory::creatHandler(Type type, Reactor* r, Protocol* p)
{
    EventHandler* handler = nullptr;
    switch (type)
    {
    case Type::Accept:
        handler = new AcceptHandler(r, p);
        break;
    case Type::Connection:
		handler = new ConnectionHandler(r, p);
        break;
    case Type::Mqtt:
		handler = new MqttHandler(r, p);
        break;
    default:
        break;
    }
    return handler;
}
