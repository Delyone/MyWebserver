
TinyWebServer
===============
Linux系统下C++轻量级Web服务器，接收浏览器消息并响应请求，实践网络编程。

* 基于 **线程池 + 非阻塞socket + epoll(支持ET和LT)**实现**Reactor和Proactor**两种事件处理模式的并发模型
* 使用**有限状态机**解析HTTP**GET和POST**请求报文，分别实现客户端访问**图片、视频**文件和**注册、登录**功能
* 使用SIGALRM信号触发定时器，实现**升序链表定时器容器**，关闭超时非活动连接
* 使用**MySQL**数据库存储用户信息，建立基于**RAII机制的连接池**，提高逻辑单元对数据的访问效率
* 实现**同步/异步（阻塞队列）日志系统**，记录服务器运行状态
* 解析**multi/form-data**类型的POST请求，实现**文件上传**功能
* 利用**jsoncpp**生成json数据，展示**文件列表**
* 部署在云端，经Webbench压力测试可实现**近万的并发连接**数据交换

目录
-----

| [目录树](#目录树) | [框架](#框架) | [Demo演示](#Demo演示) |[更新日志](#更新日志) |[To Do](#ToDo) | [快速运行](#快速运行) | [个性化运行](#个性化运行) | [压力测试](#压力测试) | [致谢](#致谢) |

目录树
-------------
```
.
├── CGImysql           数据库连接池
│   ├── sql_connection_pool.cpp
│   ├── sql_connection_pool.h
├── http               http连接请求处理类
│   ├── http_conn.cpp
│   ├── http_conn.h
├── lock               线程同步机制包装类
│   ├── locker.h
├── log                同步/异步日志系统
│   ├── block_queue.h
│   ├── log.cpp
│   ├── log.h
├── root               静态资源
├── test_presure       简易服务器压力测试
├── threadpool         半同步/半反应堆线程池
│   ├── threadpool.h
├── timer              定时器处理非活动连接
│   ├── lst_timer.cpp
│   ├── lst_timer.h
├── build.sh           
├── config.cpp         配置
├── config.h           配置
├── main.cpp           主函数
├── makefile   
├── webserver.cpp          
└── webserver.h
```

框架
-------------
<div align=center><img src="http://ww1.sinaimg.cn/large/005TJ2c7ly1ge0j1atq5hj30g60lm0w4.jpg" height="765"/> </div>

Demo演示
----------
> * **点击即可体验→**[潮节呾吧](http://www.chaofest.xyz)

<div align=center><img src="https://github.com/Delyone/MyWebserver/blob/master/root/images/welcome.png" width="800"/> </div>


更新日志
-------
- [x] 解析multi/form-data类型的POST请求，实现文件上传功能
- [x] 利用jsoncpp生成json数据，展示文件列表

ToDo
-------
- [x] 利用标准库容器封装char，实现自动扩容的缓冲区
- [x] 根据文件列表下载文件


快速运行
------------
* 服务器测试环境
	* Ubuntu版本20.04
	* MySQL版本5.7.29（虚拟机）和8.0.29（云服务器）
	* jsoncpp

* 浏览器测试环境
	* Windows、Linux均可
	* Chrome
	* FireFox
	* 搜狗浏览器
    * 微信电脑端

* 如无cmake, 先[安装cmake3.8以上版本](https://blog.csdn.net/zjb18741809273/article/details/119192553)

* 配置安装jsoncpp [cmake编译jsoncpp](https://blog.csdn.net/qq_40199447/article/details/105379634) [报错解决](https://blog.csdn.net/qq_41821678/article/details/120331269)

* 测试前确认已安装MySQL数据库

    ```C++
    // 建立mydb库
    create database mydb;

    // 创建user表
    USE mydb;
    CREATE TABLE user(
        username char(50) NULL,
        passwd char(50) NULL
    )ENGINE=InnoDB;

    // 添加数据
    INSERT INTO user(username, passwd) VALUES('name', 'passwd');
    ```

* 修改main.cpp中的数据库初始化信息

    ```C++
    //数据库登录名(按实际修改),密码（按实际修改）,库名
    string user = "root";
    string passwd = "root";
    string databasename = "mydb";
    ```

* build

    ```C++
    sh ./build.sh
    ```

* 启动server

    ```C++
    ./server
    ```

* 浏览器端

    ```C++
    ip:9999
    ```

个性化运行
------

```C++
./server [-p port] [-l LOGWrite] [-m TRIGMode] [-o OPT_LINGER] [-s sql_num] [-t thread_num] [-c close_log] [-a actor_model]
```

温馨提示:以上参数不是非必须，不用全部使用，根据个人情况搭配选用即可.

* -p，自定义端口号
	* 默认9999
* -l，选择日志写入方式，默认同步写入
	* 0，同步写入
	* 1，异步写入
* -m，listenfd和connfd的模式组合，默认使用LT + LT
	* 0，表示使用LT + LT
	* 1，表示使用LT + ET
    * 2，表示使用ET + LT
    * 3，表示使用ET + ET
* -o，优雅关闭连接，默认不使用
	* 0，不使用
	* 1，使用
* -s，数据库连接数量
	* 默认为8
* -t，线程数量
	* 默认为8
* -c，关闭日志，默认打开
	* 0，打开日志
	* 1，关闭日志
* -a，选择反应堆模型，默认Proactor
	* 0，Proactor模型
	* 1，Reactor模型

测试示例命令与含义

```C++
./server -p 9007 -l 1 -m 0 -o 1 -s 10 -t 10 -c 1 -a 1
```

- [x] 端口9007
- [x] 异步写入日志
- [x] 使用LT + LT组合
- [x] 使用优雅关闭连接
- [x] 数据库连接池内有10条连接
- [x] 线程池内有10条线程
- [x] 关闭日志
- [x] Reactor反应堆模型


压力测试
-------------
测试环境：
> * x为云服务器：通用型S3 1核2G 1M带宽 40G云硬盘
> * 系统：Ubuntu 20.04 server 64bit

测试方法：
> * 关闭日志，使用80端口运行(不用端口只用ip就能直接访问): ./server -p 80 -c 1
> * 使用Webbench对服务器进行压力测试: ./webbench -c 9000 -t 5 http://ip (并发连接总数：9000;访问服务器时间：5s)
> * 对listenfd和connfd分别采用ET和LT模式

测试结果：
> * 均可实现9k+的并发连接，受限于云服务器的性能，在虚拟机中有6k+的QPS在此处只有500左右。

> * LT+LT
<div align=center><img src="https://github.com/Delyone/MyWebserver/blob/master/root/images/Proactor+LT+LT.png" width="500"/> </div>

> * LT+ET
<div align=center><img src="https://github.com/Delyone/MyWebserver/blob/master/root/images/Proactor+LT+ET.png" width="500"/> </div>

> * ET+LT
<div align=center><img src="https://github.com/Delyone/MyWebserver/blob/master/root/images/Proactor+ET+LT.png" width="500"/> </div>

> * ET+ET
<div align=center><img src="https://github.com/Delyone/MyWebserver/blob/master/root/images/Proactor+ET+ET.png" width="500"/> </div>


**注意：** 使用本项目的webbench进行压测时，若报错显示webbench命令找不到，将可执行文件webbench删除后，重新编译即可。


致谢
------------
* Linux高性能服务器编程，游双著.
* [牛客网Linux高并发服务器开发](https://www.nowcoder.com/courses/cover/live/504)
* [社长的TinyWebServer](https://github.com/qinguoyi/TinyWebServer)
* [Mark的WebServer](https://github.com/markparticle/WebServer)
* [Sakura的WebServer](https://github.com/Sakura1221/SimpleWebServer)
