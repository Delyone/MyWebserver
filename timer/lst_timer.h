#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../log/log.h"

//连接资源结构体成员需要用到定时器类，提前声明
class util_timer;

//连接资源
struct client_data
{
    sockaddr_in address; //客户端socket地址
    int sockfd; //socket文件描述符
    util_timer *timer; //定时器
};

//定时器类（链表结点）
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}
public:
    time_t expire; //超时时间（= 浏览器和服务器连接时刻 + 固定时间TIMESLOT），是绝对时间
    void (* cb_func)(client_data *); //回调函数
    client_data *user_data; //连接资源
    util_timer *prev; //前向定时器
    util_timer *next; //后继定时器
};

//定时器容器类(升序双向链表结构)
class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();
    void add_timer(util_timer *timer); //添加定时器
    //调整定时器，任务发生变化时，调整定时器在链表中的位置
    void adjust_timer(util_timer *timer);
    void del_timer(util_timer *timer);//删除定时器
    void tick(); //定时任务处理函数
private:
    //主要用于调整链表内部结点
    void add_timer(util_timer *timer, util_timer *lst_head);
    //头尾结点没有意义，仅仅统一方便调整？（但是根据删除的代码感觉是有用的结点？）
    util_timer *head; //头部结点
    util_timer *tail; //尾结点
};

//定时器更高层次封装
class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);
    int setnonblocking(int fd); //对文件描述符设置非阻塞
    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);
    
    static void sig_handler(int sig); //信号处理函数
    void addsig(int sig, void(handler)(int), bool restart = true);//设置信号函数
    void timer_handler(); //定时处理任务，重新定时以不断触发SIGALRM信号
    void show_error(int connfd, const char *info);
public:
    static int *u_pipefd;
    sort_timer_lst m_timer_lst; //创建定时器容器链表
    static int u_epollfd;
    int m_TIMESLOT; //固定时间
};

//定时器回调函数
void cb_func(client_data *user_data);

#endif