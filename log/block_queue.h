//循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;
//线程安全，每个操作前都要先加互斥锁，操作完成后再解锁

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"
using namespace std;

//阻塞队列类
//当队列为空时，从队列中获取元素的线程会被挂起
//当队列满时，往队列添加元素的线程会挂起
template<class T>
class block_queue {
public:
    //构造函数创建循环数组
    block_queue(int max_size = 1000) {
        if(max_size <= 0) exit(-1);
        m_max_size = max_size;
        m_array = new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    void clear() {
        m_mutex.lock(); //加锁
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock(); //解锁
    }

    ~block_queue() {
        m_mutex.lock(); //加锁
        if(m_array != NULL) delete [] m_array;
        m_mutex.unlock(); //解锁
    }
    //判断队列是否满了
    bool full() {
        m_mutex.lock(); //加锁
        if(m_size >= m_max_size) {
            m_mutex.unlock(); //解锁
            return true;
        }
        m_mutex.unlock(); //解锁
        return false;
    }
    //判断队列是否为空
    bool empty() {
        m_mutex.lock(); //加锁
        if(m_size == 0) {
            m_mutex.unlock(); //解锁
            return true;
        }
        m_mutex.unlock(); //解锁
        return false;
    }
    //返回队首元素
    bool front(T &value) {
        m_mutex.lock(); //加锁
        if(m_size == 0) {
            m_mutex.unlock(); //解锁
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock(); //解锁
        return true;
    }
    //返回队尾元素
    bool back(T &value) {
        m_mutex.lock(); //加锁
        if(m_size == 0) {
            m_mutex.unlock(); //解锁
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock(); //解锁
        return true;
    }
    int size() {
        int tmp = 0;
        m_mutex.lock(); //加锁
        tmp = m_size;
        m_mutex.unlock(); //解锁
        return tmp;
    }
    int max_size() {
        int tmp = 0;
        m_mutex.lock(); //加锁
        tmp = m_max_size;
        m_mutex.unlock(); //解锁
        return tmp;
    }
    //往队列添加元素，需要将所有使用队列的线程先唤醒
    //当有元素push进队列，相当于生产者生产了一个元素
    //若当前没有线程等待条件变量，则唤醒无意义
    bool push(const T &item) {
        m_mutex.lock(); //加锁
        if(m_size >= m_max_size) {
            //以广播的方式唤醒所有等待目标条件变量的线程
            m_cond.broadcast();
            m_mutex.unlock(); //解锁
            return false;
        }
        //将新增数据放在循环数组的对应位置
        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;
        m_size++;
        //以广播的方式唤醒所有等待目标条件变量的线程
        m_cond.broadcast();
        m_mutex.unlock(); //解锁
        return true;
    }
    //pop时，如果当前队列没有元素，将会等待条件变量
    bool pop(T &item) {
        m_mutex.lock(); //加锁
        //多个消费者，需要用while
        while(m_size <= 0) {
            //当重新抢到互斥锁，m_cond.wait返回0
            if(!m_cond.wait(m_mutex.get())) {
                m_mutex.unlock(); //解锁
                return false;
            }
        }
        //取出队首元素
        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock(); //解锁
        return true;
    }
    //增加超时处理的pop函数（项目中没用到）
    bool pop(T &item, int ms_timeout) {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        m_mutex.lock(); //加锁
        if(m_size <= 0) {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if(!m_cond.timewait(m_mutex.get(), t)) {
                m_mutex.unlock(); //解锁
                return false;
            }
        }
        if(m_size <= 0) {
            m_mutex.unlock(); //解锁
            return false;
        }
        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock(); //解锁
        return true;
        
    }

private:
    locker m_mutex; //互斥锁
    cond m_cond; //信号量

    T *m_array; //数组指针
    int m_size; //当前大小
    int m_max_size; //容量
    int m_front; //队首元素下标（没有时为-1）
    int m_back; //队尾元素下标（没有时为-1）
};
#endif