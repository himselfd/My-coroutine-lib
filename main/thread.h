#ifndef _THREAD_H_  // 防止头文件被重复包含
#define _THREAD_H_

#include <mutex>              // 引入互斥锁
#include <condition_variable> // 引入条件变量
#include <functional>         // 引入 std::function

namespace sylar {

// 信号量类，用于线程间的同步
class Semaphore {
private:
    std::mutex mtx;              // 互斥锁，用于保护共享资源
    std::condition_variable cv;  // 条件变量，用于线程间通信
    int count;                   // 信号量的计数器

public:
    // 构造函数，初始化信号量的计数器
    explicit Semaphore(int count_ = 0) : count(count_) {}

    // P 操作（等待信号量）
    void wait() {
        std::unique_lock<std::mutex> lock(mtx);  // 加锁
        if (count == 0) {                        // 如果计数器为 0，等待信号量
            cv.wait(lock);                       // 等待 V 操作唤醒
        }
        count--;                                 // 计数器减 1
    }

    // V 操作（释放信号量）
    void signal() {
        std::unique_lock<std::mutex> lock(mtx);  // 加锁
        count++;                                 // 计数器加 1
        cv.notify_one();                         // 唤醒一个等待的线程
    }
};

// 线程类，封装线程的创建、管理和同步
class Thread {
public:
    // 构造函数，传入线程函数和线程名称
    Thread(std::function<void()> cb, const std::string& name);
    ~Thread();  // 析构函数

    // 获取线程 ID
    pid_t getId() const { return m_id; }
    // 获取线程名称
    const std::string& getName() const { return m_name; }

    // 等待线程执行完毕
    void join();

public:
    // 静态方法：获取当前线程的系统级 ID
    static pid_t GetThreadId();
    // 静态方法：获取当前线程的 Thread 对象
    static Thread* GetThis();
    // 静态方法：获取当前线程的名称
    static const std::string& GetName();
    // 静态方法：设置当前线程的名称
    static void SetName(const std::string& name);

private:
    // 静态方法：线程的入口函数
    static void* run(void* arg);

private:
    pid_t m_id = -1;            // 线程的系统级 ID
    pthread_t m_thread = 0;     // 线程句柄
    std::function<void()> m_cb; // 线程要执行的任务
    std::string m_name;         // 线程的名称
    Semaphore m_semaphore;      // 信号量，用于线程同步
};

}  // namespace sylar

#endif  // _THREAD_H_