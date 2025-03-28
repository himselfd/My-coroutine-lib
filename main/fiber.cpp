#include "fiber.h"

static bool debug = false;  // 调试标志

namespace sylar {

// 当前线程的协程控制信息

// 正在运行的协程
static thread_local Fiber* t_fiber = nullptr;
// 主协程
static thread_local std::shared_ptr<Fiber> t_thread_fiber = nullptr;
// 调度协程
static thread_local Fiber* t_scheduler_Fiber = nullptr;

// 协程 ID 计数器
static std::atomic<uint64_t> s_fiber_id{0};
// 协程总数计数器
static std::atomic<uint64_t> s_fiber_count{0};

// 设置当前运行的协程
void Fiber::SetThis(Fiber* f) {
    t_fiber = f;
}

// 获取当前运行的协程，如果不存在则创建主协程
std::shared_ptr<Fiber> Fiber::Getthis() {
    if (t_fiber) {
        return t_fiber->shared_from_this();
    }
    // 创建主协程
    std::shared_ptr<Fiber> main_fiber(new Fiber());
    t_thread_fiber = main_fiber;
    t_scheduler_Fiber = main_fiber.get();  // 主协程默认为调度协程

    assert(t_fiber == main_fiber.get());
    return t_fiber->shared_from_this();
}

// 设置调度协程
void Fiber::SetSchedulerFiber(Fiber* f) {
    t_scheduler_Fiber = f;
}

// 获取当前协程 ID
uint64_t Fiber::GetFiberId() {
    if (t_fiber) {
        return t_fiber->getId();
    }
    return (uint64_t)-1;  // 如果当前没有协程，返回无效 ID
}

// 主协程构造函数
Fiber::Fiber() {
    SetThis(this);  // 设置当前协程
    m_state = RUNNING;  // 设置状态为运行中

    // 初始化上下文
    if (getcontext(&m_ctx)) {
        std::cerr << "Fiber() failed\n";
        pthread_exit(NULL);
    }

    m_id = s_fiber_id++;  // 分配协程 ID
    s_fiber_count++;      // 增加协程计数
    if (debug) std::cout << "Fiber(): main id = " << m_id << std::endl;
}

// 普通协程构造函数
Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler)
    : m_cb(cb), m_runInScheduler(run_in_scheduler) {
    m_state = READY;  // 设置状态为就绪

    // 分配协程栈空间
    m_stack_size = stacksize ? stacksize : 128000;  // 默认栈大小为 128KB
    m_stack = malloc(m_stack_size);

    // 初始化上下文
    if (getcontext(&m_ctx)) {
        std::cerr << "Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler) failed\n";
        pthread_exit(NULL);
    }

    m_ctx.uc_link = nullptr;  // 上下文结束后不跳转到其他上下文
    m_ctx.uc_stack.ss_sp = m_stack;  // 设置栈空间
    m_ctx.uc_stack.ss_size = m_stack_size;  // 设置栈大小
    makecontext(&m_ctx, &Fiber::MainFunc, 0);  // 设置入口函数

    m_id = s_fiber_id++;  // 分配协程 ID
    s_fiber_count++;      // 增加协程计数
    if (debug) std::cout << "Fiber(): child id = " << m_id << std::endl;
}

// 析构函数
Fiber::~Fiber() {
    s_fiber_count--;  // 减少协程计数
    if (m_stack) {
        free(m_stack);  // 释放栈空间
    }
    if (debug) std::cout << "~Fiber(): id = " << m_id << std::endl;
}

// 重置协程，重用协程对象
void Fiber::reset(std::function<void()> cb) {
    assert(m_stack != nullptr && m_state == TERM);  // 确保协程已终止

    m_state = READY;  // 设置状态为就绪
    m_cb = cb;       // 设置新的任务函数

    // 重新初始化上下文
    if (getcontext(&m_ctx)) {
        std::cerr << "reset() failed\n";
        pthread_exit(NULL);
    }

    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stack_size;
    makecontext(&m_ctx, &Fiber::MainFunc, 0);
}

// 恢复协程运行
void Fiber::resume() {
    assert(m_state == READY);  // 确保协程处于就绪状态

    m_state = RUNNING;  // 设置状态为运行中

    if (m_runInScheduler) {//判断是否需要调度协程协助
        SetThis(this);  // 设置当前协程
        // 切换到当前协程
        if (swapcontext(&(t_scheduler_Fiber->m_ctx), &m_ctx)) {
            std::cerr << "resume() to t_scheduler_fiber failed\n";
            pthread_exit(NULL);
        }
    } else {
        SetThis(this);
        // 切换到当前协程
        if (swapcontext(&(t_thread_fiber->m_ctx), &m_ctx)) {
            std::cerr << "resume() to t_thread_fiber failed\n";
            pthread_exit(NULL);
        }
    }
}

// 让出协程执行权
void Fiber::yield() {
    assert(m_state == RUNNING || m_state == TERM);  // 确保协程正在运行或已终止

    if (m_state != TERM) {
        m_state = READY;  // 设置状态为就绪
    }

    if (m_runInScheduler) {//判断是否需要调度协程协助
        SetThis(t_scheduler_Fiber);  // 切换到调度协程
        // 切换到调度协程
        if (swapcontext(&m_ctx, &(t_scheduler_Fiber->m_ctx))) {
            std::cerr << "yield() to t_scheduler_fiber failed\n";
            pthread_exit(NULL);
        }
    } else {
        SetThis(t_thread_fiber.get());  // 切换到主协程
        // 切换到主协程
        if (swapcontext(&m_ctx, &(t_thread_fiber->m_ctx))) {
            std::cerr << "yield() to t_thread_fiber failed\n";
            pthread_exit(NULL);
        }
    }
}

// 协程入口函数
void Fiber::MainFunc() {
    std::shared_ptr<Fiber> curr = Getthis();  // 获取当前协程
    assert(curr != nullptr);

    curr->m_cb();  // 执行任务函数
    curr->m_cb = nullptr;  // 清空任务函数
    curr->m_state = TERM;  // 设置状态为终止

    // 运行完毕，让出执行权
    auto raw_ptr = curr.get();
    curr.reset();  // 释放智能指针
    raw_ptr->yield();  // 切换到调度协程或主协程
}

}  // namespace sylar