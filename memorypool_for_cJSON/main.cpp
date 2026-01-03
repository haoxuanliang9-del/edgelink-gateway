#include "reactor.h"
#include "cJSON.h"
#include "memorypool.h"
#include <iostream>


void on_message(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* msg)
{
    // 打印主题（topic 不会为 NULL，MQTT 协议保证）
    printf("Topic: %s\n", msg->topic);

    // 安全打印 payload（可能为空或包含二进制数据）
    if (msg->payloadlen > 0 && msg->payload != NULL) {
        // 假设是文本（如 JSON、字符串），用 %.*s 精确控制长度
        printf("Payload: %.*s\n", msg->payloadlen, (char*)msg->payload);
    }
    else {
        printf("Payload: (empty)\n");
    }

    // 打印 QoS 和 retain 标志
    printf("QoS: %d, Retained: %s\n", msg->qos, msg->retain ? "true" : "false");

    // 可选：打印 mid（对 QoS=0 通常无意义）
    if (msg->qos > 0) {
        printf("Message ID: %d\n", msg->mid);
    }

    printf("------------------------\n");
}

int main() {
    // 1. 初始化 MQTT
    mosquitto_lib_init();
    // 將 reactor 指針預留，以便回調函數使用
    struct mosquitto* mosq = mosquitto_new("gateway_client", true, nullptr);

    mosquitto_message_callback_set(mosq, on_message);

    // 2. 異步連接 (這不會阻塞，但會初始化內部數據結構)
    if (mosquitto_connect(mosq, "127.0.0.1", 1883, 60) != MOSQ_ERR_SUCCESS) {
        perror("MQTT connect failed");
        return -1;
    }
    mosquitto_subscribe(mosq, NULL, "opi/sensor", 0);

    // 3. 初始化本地 TCP 監聽
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serveraddr {};
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(2048);
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("Bind failed");
        return -1;
    }
    listen(sockfd, 128);
    set_nonblocking(sockfd);
   

    // 獲取 MQTT 的 Socket 並註冊到 Epoll
    int mosqfd = mosquitto_socket(mosq);
    Reactor reactor(sockfd, mosq);
    if (mosqfd != -1) {
        // 將 mosq 實例傳入，以便 Handler 內部調用
        reactor.mqttRegister(mosqfd, EPOLLIN | EPOLLOUT, nullptr, mosq);
    }

    std::cout << "Gateway is running... Listening on port 2048" << std::endl;

    // 5. 進入統一的事件循環
    // 注意：確保你的 mqttLoop 內部調用了 expireTimer
    reactor.mqttLoop();

    return 0;
}




