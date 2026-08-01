#pragma once

// SPSC 有界环形队列（无锁）
// - 单生产者：WGC 捕获回调线程
// - 单消费者：Pacing 消费线程
// - 固定容量：构造时一次性分配，运行时零动态分配
// - 满时丢弃最旧帧（drop oldest）
// - 内部含 1 个守卫槽（容量取 2 的幂，可用 = 槽数 - 1），
//   保证生产者绝不写入消费者正在读取的槽位

#include <atomic>
#include <cstdint>
#include <vector>

#include "graphics/Frame.h"

namespace sf {

class FrameQueue {
public:
    explicit FrameQueue(uint32_t capacity = 8) {
        uint32_t cap = 4;
        while (cap < capacity) cap <<= 1;
        m_slots.resize(cap);          // 一次性分配
        m_capacity = cap - 1;         // 守卫槽
        m_mask     = cap - 1;
    }

    // 生产者：满时丢弃最旧（返回 true 表示发生了丢弃覆盖）
    bool Push(CaptureFrame&& f) {
        const uint32_t w = m_write.load(std::memory_order_relaxed);
        const uint32_t r = m_read.load(std::memory_order_acquire);
        bool overwrote = false;
        if (w - r >= m_capacity) {                    // 满：丢最旧
            m_read.store(r + 1, std::memory_order_release);
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            overwrote = true;
        }
        m_slots[w & m_mask] = std::move(f);           // 移入（ComPtr 指针交换，无分配）
        m_write.store(w + 1, std::memory_order_release);
        m_pushed.fetch_add(1, std::memory_order_relaxed);
        return overwrote;
    }

    // 消费者：非阻塞弹出；空返回 false
    bool Pop(CaptureFrame& out) {
        const uint32_t r = m_read.load(std::memory_order_relaxed);
        const uint32_t w = m_write.load(std::memory_order_acquire);
        if (r == w) return false;
        out = std::move(m_slots[r & m_mask]);
        m_read.store(r + 1, std::memory_order_release);
        return true;
    }

    // 清空并复位计数（Stop/重启时调用；释放残留帧引用）
    void Reset() {
        CaptureFrame tmp;
        while (Pop(tmp)) {}
        m_read.store(0);
        m_write.store(0);
        m_pushed.store(0);
        m_dropped.store(0);
    }

    uint32_t Size() const {
        return m_write.load(std::memory_order_acquire) -
               m_read.load(std::memory_order_acquire);
    }
    uint32_t Capacity() const { return m_capacity; }
    uint64_t TotalPushed() const  { return m_pushed.load(std::memory_order_relaxed); }
    uint64_t TotalDropped() const { return m_dropped.load(std::memory_order_relaxed); }

private:
    std::vector<CaptureFrame> m_slots;   // 固定容量
    uint32_t m_capacity = 0;
    uint32_t m_mask     = 0;
    std::atomic<uint32_t> m_read{0};
    std::atomic<uint32_t> m_write{0};
    std::atomic<uint64_t> m_pushed{0};
    std::atomic<uint64_t> m_dropped{0};
};

} // namespace sf
