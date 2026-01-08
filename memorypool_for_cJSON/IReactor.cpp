#include "IReactor.h"




void IReactor::loop() {
    while (1) {
        int nfds = epoll_wait(efd, events, MAX_EVENTS, -1);
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            uint32_t revents = events[i].events;

            // 1. 检查 handler 是否存在（防止之前的循环已经将其删除）
            if (handler.find(fd) == handler.end()) continue;

            // 2. 处理读
            if (revents & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
                handler[fd]->handleRead(fd);//返回结果：读取出错、帧解析出错、业务层解析出错、成功
            }

            // 3. 再次检查，handleRead 可能触发了 remove
            if (handler.find(fd) != handler.end() && (revents & EPOLLOUT)) {
                handler[fd]->handleWrite(fd);
            }

            // 4. 处理错误
            if (handler.find(fd) != handler.end() && (revents & (EPOLLERR | EPOLLHUP))) {
                close(fd);
                remove(fd);
            }
        }
    }
}