#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
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
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn {
public:
    static const int FILENAME_LEN = 200; //读取文件名称大小
    static const int READ_BUFFER_SIZE = 2048; //读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024; //写缓冲区大小
    //枚举（enum)报文的请求方法
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATH};
    //主状态机的状态（按照状态调用响应函数解析，parse_request_line，parse_headers，parse_content）
    enum CHECK_STATE {CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};
    //报文解析结果（请求不完整，需要继续读取请求报文数据； 获得完整的HTTP请求； 有语法错误； 请求资源不存在; 
                //请求资源禁止访问； 请求资源正常访问； 服务器内部错误； 关闭连接）
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST,
                    FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};
    //从状态机状态，LINE_OK（读取到\r\n，完整读取一行）,LINE_BAD（提交语法有误）,LINE_OPEN（新数据到达，读取的行不完整）
    enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};
public:
    http_conn() {}
    ~http_conn() {}
public:
    //初始化套接字
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user,string passwd, string sqlname);
    void close_conn(bool real_close = true); //关闭HTTP连接
    void process();
    bool read_once(); //读取浏览器端发来的全部数据
    bool write(); //响应报文写入函数
    sockaddr_in *get_address() {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool); //CGI使用线程池初始化数据库读取表
    int timer_flag;
    int improv;

private:
    void init(); //重载，初始化新接受的连接
    HTTP_CODE process_read(); //从m_read_buf读取并处理请求报文
    bool process_write(HTTP_CODE ret); //向m_write_buf写入响应报文数据
    HTTP_CODE parse_request_line(char *text);//主状态机解析请求行
    HTTP_CODE parse_headers(char *text);//主状态机解析请求头部
    HTTP_CODE parse_content(char *text);//主状态机解析请求内容
    HTTP_CODE do_request(); //生成响应报文
    //m_start_line是已经解析的字符
    //get_line用于将指针向后偏移，指向未处理的字符
    char *get_line() {return m_read_buf + m_start_line;};
    LINE_STATUS parse_line(); //从状态机读取一行，分析是请求报文的那一部分
    void unmap(); //释放mmap创建的内存空间
    //根据响应报文格式生成8个部分，下列函数由do_request调用（然而没看到？实际是process_write）
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status,const char *title);
    bool add_headers(int content_len);
    bool add_content_type();
    bool add_content_length(int content_len);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    int m_state;// 读为0，写为1

private:
    int m_sockfd;
    sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE]; //存储读取的请求报文数据（缓冲区）
    int m_read_idx; //m_read_buf中数据的最后一个字节的下一个位置
    int m_checked_idx; //m_read_buf读取的位置
    int m_start_line; //m_read_buf已经解析的字符个数
    char m_write_buf[WRITE_BUFFER_SIZE]; //存储发出的响应报文数据（缓冲区）
    int m_write_idx; //buffer中的长度
    CHECK_STATE m_check_state; //主状态机的状态
    METHOD m_method; //请求方法

    //解析请求报文中对应的6个变量
    char m_real_file[FILENAME_LEN]; //存储读取文件的名称
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;


    char *m_file_address; //读取服务器上的文件地址
    struct stat m_file_stat; 
    struct iovec m_iv[2]; //io向量机制iovec
    int m_iv_count;
    int cgi;//是否启用的POST
    char *m_string;//存储请求头数据
    int bytes_to_send; //剩余发送字节数
    int bytes_have_send; //已发送字节数
    char *doc_root;

    map<string,string> m_users;
    int m_TRIGMode; //epoll工作模式：0-LT+LT；1-LT+ET；2-ET+LT；3-ET+ET
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif