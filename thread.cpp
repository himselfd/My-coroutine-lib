#include "thread.h"  // 引入头文件

#include <sys/syscall.h>  // 引入系统调用相关函数
#include <iostream>       // 引入输入输出流
#include <unistd.h>       // 引入 POSIX 标准库

namespace sylar {

// 线程局部存储：当前线程的 Thread 对象指针
static thread_local Thread* t_thread = nullptr;
// 线程局部存储：当前线程的名称
static thread_local std::string t_thread_name = "UNKNOWN";

// 获取当前线程的系统级 ID
pid_t Thread::GetThreadId() {
    return syscall(SYS_gettid);  // 使用系统调用获取线程 ID
}

// 获取当前线程的 Thread 对象
Thread* Thread::GetThis() {
    return t_thread;
}

// 获取当前线程的名称
const std::string& Thread::GetName() {
    return t_thread_name;
}

// 设置当前线程的名称
void Thread::SetName(const std::string& name) {
    if (t_thread) {  // 如果当前线程的 Thread 对象存在
        t_thread->m_name = name;  // 设置 Thread 对象的名称
    }
    t_thread_name = name;  // 设置线程局部存储的名称
}

// 构造函数，创建线程并初始化
Thread::Thread(std::function<void()> cb, const std::string& name)
    : m_cb(cb), m_name(name) {
    // 创建线程，传入线程函数 run 和当前对象的指针
    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);
    if (rt) {  // 如果线程创建失败
        std::cerr << "pthread_create thread fail, rt=" << rt << " name=" << name;
        throw std::logic_error("pthread_create error");  // 抛出异常
    }
    // 等待线程函数完成初始化
    m_semaphore.wait();
}

// 析构函数
Thread::~Thread() {
    if (m_thread) {  // 如果线程句柄存在
        pthread_detach(m_thread);  // 分离线程，避免资源泄漏
        m_thread = 0;  // 重置线程句柄
    }
}

// 等待线程执行完毕
void Thread::join() {
    if (m_thread) {  // 如果线程句柄存在
        int rt = pthread_join(m_thread, nullptr);  // 等待线程结束
        if (rt) {  // 如果等待失败
            std::cerr << "pthread_join failed, rt = " << rt << ", name = " << m_name << std::endl;
            throw std::logic_error("pthread_join error");  // 抛出异常
        }
        m_thread = 0;  // 重置线程句柄
    }
}

// 线程的入口函数
void* Thread::run(void* arg) {
    Thread* thread = (Thread*)arg;  // 将参数转换为 Thread 对象指针

    t_thread = thread;  // 设置当前线程的 Thread 对象
    t_thread_name = thread->m_name;  // 设置当前线程的名称
    thread->m_id = GetThreadId();  // 获取当前线程的系统级 ID
    // 设置线程的名称（名称长度最多为 15 个字符）
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());

    std::function<void()> cb;
    cb.swap(thread->m_cb);  // 交换任务函数，减少智能指针的引用计数

    // 初始化完成，通知主线程可以继续执行
    thread->m_semaphore.signal();

    cb();  // 执行任务函数
    return 0;  // 返回空指针
}

}  // namespace sylar