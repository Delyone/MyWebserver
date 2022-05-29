#ifndef LOCKER_H //即 if not defined,防止编译时头文件重复包含而冲突
#define LOCKER_H

#include<exception> //异常类的基类
#include<pthread.h> //线程，有的函数被互斥量和条件变量使用
#include<semaphore.h> //操作系统提供的信号量（及函数）

//线程同步机制封装类
//锁机制的功能：实现多线程同步，通过锁机制，确保任一时刻只能有一个线程能进入关键代码
//这里封装的意义：
//1.当对象（锁）创建时，自动调用构造函数，当对象超出作用域时自动调用析构函数，实现RAII
//2.原函数名很长，封装后简洁

//信号量
class sem
{
public:
    //构造函数
    sem() {
        //初始化
        if(sem_init(&m_sem, 0, 0) != 0) {
            //抛出异常将终止当前的函数，并把控制权转移给能处理该异常的代码
            throw std::exception();
        }
    }
    sem(int num) {
        if(sem_init(&m_sem, 0, num) != 0) {
            throw std::exception();
        }
    }
    //析构函数
    ~sem() {
        //销毁信号量
        sem_destroy(&m_sem);
    }
    //等待信号量
    bool wait() {
        //sem_wait函数将以原子操作方式将信号量减一,信号量为0时,sem_wait阻塞（即P操作）
        return sem_wait(&m_sem) == 0;
    }
    //增加信号量
    bool post() {
        //sem_post函数以原子操作方式将信号量加一,信号量大于0时,唤醒调用sem_post的线程（即V操作）
        return sem_post(&m_sem) == 0;
    }
private:
    sem_t m_sem;
};

//互斥量（互斥锁）mutex
class locker {
public:
    locker() {
        if(pthread_mutex_init(&m_mutex,NULL) != 0) {
            throw std::exception();
        }
    }
    ~locker() {
        //销毁互斥锁
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock() {
        //以原子操作方式给互斥锁加锁
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;//解锁
    }
    pthread_mutex_t *get() { //返回互斥量的指针
        return &m_mutex;
    }
private:
    pthread_mutex_t m_mutex;
};

//条件变量
class cond {
public:
    cond() {
        if(pthread_cond_init(&m_cond,NULL) != 0) {
            throw std::exception();
        }
    }
    ~cond() {
        pthread_cond_destroy(&m_cond);
    }
    //用于等待目标条件变量，传入加锁的互斥锁，所以函数内部有一次解锁和加锁的操作，
    bool wait(pthread_mutex_t *m_mutex) {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);//传入m_mutex前的操作
        ret = pthread_cond_wait(&m_cond,m_mutex);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool timewait(pthread_mutex_t *m_mutex,struct timespec t) {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond,m_mutex,&t);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast() { //以广播的方式唤醒所有等待目标条件变量的线程
        return pthread_cond_broadcast(&m_cond) == 0;
    }
private:
    //static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};
#endif