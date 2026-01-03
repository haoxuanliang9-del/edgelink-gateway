#pragma once
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

// 时间轮层级参数（2 的幂）
#define TVR_BITS 8 // L1: 2^8 = 256 slots
#define TVN_BITS 6 // L2~L5: 2^6 = 64 slots

#define TVR_SIZE (1U << TVR_BITS) // 256
#define TVN_SIZE (1U << TVN_BITS) // 64

#define TVR_MASK (TVR_SIZE - 1) // 0xFF
#define TVN_MASK (TVN_SIZE - 1) // 0x3F

// 最大支持延迟：2^(8 + 4*6) = 2^32 = 4294967296，但 uint64_t 最大为 4294967295
#define MAX_SUPPORTED_DELAY (UINT32_MAX)

typedef void (*callback)(void*);


typedef struct TimeWheelNode
{
    callback func;
    struct TimeWheelNode* next;
    void* args;
    uint64_t expire;
    bool active;

} TimeWheelNode;

typedef struct Wheel
{
    TimeWheelNode* wheelL1[TVR_SIZE]; // 256
    TimeWheelNode* wheelL2[TVN_SIZE]; // 64
    TimeWheelNode* wheelL3[TVN_SIZE];
    TimeWheelNode* wheelL4[TVN_SIZE];
    TimeWheelNode* wheelL5[TVN_SIZE];
    uint64_t time;
} Wheel;

uint64_t get_monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

void initWheel(Wheel* wheel);

void expireTimer(Wheel* w);

TimeWheelNode* addNewTimer(Wheel* wheel, callback func, uint64_t delay, void* args);

void clearTimeWheel(Wheel* w);

void cancelTimer(TimeWheelNode* t);
