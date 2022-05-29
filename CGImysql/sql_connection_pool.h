#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool {
public:
    MYSQL *GetConnection();             //获取数据库连接
    bool ReleaseConnection(MYSQL *conn);//释放连接
    int GetFreeConn();                  //获取连接
    void DestroyPool();                 //销毁所有连接

    //局部静态变量单例模式（懒汉）
    static connection_pool *GetInstance();
    void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log);
private:
    connection_pool();
    ~connection_pool();
    int m_MaxConn; //最大连接数
    int m_CurConn; //当前已经使用的连接数（本项目没用到）
    int m_FreeConn;//当前空闲连接数（本项目没用到，因为有信号量）
    locker lock;   //互斥锁
    list<MYSQL *> connList; //连接池
    sem reserve;   //信号量

public:
    string m_url;          //主机地址
    string m_Port;         //数据库端口号
    string m_User;         //登录数据库用户名
    string m_PassWord;     //登录数据库密码
    string m_DatabaseName; //使用数据库名
    int m_close_log;       //日志开关
};

//将数据库连接的获取和释放通过RAII机制封装，避免手动释放
//因为连接池创建后，在服务器运行过程不会调用析构函数；
//所以多一层封装，超出作用域时，connectionRAII类自动调用析构函数，释放连接
class connectionRAII {
public:
    //数据库连接本身是指针类型，通过双指针才能修改MYSQL参数
    connectionRAII(MYSQL **con, connection_pool *connPool);
    ~connectionRAII();
private:
    MYSQL *conRAII;
    connection_pool *poolRAII;
};
#endif