#include "scheduler.h"

static bool debug = false;  // 调试模式开关

namespace sylar {

// 线程局部存储：当前线程关联的调度器
static thread_local Scheduler* t_scheduler = nullptr;

// 获取当前线程的调度器
Scheduler* Scheduler::GetThis() {
    return t_scheduler;
}

// 设置当前线程的调度器为自身
void Scheduler::SetThis() {
    t_scheduler = this;
}

// 构造函数
Scheduler::Scheduler(size_t threads, bool use_caller, const std::string& name)
    : m_useCaller(use_caller), m_name(name) {
    assert(threads > 0 && Scheduler::GetThis() == nullptr);  // 验证参数合法性
    SetThis();  // 绑定当前线程的调度器

    Thread::SetName(m_name);  // 设置线程名称

    // 若使用主线程作为工作线程
    if (use_caller) {
        threads--;  // 总线程数减1（主线程占一个名额）

        // 确保主线程已初始化主协程
        Fiber::GetThis();

        // 创建调度器协程，用于主线程的任务循环
        m_schedulerFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false));
        Fiber::SetSchedulerFiber(m_schedulerFiber.get());  // 设置为调度协程

        m_rootThread = Thread::GetThreadId();  // 记录主线程ID
        m_threadIds.push_back(m_rootThread);   // 添加到线程ID列表
    }

    m_threadCount = threads;  // 设置线程数
    if (debug) std::cout << "Scheduler::Scheduler() success\n";
}

// 析构函数
Scheduler::~Scheduler() {
    assert(stopping() == true);  // 必须处于停止状态
    if (GetThis() == this) {     // 解除当前线程的调度器绑定
        t_scheduler = nullptr;
    }
    if (debug) std::cout << "Scheduler::~Scheduler() success\n";
}

// 启动线程池
void Scheduler::start() {
    std::lock_guard<std::mutex> lock(m_mutex);  // 加锁
    if (m_stopping) {  // 已停止则报错
        std::cerr << "Scheduler is stopped" << std::endl;
        return;
    }

    assert(m_threads.empty());  // 确保线程池未初始化
    m_threads.resize(m_threadCount);  // 初始化线程池
    for (size_t i = 0; i < m_threadCount; i++) {
        // 创建线程，绑定run方法
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this), 
                            m_name + "_" + std::to_string(i)));
        m_threadIds.push_back(m_threads[i]->getId());  // 记录线程ID
    }
    if (debug) std::cout << "Scheduler::start() success\n";
}

// 线程的主循环
void Scheduler::run() {
    int thread_id = Thread::GetThreadId();  // 当前线程ID
    if (debug) std::cout << "Scheduler::run() starts in thread: " << thread_id << std::endl;

    //set_hook_enable(true);

    SetThis();  // 绑定当前线程的调度器

    // 若非主线程，需初始化主协程
    if (thread_id != m_rootThread) {
        Fiber::GetThis();
    }

    // 创建空闲协程（处理无任务时的逻辑）
    std::shared_ptr<Fiber> idle_fiber = std::make_shared<Fiber>(std::bind(&Scheduler::idle, this));
    ScheduleTask task;  // 临时任务对象

    while (true) {
        task.reset();  // 重置任务
        bool ticket_me = false;//判断是否需要唤醒其它线程

        {
            std::lock_guard<std::mutex> lock(m_mutex);  // 加锁访问任务队列
            auto it = m_tasks.begin();
            // 遍历任务队列，寻找可执行的任务
            while (it != m_tasks.end()) {
                // 若任务指定了线程且不匹配当前线程，跳过并标记需要唤醒
                if (it->thread != -1 && it->thread != thread_id) {
                    it++;
                    ticket_me = true;
                    continue;
                }

                // 找到有效任务，取出并退出循环
                assert(it->fiber || it->cb);
                task = *it;
                m_tasks.erase(it);
                m_activeThreadCount++;  // 增加活跃线程计数
                break;
            }
            ticket_me = ticket_me || (it != m_tasks.end());  // 是否需要唤醒其他线程
        }

        if (ticket_me) {
            tickle();  // 唤醒其他线程
        }

        // 处理任务
        if (task.fiber) {  // 协程任务
            {
                std::lock_guard<std::mutex> lock(task.fiber->m_mutex);
                if (task.fiber->getState() != Fiber::TERM) {  // 仅运行未终止的协程
                    task.fiber->resume();  // 恢复协程执行
                }
            }
            m_activeThreadCount--;  // 减少活跃计数
            task.reset();
        } else if (task.cb) {  // 回调函数任务
            std::shared_ptr<Fiber> cb_fiber = std::make_shared<Fiber>(task.cb);
            {
                std::lock_guard<std::mutex> lock(cb_fiber->m_mutex);
                cb_fiber->resume();  // 创建新协程执行回调
            }
            m_activeThreadCount--;
            task.reset();
        } else {  // 无任务，执行空闲逻辑
            // 若空闲协程已终止，退出循环
            if (idle_fiber->getState() == Fiber::TERM) {
                if (debug) std::cout << "Scheduler::run() ends in thread: " << thread_id << std::endl;
                break;
            }
            m_idleThreadCount++;    // 增加空闲计数
            idle_fiber->resume();   // 执行空闲协程
            m_idleThreadCount--;    // 恢复空闲计数
        }
    }
}

// 停止调度器
void Scheduler::stop() {
    if (debug) std::cout << "Scheduler::stop() starts in thread: " << Thread::GetThreadId() << std::endl;

    if (stopping()) {  // 若已停止，直接返回
        return;
    }

    m_stopping = true;  // 标记为停止状态

    // 验证是否在主线程调用
    if (m_useCaller) {
        assert(GetThis() == this);
    } else {
        assert(GetThis() != this);
    }

    // 唤醒所有线程，确保它们能退出循环
    for (size_t i = 0; i < m_threadCount; i++) {
        tickle();
    }

    // 唤醒主线程的调度协程
    if (m_schedulerFiber) {
        tickle();
        m_schedulerFiber->resume();  // 恢复主线程的调度协程
        if (debug) std::cout << "m_schedulerFiber ends in thread:" << Thread::GetThreadId() << std::endl;
    }

    // 等待所有线程结束
    std::vector<std::shared_ptr<Thread>> thrs;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        thrs.swap(m_threads);  // 转移线程所有权
    }

    for (auto& i : thrs) {
        i->join();  // 等待线程结束
    }
    if (debug) std::cout << "Scheduler::stop() ends in thread:" << Thread::GetThreadId() << std::endl;
}

// 空闲处理函数（协程）
void Scheduler::idle() {
    while (!stopping()) {  // 循环直到停止
        if (debug) std::cout << "Scheduler::idle(), sleeping in thread: " << Thread::GetThreadId() << std::endl;
        sleep(1);           // 休眠1秒（模拟空闲）
        Fiber::GetThis()->yield();  // 让出CPU
    }
}

// 当前为空实现，需扩展唤醒逻辑（如条件变量）
void Scheduler::tickle() {}

}  // namespace sylar