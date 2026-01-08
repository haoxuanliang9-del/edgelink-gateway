#include "memorypool.h"
#include <stdlib.h>


MemoryPool* globalMemoryPool = nullptr;
extern "C"
{
    void* myMalloc(size_t size)
    {
        if (!globalMemoryPool) return malloc(size);

        return globalMemoryPool->allocate(size);
    }


    void myFree(void* ptr) {
        if (!ptr) return;
        if (!globalMemoryPool) {
            free(ptr);
            return;
        }

        globalMemoryPool->deallocate(ptr);
    }
}



MemoryPool::MemoryPool(size_t pS, size_t bS, size_t aS)
    : pageSize(pS)
    , blockList(nullptr)
    , alignSize(aS)
{
    pthread_mutex_init(&mutex, nullptr);
    size_t a = alignSize;

    // 对齐要求必须是 2 的幂次方
    if ((a & (a - 1)) != 0)
    {
        throw std::invalid_argument("Alignment size must be a power of two.");
    }
    // 向上对齐 blockSize 到 a
    size_t alignedBlockSize = (bS + a - 1) & ~(a - 1);
    // 确保至少能存一个 Node（next 指针）
    blockSize = (alignedBlockSize < sizeof(Node)) ? sizeof(Node) : alignedBlockSize;

    if (pageSize < blockSize)
    {
        pageSize = blockSize; // 自动调整
    }

    usablePageSize = pageSize - (pageSize % blockSize);
}

MemoryPool::~MemoryPool()
{
    for (auto page : pages)
    {
        delete[] static_cast<char*>(page);//
    }
}

void MemoryPool::expand()
{
    char* page = new char[pageSize];
    pages.push_back(page); // new char[N] 返回的地址一定是 16 字节对齐的

    size_t numBlocks = usablePageSize / blockSize;
    for (size_t i = 0; i < numBlocks; ++i)
    {
        insert2List(page + i * blockSize);
    }
    uintptr_t currentPageAddr = reinterpret_cast<uintptr_t>(page);
    if (currentPageAddr < startAddr)
        startAddr = currentPageAddr;
    if (currentPageAddr > endAddr)
        endAddr = currentPageAddr;
}

void MemoryPool::insert2List(void* p)
{
    Node* node = static_cast<Node*>(p);
    node->next = blockList;
    blockList = node;
}

void* MemoryPool::allocate(size_t size)
{
    if (size < blockSize)
    {
        pthread_mutex_lock(&mutex);
        if (!blockList)
        {
            expand();
            if (!blockList)
            {
                pthread_mutex_unlock(&mutex);
                return nullptr;
            }
        }
        Node* node = blockList;
        if (blockList)
        {
            blockList = blockList->next;

        }
        pthread_mutex_unlock(&mutex);
        mallocCount.fetch_add(1, std::memory_order_relaxed);
        return node;
    }
    return malloc(size);
}

void MemoryPool::deallocate(void* ptr)
{
    if (ptr == nullptr)
        return;
    uintptr_t p = reinterpret_cast<uintptr_t>(ptr);


    if (p >= startAddr && p < endAddr + pageSize)
    {
        pthread_mutex_lock(&mutex);
        for (auto page : pages)
        {

            uintptr_t pg = reinterpret_cast<uintptr_t>(page);
            if (p >= pg && p < pg + usablePageSize && (p - pg) % blockSize == 0)
            {
                insert2List(ptr);
                pthread_mutex_unlock(&mutex);
                freeCount.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        pthread_mutex_unlock(&mutex);
    }

    free(ptr);
}