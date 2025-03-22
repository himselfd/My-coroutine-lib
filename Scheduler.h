#ifndef _SCHEDULER_H_  // 防止头文件重复包含
#define _SCHEDULER_H_

//#include "hook.h"     
#include "fiber.h"      // 协程实现
#include "thread.h"     // 线程实现

#include <mutex>        // 互斥锁
#include <vector>       // 动态数组容器

namespace sylar {

// 调度器类，管理线程池和协程任务
class Scheduler {
public:
    // 构造函数：指定线程数、是否使用主线程作为工作线程、调度器名称
    Scheduler(size_t threads = 1, bool use_caller = true, const std::string& name = "Scheduler");
    virtual ~Scheduler();  // 虚析构函数，支持继承

    // 获取调度器名称
    const std::string& getName() { return m_name; }

public:
    // 静态方法：获取当前线程关联的调度器
    static Scheduler* GetThis();

protected:
    // 设置当前线程关联的调度器为自身
    void SetThis();

public:
    // 添加任务到任务队列（模板方法，支持协程或回调函数）
    template <class FiberOrCb>
    void schedulerLock(FiberOrCb fc, int thread = -1) {
        bool need_ticket;
        {
            std::lock_guard<std::mutex> lock(m_mutex);  // 加锁保护任务队列
            need_ticket = m_tasks.empty();  // 若队列为空，需要唤醒线程

            ScheduleTask task(fc, thread);  // 创建任务对象
            if (task.fiber || task.cb) {   // 确保任务有效
                m_tasks.push_back(task);    // 添加到任务队列
            }
        }

        if (need_ticket) {  // 需要唤醒空闲线程
            ticket();
        }
    }

    // 启动线程池
    virtual void start();
    // 关闭线程池
    virtual void stop();

protected:
    virtual void tickle();  // 唤醒空闲线程
    virtual void run();     // 线程运行的主循环
    virtual void idle();    // 空闲时的处理逻辑
    virtual bool stopping(); // 判断是否正在停止

    // 是否有空闲线程
    bool hasIdleThreads() { return m_idleThreadCount > 0; }

private:
    // 任务结构体，封装协程或回调函数
    struct ScheduleTask {
        std::shared_ptr<Fiber> fiber;  // 协程任务
        std::function<void()> cb;      // 回调函数
        int thread;  // 指定运行的线程ID（-1表示任意线程）

        ScheduleTask() : fiber(nullptr), cb(nullptr), thread(-1) {}

        // 多种构造函数，支持不同类型的任务
        ScheduleTask(std::shared_ptr<Fiber> f, int thr) : fiber(f), thread(thr) {}
        ScheduleTask(std::shared_ptr<Fiber>* f, int thr) { fiber.swap(*f); thread = thr; }
        ScheduleTask(std::function<void()> f, int thr) : cb(f), thread(thr) {}
        ScheduleTask(std::function<void()>* f, int thr) { cb.swap(*f); thread = thr; }

        void reset() {  // 重置任务
            fiber = nullptr;
            cb = nullptr;
            thread = -1;
        }
    };

private:
    std::string m_name;                 // 调度器名称
    std::mutex m_mutex;                 // 互斥锁，保护任务队列
    std::vector<std::shared_ptr<Thread>> m_threads;  // 线程池
    std::vector<ScheduleTask> m_tasks;  // 任务队列
    std::vector<int> m_threadIds;       // 所有线程的ID列表
    size_t m_threadCount = 0;           // 线程总数
    std::atomic<size_t> m_activeThreadCount{0};  // 活跃线程数
    std::atomic<size_t> m_idleThreadCount{0};    // 空闲线程数
    bool m_useCaller;                   // 是否使用主线程作为工作线程
    std::shared_ptr<Fiber> m_schedulerFiber;  // 调度器协程（主线程使用）
    int m_rootThread = -1;              // 主线程ID
    bool m_stopping = false;            // 是否正在关闭
};

}  // namespace sylar

#endif  // _SCHEDULER_H_