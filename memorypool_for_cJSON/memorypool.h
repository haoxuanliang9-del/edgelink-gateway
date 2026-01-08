#ifndef MEMORYPOOL_H
#define MEMORYPOOL_H

#include <iostream>
#include <vector>
#include <atomic>
#include <pthread.h>
#include <algorithm>
#include <cstdint>
class MemoryPool
{

public:
    explicit MemoryPool(size_t pS, size_t bS, size_t aS);
    ~MemoryPool();

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    // 分配小块内存
    void* allocate(size_t size);

    // 回收小块内存
    void deallocate(void* ptr);


    size_t getMallocCount() { return mallocCount.load(std::memory_order_relaxed); }
    size_t getFreeCount() { return freeCount.load(std::memory_order_relaxed); }

private:
    void insert2List(void*);

    // 申请大块内存、切分、挂载
    void expand();

private:
    struct Node
    {
        Node* next;
        Node() { next = nullptr; }
    };

    std::vector<void*> pages; // 存放页地址
    uintptr_t startAddr = UINTPTR_MAX;
    uintptr_t endAddr = 0;

    Node* blockList; // 挂载小块地址

    size_t pageSize;
    size_t blockSize;
    size_t alignSize;
    size_t usablePageSize;


    std::atomic<size_t> mallocCount{ 0 };
    std::atomic<size_t> freeCount{ 0 };

    pthread_mutex_t mutex;
};


extern MemoryPool* globalMemoryPool;
#ifdef __cplusplus
extern "C" {
#endif

    void* myMalloc(size_t size);
    void myFree(void* ptr);

#ifdef __cplusplus
}
#endif

#endif // MEMORYPOOL_H