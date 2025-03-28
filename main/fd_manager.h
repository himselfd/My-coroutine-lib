#ifndef _FD_MANAGER_H_
#define _FD_MANAGER_H_

#include <memory>
#include <shared_mutex>
#include "thread.h"

namespace sylar{

    // fd info
    class FdCtx : public std::enable_shared_from_this<FdCtx>
    {
    private:
    bool m_isInit = false;//标记文件描述符是否已初始化。
    bool m_isSocket = false;//标记文件描述符是否是一个套接字。
    bool m_sysNonblock = false;//标记文件描述符是否设置为系统非阻塞模式。
    bool m_userNonblock = false;//标记文件描述符是否设置为用户非阻塞模式。
    bool m_isClosed = false;//标记文件描述符是否已关闭。
    int m_fd;//文件描述符的整数值

    // read event timeout
    uint64_t m_recvTimeout = (uint64_t)-1;//读事件的超时时间，默认为 -1 表示没有超时限制。
    // write event timeout
    uint64_t m_sendTimeout = (uint64_t)-1;//写事件的超时时间，默认为 -1 表示没有超时限制。

    public:
    FdCtx(int fd);
    ~FdCtx();

    bool init();//初始化 FdCtx 对象。
    bool isInit() const {return m_isInit;}
    bool isSocket() const {return m_isSocket;}
    bool isClosed() const {return m_isClosed;}

    void setUserNonblock(bool v) {m_userNonblock = v;}//设置和获取用户层面的非阻塞状态。
    bool getUserNonblock() const {return m_userNonblock;}

    void setSysNonblock(bool v) {m_sysNonblock = v;}//设置和获取系统层面的非阻塞状态。
    bool getSysNonblock() const {return m_sysNonblock;}
    //设置和获取超时时间，type 用于区分读事件和写事件的超时设置，v表示时间毫秒。
    void setTimeout(int type, uint64_t v);
    uint64_t getTimeout(int type);
    };

    class FdManager
    {
    public:
    FdManager();//构造函数
    //获取指定文件描述符的 FdCtx 对象。如果 auto_create 为 true，在不存在时自动创建新的 FdCtx 对象。
    std::shared_ptr<FdCtx> get(int fd, bool auto_create = false);
    void del(int fd);//删除指定文件描述符的 FdCtx 对象                                                       
    
    private:
    std::shared_mutex m_mutex;//用于保护对 m_datas 的访问，支持共享读锁和独占写锁。
    std::vector<std::shared_ptr<FdCtx>> m_datas;//存储所有 FdCtx 对象的共享指针。
    };
  
    template<class T>
    class Singleton{
    private:
    static T *instance;//对外提供的实例
    static std::mutext mutex;//互斥锁
    Singleton();
    ~Singleton();
    Singleton(const Singleton&)=delete;
    Singleton &operater=(const Singleton&)=delete;
    public:
    static T * GetInstance(){
        std::lock_grud<std::mutex>lock(mutex);//加锁
        if(instance==nullptr){
            instance=new T();
        }
        return instance;//提高对外的访问点，在系统生命周期中
                        //一般一个类只有一个全局实例。
        }
    static void DestroyInstace(){
        std::lock_guard<std::mutex>lock(mutex);
        if(instance){
            delete instance;
            instance=nullptr;//防止野指针
        }
    }
    };
    typedef Singleton<FdManager> FdMgr;//重定义将Singleton<FdManager> 变成FdMgr的缩写。 
}
#endif