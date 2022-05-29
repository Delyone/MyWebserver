#include "lst_timer.h"
#include "../http/http_conn.h"

//定时器容器类相关函数
sort_timer_lst::sort_timer_lst() {
    head = NULL;
    tail = NULL;
}
sort_timer_lst::~sort_timer_lst() {
    util_timer *tmp = head;
    //循环链表删除第一个，直到完全销毁
    while(tmp) {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}
//添加定时器，升序插入
void sort_timer_lst::add_timer(util_timer *timer) {
    if(!timer) return; //无定时器返回
    if(!head) {
        head = tail = timer;
        return;
    }
    //传入定时器时间比容器头部还小，插在头部前面
    if(timer->expire < head->expire) {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head); //调用重载函数，调整内部结点
}
//调整定时器，任务发生变化时（例如有数据传输，时间改变），调整定时器在链表中的位置
void sort_timer_lst::adjust_timer(util_timer *timer) {
    if(!timer) return;
    util_timer *tmp = timer->next;
    //被调整的定时器在链表尾部，不调整
    //定时器超时值仍小于下一个定时器超时值，不调整
    if(!tmp || (timer->expire < tmp->expire)) return;
    //被调整的定时器是链表头结点，取出再插入
    if(timer == head) {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    //被调整的定时器在内部，将定时器取出再插入
    else {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}
//删除定时器，时间O（1）
void sort_timer_lst::del_timer(util_timer *timer) {
    if(!timer) return;
    //链表中只有一个定时器
    if((timer == head) && (timer == tail)) {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    //被删除的定时器是头结点
    if(timer == head) {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    //被删除的定时器是尾结点
    if(timer == tail) {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    //被删除的定时器在链表内部
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}
//定时任务处理函数
void sort_timer_lst::tick() {
    if(!head) return;

    time_t cur = time(NULL);//获取当前时间
    util_timer *tmp = head;
    //遍历定时器链表
    while(tmp) {
        //当前时间小于定时器的超时时间就退出循环
        if(cur < tmp->expire) break;
        //当前定时器到期，调用回调函数，执行定时事件
        tmp->cb_func(tmp->user_data);
        //处理后的定时器删除，并重置头结点
        head = tmp->next;
        if(head) head->prev = NULL;
        delete tmp;
        tmp = head;
    }
}
//添加定时器的重载版本，时间O（n）
void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head) {
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;
    //遍历当前结点之后的链表，按照超时值找到目标位置
    while(tmp) {
        if(timer->expire < tmp->expire) {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    //遍历完发现，需要放在尾结点
    if(!tmp) {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}


void Utils::init(int timeslot) {
    m_TIMESLOT = timeslot;
}
//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}
//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if(1 == TRIGMode) event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else event.events = EPOLLIN | EPOLLRDHUP;

    if(one_shot) event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd); //对文件描述符设置非阻塞
}
//信号处理函数（仅发送信号值，不处理信号对应逻辑，缩短异步执行时间，减少对主程序的影响）
void Utils::sig_handler(int sig) {
    //为保证函数的可重入性，保留原来的errno
    //可重入性表示中断后可再次进入该函数，环境变量和之前相同，不会丢失数据
    int save_errno = errno;
    int msg = sig;
    //将信号值从管道写端写入，传输字符类型
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}
//设置信号函数
void Utils::addsig(int sig, void(handler)(int), bool restart) {
    struct sigaction sa; //创建sigaction结构体变量
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart) sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask); //将所有信号添加到信号集
    assert(sigaction(sig, &sa, NULL) != -1); //执行函数
}
//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler() {
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}
void Utils::show_error(int connfd, const char *info) {
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;


//定时器回调函数（从内核事件表删除非活动socket上的注册事件，并关闭）
class Utils;
void cb_func(client_data *user_data) {
    //删除。。。
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);//关闭文件描述符
    http_conn::m_user_count--; //减少连接数
}
