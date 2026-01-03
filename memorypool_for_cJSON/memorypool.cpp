
#include "memorypool.h"


MemoryPool::MemoryPool(size_t pS, size_t bS, size_t aS)
: pageSize(pS)
,alignSize(aS)
,blockList(nullptr)
{
    // 向上对齐 blockSize 到 a
    size_t alignedBlockSize = (bS + aS - 1) & ~(aS - 1);
    // 确保至少能存一个 Node（next 指针）
    blockSize = (alignedBlockSize < sizeof(Node)) ? sizeof(Node) : alignedBlockSize;
    
    if (pageSize < blockSize)
    {
        pageSize = blockSize; // 自动调整S
    }
}

MemoryPool::~MemoryPool()
{
    for(auto page : pages)
    {
        delete[] static_cast<char *>(page);//
    }
}

void MemoryPool::expand()
{
    char* page = new char[pageSize];
    pages.push_back(page); // new char[N] 返回的地址一定是 16 字节对齐的

    size_t numBlocks = pageSize / blockSize;
    for (size_t i = 0; i < numBlocks; ++i)
    {
        insert2List(page + i * blockSize);
    }
}

void MemoryPool::insert2List(void *p)
{
    Node *node = static_cast<Node *>(p);
    node->next = blockList;
    blockList = node;
}

void* MemoryPool::allocate(size_t size)
{
    if (size <= blockSize)
    {
        if (!blockList)
        {
            expand();
            if (!blockList)
            {
                return nullptr;
            }
        }
        Node* node = blockList;
        blockList = blockList->next;
        return node;
    }
    else
    {
        char* ptr = (char*)malloc(size);
        return ptr;
    }
    
}

void MemoryPool::deallocate(void* ptr)
{
    if (ptr == nullptr)
        return;
    size_t useablePageSize = pageSize - pageSize % blockSize;
    char* p = static_cast<char*>(ptr);
    for (auto page : pages)
    {
        char* start = static_cast<char*>(page);
        char* end = start + useablePageSize; 
        if (p >= start && p < end)
        {
            if ((p - start) % blockSize == 0)
            {
                insert2List(p);
                return;
            }
                
        }
    }
    free(p);
}



