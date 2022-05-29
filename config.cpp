#include "config.h"

Config::Config() {
    PORT = 9999;        //端口号,默认9999
    LOGWrite = 0;       //日志写入方式，默认同步
    TRIGMode = 0;       //触发组合模型,默认LT+LT
    LISTENTrigmode = 0; //listenfd触发模式,默认LT
    CONNTrigmode = 0;   //connfd触发模式,默认LT
    OPT_LINGER = 0;     //优雅关闭连接,默认不使用
    sql_num = 8;        //数据库连接池数量,默认8
    thread_num = 8;     //线程池内的线程数量,默认8
    close_log = 0;      //是否关闭日志,默认开启
    actor_model = 0;    //并发模型选择,默认proactor
}

void Config::parse_arg(int argc, char *argv[]) {
    int opt;
    const char *str = "p:l:m:o:s:t:c:a:";
    //getopt分析命令行参数
    while((opt = getopt(argc, argv, str)) != -1) {
        switch (opt)
        {
        case 'p':
        {
            PORT = atoi(optarg);
            break;
        }
        case 'l':
        {
            LOGWrite = atoi(optarg);
            break;
        }
        case 'm':
        {
            TRIGMode = atoi(optarg);
            break;
        }
        case 'o':
        {
            OPT_LINGER = atoi(optarg);
            break;
        }
        case 's':
        {
            sql_num = atoi(optarg);
            break;
        }
        case 't':
        {
            thread_num = atoi(optarg);
            break;
        }
        case 'c':
        {
            close_log = atoi(optarg);
            break;
        }
        case 'a':
        {
            actor_model = atoi(optarg);
            break;
        }       
        default:
            break;
        }
    }
}