#include "protocol.h"
#include <string.h>
#include <memory>
#include <netinet/in.h>
#include "packet.h"
#include <iostream>
#include "cJSON.h"
#include <mosquitto.h>


void Protocol::frameParse()//把缓冲区数据解析为mqtt帧
{

	//解析出的每个完整帧发交给Dispatcher处理

    while (true)
    {
        // 1. 基础长度校验 (Header 2 bytes + Payload 8 bytes)
        if (recvBuffer.size() < 10) return;

        uint16_t len;
        memcpy(&len, recvBuffer.data(), 2);
        len = ntohs(len);

        // 2. 协议鲁棒性检查
        if (len != sizeof(SensorRawPacket)) {
            std::cerr << "Protocol Error: Expected " << sizeof(SensorRawPacket)
                << ", got " << len << ". Sliding buffer..." << std::endl;
            recvBuffer.erase(0, 1); // 逐字节滑动寻找下一个同步头，防止由于错位导致整条连接报废
            continue;
        }

        if (recvBuffer.size() < (len + 2)) return; // 等待数据包收全

        // 3. 指针转换与字节序处理
        SensorRawPacket* raw = (SensorRawPacket*)(recvBuffer.data() + 2);
        uint16_t t = ntohs(raw->rawTemp);
        uint16_t h = ntohs(raw->rawHumi);
        uint16_t id = raw->deviceId;
        uint16_t s = raw->statusCode;
        uint16_t recvSum = ntohs(raw->checksum);
        uint16_t calcSum = (uint16_t)(id + t + h + s);

        // 4. 校验和验证
        if (calcSum != recvSum) {
            std::cerr << "CheckSum Error! ID:" << id << " Calc:" << calcSum << " Recv:" << recvSum << std::endl;
            recvBuffer.erase(0, len + 2);
            continue;
        }

        // 5. 业务逻辑处理：二进制转 JSON
        // 注意：cJSON 此时使用的是你在 Reactor 中挂载的内存池
        cJSON* msg = cJSON_CreateObject();
        if (msg) {
            cJSON_AddNumberToObject(msg, "dev_id", id);
            cJSON_AddNumberToObject(msg, "temp", t / 100.0);
            cJSON_AddNumberToObject(msg, "humi", h / 100.0);
            cJSON_AddNumberToObject(msg, "gw_id", 1); // 增加网关标识

            char* msgStr = cJSON_PrintUnformatted(msg);
            if (msgStr) {
                struct mosquitto* mosq = reactor->getMosq();
                int rc = mosquitto_publish(mosq, NULL, "sensor/data", (int)strlen(msgStr), msgStr, 0, false);

                if (rc == MOSQ_ERR_SUCCESS) {
                    // 【核心修改】主动通知 Reactor 处理 MQTT 写事件
                    // 只有这样，被 publish 进队列的数据才会由 MqttHandler::handleWrite 真正发出
                    if (mosquitto_want_write(mosq)) {
                        int mosq_fd = mosquitto_socket(mosq);
                        if (mosq_fd != -1) {
                            reactor->update(mosq_fd, EPOLLIN | EPOLLOUT);
                        }
                    }
                }
                else {
                    std::cerr << "MQTT Publish failed: " << mosquitto_strerror(rc) << std::endl;
                }
                cJSON_free(msgStr); // 归还内存池
            }
            cJSON_Delete(msg); // 归还内存池
        }

        // 6. 成功处理，清理缓冲区
        recvBuffer.erase(0, len + 2);
    }
}