#include "timer.h"

namespace sylar {

/******************* Timer 方法实现 *******************/

// 取消定时器
bool Timer::cancel() {
    std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mutex);
    if (!m_cb) return false; // 已取消

    m_cb = nullptr; // 清空回调
    
    // 从管理器的时间堆中移除
    auto it = m_manager->m_timers.find(shared_from_this());
    if (it != m_manager->m_timers.end()) {
        m_manager->m_timers.erase(it);
    }
    return true;
}

// 刷新定时器（重新计算下一次触发时间）
bool Timer::refresh() {
    std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mutex);
    if (!m_cb) return false; // 已取消

    auto it = m_manager->m_timers.find(shared_from_this());
    if (it == m_manager->m_timers.end()) return false;

    // 移除并重新插入以更新排序
    m_manager->m_timers.erase(it);
    m_next = std::chrono::system_clock::now() + std::chrono::milliseconds(m_ms);
    m_manager->m_timers.insert(shared_from_this());
    return true;
}

// 重设定时器
bool Timer::reset(uint64_t ms, bool from_now) {
    if (ms == m_ms && !from_now) return true; // 无需修改

    {
        std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mutex);
        if (!m_cb) return false;

        // 从堆中移除旧时间
        auto it = m_manager->m_timers.find(shared_from_this());
        if (it == m_manager->m_timers.end()) return false;
        m_manager->m_timers.erase(it);
    }

    // 计算新的触发时间
    auto start = from_now ? std::chrono::system_clock::now() 
                          : (m_next - std::chrono::milliseconds(m_ms));
    m_ms = ms;
    m_next = start + std::chrono::milliseconds(m_ms);
    m_manager->addTimer(shared_from_this()); // 重新插入堆
    return true;
}

// 构造函数（私有）
Timer::Timer(uint64_t ms, std::function<void()> cb, 
            bool recurring, TimerManager* manager)
    : m_recurring(recurring), m_ms(ms), m_cb(cb), m_manager(manager) {
    m_next = std::chrono::system_clock::now() + 
             std::chrono::milliseconds(m_ms); // 初始化触发时间
}

// 比较器（按触发时间排序）
bool Timer::Comparator::operator()(
    const std::shared_ptr<Timer>& lhs, 
    const std::shared_ptr<Timer>& rhs) const {
    return lhs->m_next < rhs->m_next; // 小顶堆
}

/******************* TimerManager 方法实现 *******************/

TimerManager::TimerManager() {
    m_previouseTime = std::chrono::system_clock::now(); // 初始化检测时间
}

// 添加普通定时器
std::shared_ptr<Timer> TimerManager::addTimer(
    uint64_t ms, std::function<void()> cb, bool recurring) {
    auto timer = std::make_shared<Timer>(ms, cb, recurring, this);
    addTimer(timer);
    return timer;
}

// 内部添加定时器
void TimerManager::addTimer(std::shared_ptr<Timer> timer) {
    bool at_front = false;
    {
        std::unique_lock<std::shared_mutex> write_lock(m_mutex);
        auto it = m_timers.insert(timer).first; // 插入定时器
        at_front = (it == m_timers.begin()) && !m_tickled; // 是否插入到堆顶
        if (at_front) m_tickled = true; // 标记已触发
    }

    if (at_front) onTimerInsertedAtFront(); // 触发前端插入事件
}

// 条件定时器回调包装
static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb) {
    std::shared_ptr<void> tmp = weak_cond.lock();
    if (tmp) cb(); // 只有当条件存在时执行
}

// 添加条件定时器
std::shared_ptr<Timer> TimerManager::addConditionTimer(
    uint64_t ms, std::function<void()> cb, 
    std::weak_ptr<void> weak_cond, bool recurring) {
    return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
}

// 获取最近定时器剩余时间
uint64_t TimerManager::getNextTimer() {
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    m_tickled = false; // 重置触发标记

    if (m_timers.empty()) return ~0ull; // 无定时器返回最大值

    auto now = std::chrono::system_clock::now();
    auto next_time = (*m_timers.begin())->m_next;

    if (now >= next_time) return 0; // 已超时
    else {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            next_time - now);
        return duration.count();
    }
}

// 收集过期回调
void TimerManager::listExpiredCb(std::vector<std::function<void()>>& cbs) {
    auto now = std::chrono::system_clock::now();
    std::unique_lock<std::shared_mutex> write_lock(m_mutex);
    
    bool rollover = detectClockRollover(); // 检测时间回退

    while (!m_timers.empty() && (rollover || (*m_timers.begin())->m_next <= now)) {
        auto timer = *m_timers.begin();
        m_timers.erase(m_timers.begin());

        cbs.push_back(timer->m_cb); // 收集回调

        if (timer->m_recurring) { // 循环定时器重新插入
            timer->m_next = now + std::chrono::milliseconds(timer->m_ms);
            m_timers.insert(timer);
        } else {
            timer->m_cb = nullptr; // 标记为已取消
        }
    }
}

// 检测系统时间是否回退（例如手动修改系统时间）
bool TimerManager::detectClockRollover() {
    auto now = std::chrono::system_clock::now();
    bool rollover = (now < (m_previouseTime - std::chrono::hours(1)));
    m_previouseTime = now;
    return rollover; // 如果时间回退超过1小时，返回true
}

// 判断是否存在定时器
bool TimerManager::hasTimer() {
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    return !m_timers.empty();
}

} // namespace sylar