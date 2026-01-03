#include <iostream>
#include <vector>

class MemoryPool
{

public:
    explicit MemoryPool(size_t pS, size_t bS, size_t aS);
    ~MemoryPool();

    MemoryPool(const MemoryPool &) = delete;
    MemoryPool &operator=(const MemoryPool &) = delete;

    // 分配小块内存
    void *allocate(size_t size);

    // 回收小块内存
    void deallocate(void *ptr);

private:
    void insert2List(void *);

    // 申请大块内存、切分、挂载
    void expand();

private:
    struct Node
    {
        Node *next;
        Node() { next = nullptr; }
    };

    std::vector<void *> pages; // 存放页地址

    Node *blockList; // 挂载小块地址

    size_t pageSize;
    size_t blockSize;
    size_t alignSize;
};