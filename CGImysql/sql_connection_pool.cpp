#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool() {
    m_CurConn = 0;
    m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance() {
    static connection_pool connPool;
    return &connPool;
}

//构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log) {
    //初始化数据库信息
    m_url = url;
    m_Port = Port;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DBName;
    m_close_log = close_log;

    //创建MaxConn条数据库连接
    for(int i = 0; i < MaxConn; i++) {
        MYSQL *con = NULL;
        con = mysql_init(con);

        if(con == NULL) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }

        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

        if(con == NULL) {
            LOG_ERROR("MySQL Error");
            exit(1);
        }
        connList.push_back(con); //放入连接池备用
        ++m_FreeConn; //更新空闲连接数量
    }
    reserve = sem(m_FreeConn); //将信号量初始化为最大连接数
    m_MaxConn = m_FreeConn;
}

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection() {
    MYSQL *con = NULL;
    if(0 == connList.size()) return NULL;
    reserve.wait(); //取出连接，信号量原子减1，为0则阻塞等待（线程大于连接池数量时）
    lock.lock(); //加锁
    con = connList.front();
    connList.pop_front();
    --m_FreeConn;
    ++m_CurConn;
    lock.unlock();//解锁
    return con;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con) {
    if(NULL == con) return false;
    lock.lock(); //加锁
    connList.push_back(con);
    ++m_FreeConn;
    --m_CurConn;
    lock.unlock();//解锁
    reserve.post(); //信号量原子加一
    return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool() {
    lock.lock(); //加锁
    if(connList.size() > 0) {
        //通过迭代器遍历，关闭数据库连接
        list<MYSQL *>::iterator it;
        for(it = connList.begin(); it != connList.end(); ++it) {
            MYSQL *con = *it;
            mysql_close(con);
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();//清空list
    }
    lock.unlock();//解锁
}

//获取当前空闲的连接数
int connection_pool::GetFreeConn() {
    return this->m_FreeConn;
}

//RAII机制销毁连接池（通过析构函数自动调用DestroyPool()）
connection_pool::~connection_pool() {
    DestroyPool();
}


//不直接调用获取和释放连接的接口，将其封装起来，通过RAII机制进行获取和释放
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool) {
    *SQL = connPool->GetConnection();
    conRAII = *SQL;
    poolRAII = connPool;
}
connectionRAII::~connectionRAII() {
    poolRAII->ReleaseConnection(conRAII);
}