#ifndef _COROUTINE_H_  // 防止头文件重复包含
#define _COROUTINE_H_

#include <iostream>     // 输入输出流
#include <memory>       // 智能指针
#include <atomic>       // 原子操作
#include <functional>   // 函数对象
#include <cassert>      // 断言
#include <ucontext.h>   // 用户态上下文
#include <unistd.h>     // POSIX 标准库
#include <mutex>        // 互斥锁

namespace sylar {

// 协程类
class Fiber : public std::enable_shared_from_this<Fiber> {
public:
    // 协程状态
    enum State {
        READY,   // 就绪状态
        RUNNING, // 运行状态
        TERM     // 终止状态
    };

private:
    // 私有构造函数，仅由 GetThis() 调用，用于创建主协程
    Fiber();

public:
    // 构造函数，创建协程并分配栈空间
    Fiber(std::function<void()> cb, size_t stack_size = 0, bool run_in_schedule = true);
    ~Fiber();  // 析构函数

    // 重置协程，重用协程对象
    void reset(std::function<void()> cb);

    // 恢复协程运行
    void resume();
    // 让出协程执行权
    void yield();

    // 获取协程 ID
    uint64_t getId() const { return m_id; }
    // 获取协程状态
    State getState() const { return m_state; }

public:
    // 设置当前运行的协程
    static void SetThis(Fiber* f);
    // 获取当前运行的协程
    static std::shared_ptr<Fiber> Getthis();
    // 设置调度协程（默认为主协程）
    static void SetSchedulerFiber(Fiber* f);
    // 获取当前协程 ID
    static uint64_t GetFiberId();
    // 协程入口函数
    static void MainFunc();

private:
    uint64_t m_id = 0;            // 协程 ID
    uint32_t m_stack_size = 0;    // 协程栈大小
    State m_state = READY;        // 协程状态
    ucontext_t m_ctx;             // 协程上下文
    void* m_stack = nullptr;      // 协程栈空间
    std::function<void()> m_cb;   // 协程任务函数
    bool m_runInScheduler;        // 是否在调度器中运行
public:
    std::mutex m_mutex;           // 互斥锁
};

}  // namespace sylar

#endif  // _COROUTINE_H_