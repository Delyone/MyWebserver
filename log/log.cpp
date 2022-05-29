#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log() {
    m_count = 0; //日志行数记录
    m_is_async = false; //默认同步
}
Log::~Log() {
    if(m_fp != NULL) fclose(m_fp);
}
//初始化，异步需要设置阻塞队列长度，同步不用
//实现日志创建，写入方式的判断
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size) {
    //如果设置了max_queue_size，则设置为异步
    if(max_queue_size >= 1) {
        m_is_async = true; //设置为异步
        m_log_queue = new block_queue<string>(max_queue_size); //创建并设置阻塞队列长度
        pthread_t tid;
        //flush_log_thread为回调函数，这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;             //关闭日志
    m_log_buf_size = log_buf_size;       //日志缓冲区大小
    m_buf = new char[m_log_buf_size];    //要输出的内容
    memset(m_buf, '\0', m_log_buf_size); //m_buf全部填充0
    m_split_lines = split_lines;         //日志最大行数

    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    //从前往后找到第一个/的位置
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};
    //若输入的文件名没有/，则直接将时间+文件名作为日志名
    //相当于自定义日志名
    if(p == NULL) {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else {
        strcpy(log_name, p + 1); //将/的位置往后移动一个位置，然后复制到logname中
        //dir_name相当于./ ; p - file_name + 1是文件所在路径文件夹的长度
        strncpy(dir_name, file_name, p - file_name + 1); 
        //后面的参数跟format有关
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    m_fp = fopen(log_full_name, "a");
    if(m_fp == NULL) return false;
    return true;
}

//将输出内容按照标准格式整理：格式化时间+格式化内容
//完成写入日志文件的具体内容，如日志分级，分文件，格式化输出内容
void Log::write_log(int level, const char *format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    //日志分级
    switch (level) {
    case 0:
        strcpy(s, "[debug]:");//调试用
        break;
    case 1:
        strcpy(s, "[info]:");//报告系统当前状态
        break;
    case 2:
        strcpy(s, "[warn]:");//警告，调试用
        break;
    case 3:
        strcpy(s, "[erro]:");//输出系统错误信息
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    
    m_mutex.lock(); //加锁
    m_count++;//更新现有行数
    //写入一个log
    //日志不是今天或写入的日志行数是最大行的倍数时
    if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};
        //格式化日志名中的时间部分
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
        //如果时间不是今天，创建今天的日志，更新m_today和m_count
        if(m_today != my_tm.tm_mday) {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        //超过了最大行，在之前的日志名基础上加后缀，m_count / m_split_lines
        else {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");       
    }
    m_mutex.unlock(); //解锁

    va_list valst;
    //将传入的format参数赋值给valst，便于格式化输出
    va_start(valst, format);
    string log_str;
    m_mutex.lock(); //加锁
    //写入的具体 时间+内容 格式
    //时间格式化，snprintf成功返回写字符总数（不包括结尾null字符）
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    //内容格式化，用于向字符串中打印数据，数据格式用户自定义，返回写入到字符数组str中的字符个数（不包含终止符）
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;
    m_mutex.unlock(); //解锁
    //若异步且队列不满，则将日志信息加入阻塞队列
    if(m_is_async && !m_log_queue->full()) m_log_queue->push(log_str);
    //同步，则加锁向文件中写
    else {
        m_mutex.lock(); //加锁
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock(); //解锁
    }
    va_end(valst);
}

//强制刷新缓冲区
void Log::flush(void) {
    m_mutex.lock(); //加锁
    fflush(m_fp); //强制刷新写入流缓冲区，避免连续多次输出到控制台导致的错误
    m_mutex.unlock(); //解锁
}
