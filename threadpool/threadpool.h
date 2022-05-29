#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list> //顺序链表
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

//线程池模板类
//将它定义为模板类是为了代码复用，模板参数T是任务类
//使用静态成员函数worker，需要类内声明，类外初始化；无this指针
//之所以用这个静态成员函数是因为pthread_create陷阱，见详解
template <typename T>
class threadpool {
public:
    //构造函数（模型序号，数据库，线程数，最大请求数）
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state); //向请求队列中插入任务请求
    bool append_p(T *request);

private:
    //工作线程运行函数，不断从工作队列中取出任务并执行
    static void *worker(void *arg); //线程处理函数
    void run();//执行任务

private:
    int m_thread_number;         //线程数
    int m_max_requests;          //请求队列中允许的最大请求数
    pthread_t *m_threads;        //描述线程池的数组，大小为m_thread_number
    std::list<T *> m_workqueue;  //请求队列
    locker m_queuelocker;        //保护请求队列的互斥锁
    sem m_queuestat;             //是否有任务需要处理（信号量）
    connection_pool *m_connPool;    //数据库
    int m_actor_model;           //模型切换序号
};

template <typename T>
// ：后对成员进行包含，例如用传入的actor_model去初始化m_actor_model
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL), m_connPool(connPool)
{
    if(thread_number <= 0 || max_requests <= 0) throw std::exception(); //错误返回
    m_threads = new pthread_t[m_thread_number]; //创建线程数组
    if(!m_threads) throw std::exception(); //创建失败返回
    for(int i = 0; i < thread_number; ++i) {
        //判断中创建线程，失败就删除数组并返回
        //类对象传递时用this指针，传递给静态函数后，将其转换为线程池类，并调用私有成员函数run
        if(pthread_create(m_threads + i, NULL, worker, this) != 0) { //这里的worker必须是静态成员函数
            delete[] m_threads;
            throw std::exception();
        }
        //判断中线程分离：线程主动与主控线程断开关系。
        //线程结束后（不会产生僵尸线程），其退出状态不由其他线程获取，直接自己自动释放（不会单独对工作线程回收）
        //失败就删除数组并返回
        if(pthread_detach(m_threads[i])) { 
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
}

template <typename T>
bool threadpool<T>::append(T *request, int state) {
    m_queuelocker.lock(); //互斥锁加锁,保证线程安全
    if(m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock(); //互斥锁解锁
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request); //加入工作队列
    m_queuelocker.unlock(); //互斥锁解锁
    m_queuestat.post(); //信号量V操作，唤醒或加1，提醒有任务要处理
    return true;
}
//比上一个函数少一步
template <typename T>
bool threadpool<T>::append_p(T *request) {
    m_queuelocker.lock(); //互斥锁加锁
    if(m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock(); //互斥锁解锁
        return false;        
    }
    m_workqueue.push_back(request); //加入工作队列
    m_queuelocker.unlock(); //互斥锁解锁
    m_queuestat.post(); //信号量V操作
    return true;
}

template <typename T>
void *threadpool<T>::worker(void *arg) {
    //将参数强转为线程池类，调用成员方法
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run() {
    while(true) {
        m_queuestat.wait();//信号量P操作,信号量等待
        m_queuelocker.lock();//被唤醒后先互斥锁加锁
        if(m_workqueue.empty()) {
            m_queuelocker.unlock();//互斥锁解锁
            continue;
        }
        //取出任务并从请求队列中删除
        T *request = m_workqueue.front();//赋予请求队列的第一个元素指针 
        m_workqueue.pop_front();//出队
        m_queuelocker.unlock();//互斥锁解锁
        if(!request) continue;
        if(1 == m_actor_model) {
            if(0 == request->m_state) { //状态为0
                if(request->read_once()) { //可读
                    request->improv = 1;
                    //从连接池取出一个数据库连接
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    //process（模板类中的方法，这里是http类）进行处理
                    request->process();
                }
                else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else { //状态不为0
                if(request->write()) { //可写
                    request->improv = 1;
                }
                else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif