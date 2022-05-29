#include "http_conn.h"
#include <mysql/mysql.h>
#include <fstream>


//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
//将数据库的用户名和密码载入到服务器的map中
map<string,string> users;

//CGI使用线程池初始化数据库读取表
void http_conn::initmysql_result(connection_pool *connPool) {
    //先从连接池取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql,connPool);

    //在user表中检索username,passwd数据，浏览器端输入
    if(mysql_query(mysql, "SELECT username,passwd FROM user")) {
        LOG_ERROR("SELECT error:%s\n",mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结果的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码存入map
    while(MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

//epoll相关代码：非阻塞模式，内核事件表注册事件，删除事件，重置EPOLLONESHOT事件
//一。对文件描述符设置非阻塞
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}
//二。将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if(1 == TRIGMode) event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else event.events = EPOLLIN | EPOLLRDHUP;

    if(one_shot) event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd); //对文件描述符设置非阻塞
}
//三。从内核时间表删除描述符
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}
//四。将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if(1 == TRIGMode) event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

//关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close) {
    if(real_close && (m_sockfd != -1)) {
        printf("close %d\n", m_sockfd); //cout<<"close "<<m_sockfd<<endl;
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

//初始化连接，外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode, 
                     int close_log, string user,string passwd, string sqlname) {
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++; //客户总量加一

    //浏览器连接重置时，可能是网站根目录出错，或http响应格式出错，或访问的文件内容为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;
    //复制字符串
    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

//初始化新接受的连接
void http_conn::init() {
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE; //默认为分析请求行状态
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

//从状态机，用于分析出一行内容（ps:主状态机负责对该行数据进行解析）
//返回值为行的读取状态，有LINE_OK（读取到\r\n，完整读取一行）,LINE_BAD（提交语法有误）,LINE_OPEN（新数据到达，读取的行不完整）
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for(; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if(temp == '\r') {
            //达到了buffer末尾还没有\n，说明要继续接收
            if((m_checked_idx + 1) == m_read_idx) return LINE_OPEN;
            else if(m_read_buf[m_checked_idx + 1] == '\n') {
                //完整读取一行后，将\r\n置为\0\0；并更新m_checked_idx以驱动主状态机解析
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n') {
            if(m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            } 
            return LINE_BAD;
        }    
    }
    return LINE_OPEN;
}

//循环读取客户数据，直至五数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once() {
    if(m_read_idx >= READ_BUFFER_SIZE) return false;
    int bytes_read = 0;

    //LT读取数据
    if(0 == m_TRIGMode) {
        //从套接字接收数据，存储在m_read_buf缓冲区
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        if(bytes_read <= 0) return false;
        return true;
    }
    //ET读取数据
    else {
        while(true) {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if(bytes_read == -1) {
                if(errno == EAGAIN || errno == EWOULDBLOCK) break;
                return false;
            }
            else if (bytes_read == 0) return false;
            m_read_idx += bytes_read;
        }
        return true;
    }
}

//解析http请求行，获得请求方法，目标url及http版本号
//strcasecmp/strncasecmp忽略大小写比较字符串,strncasecmp只比较参数1的前n个
http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
    m_url = strpbrk(text, " \t");//请求行中最先含有空格和\t任一字符的位置并返回
    if(!m_url) return BAD_REQUEST;
    *m_url++ = '\0';//将该位置改为\0，用于将前面数据取出
    char *method = text;
    //确定请求方式
    if(strcasecmp(method, "GET") == 0) m_method = GET;
    else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    }
    else return BAD_REQUEST;

    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if(!m_version) return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if(strcasecmp(m_version, "HTTP/1.1") != 0) return BAD_REQUEST;//仅支持HTTP/1.1

    if(strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if(strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if(!m_url || m_url[0] != '/') return BAD_REQUEST;
    //当url为/时，显示判断界面
    if(strlen(m_url) == 1) strcat(m_url, "judge.html");
    //请求行处理完毕，主状态机转移处理请求头
    m_check_state = CHECK_STATE_HEADER; 
    return NO_REQUEST;
}

//解析HTTP请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text) {
    if(text[0] == '\0') {
        //判断是get还是post请求
        if(m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;//post需要跳转到消息体处理状态
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    //解析请求头部连接字段
    else if(strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, "\t");//跳过空格和\t
        if(strcasecmp(text, "keep-alive") == 0) m_linger = true;//长连接
    }
    //解析请求头部内容长度字段
    else if(strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, "\t");
        m_content_length = atol(text);
    }
    //解析请求头部Host字段
    else if(strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, "\t");
        m_host = text;
    }
    else LOG_INFO("oop!unknow header: %s", text); //打印到日志
    return NO_REQUEST;
}

//主状态机解析请求内容,判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text) {
    if(m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        //POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//有限状态机的例子
//通过while循环，将主从状态机封装，对报文的每一行循环处理
http_conn::HTTP_CODE http_conn::process_read() {
    //初始化从状态机状态，http请求解析结果
    LINE_STATUS line_status = LINE_OK; //从状态机状态
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
    //注意是两个或关系的判断条件
    //GET请求只需要后一个条件；POST请求消息体末尾没有\r\n,需要通过主状态机来判断
    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)) {
        text = get_line();
        //m_start_line是每一个数据行在m_read_buf中起始位置
        //m_checked_idx是从状态机在m_read_buf中读取位置
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);
        //主状态机的三种状态转移逻辑
        switch(m_check_state) {
        case CHECK_STATE_REQUESTLINE: {
            ret = parse_request_line(text);
            if(ret == BAD_REQUEST) return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER: {
            ret = parse_headers(text);
            if(ret == BAD_REQUEST) return BAD_REQUEST;
            //完整解析GET请求后，跳转到报文响应函数
            else if(ret == GET_REQUEST) return do_request();
            break;
        }
        case CHECK_STATE_CONTENT: {
            ret = parse_content(text);
            //完整解析POST请求后，跳转到报文响应函数
            if(ret == GET_REQUEST) return do_request();
            line_status = LINE_OPEN;//更新line_status，避免再次进入循环
            break;
        }
        default: 
            return INTERNAL_ERROR;// 此外默认服务器错误
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, doc_root); //m_real_file赋值为网站根目录
    int len = strlen(doc_root);
    const char *p = strrchr(m_url, '/'); //找到m_url中/位置
    
    //处理cgi，登录和注册校验
    if(cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN -len - 1);
        free(m_url_real);

        //提取用户名和密码
        char name[100],password[100];
        int i;
        //&为分隔符，前面是用户名
        for(i = 5; m_string[i] != '&'; ++i) {
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0';
        //&为分隔符，后面是密码
        int j = 0;
        for(i = i + 10; m_string[i] != '\0'; ++i, ++j) {
            password[j] = m_string[i];
        }
        password[j] = '\0';

        //注册校验
        if(*(p + 1) == '3') {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            //不重名
            if(users.find(name) == users.end()) {
                m_lock.lock(); //加锁
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string,string>(name, password));
                m_lock.unlock();
                //校验成功，跳转登录页面
                if(!res) strcpy(m_url, "/log.html"); 
                //校验失败，跳转注册失败页面
                else strcpy(m_url, "/registerError.html");
            }
            //重名
            else strcpy(m_url, "/registerError.html");
        }

        //登录校验
        //如果表中能找到返回1，否则返回0
        else if(*(p + 1) == '2') {
            //找到
            if(users.find(name) != users.end() && users[name] == password) {
                strcpy(m_url, "/welcome.html");
            }
            else strcpy(m_url, "/logError.html");
        }
    }

    if(*(p + 1) == '0') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html"); //注册页面
        //将网站目录和/register.html进行拼接，更新到m_real_file
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if(*(p + 1) == '1') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html"); //登录页面
        //将网站目录和/log.html进行拼接，更新到m_real_file
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if(*(p + 1) == '5') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html"); //图片页面
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if(*(p + 1) == '6') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html"); //视频页面
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if(*(p + 1) == '7') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html"); //关注页面
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else strncpy(m_real_file + len, m_url, FILENAME_LEN -len - 1);

    //stat获取请求资源文件信息，获取成功则将信息更新到m_file_stat，失败返回
    if(stat(m_real_file, &m_file_stat) < 0) return NO_RESOURCE;
    //判断文件权限，是否可读，不可读返回
    if(!(m_file_stat.st_mode & S_IROTH)) return FORBIDDEN_REQUEST;
    //判断文件类型，如果是目录返回
    if(S_ISDIR(m_file_stat.st_mode)) return BAD_REQUEST;

    //只读方式获取文件描述符
    int fd = open(m_real_file, O_RDONLY);
    //将普通文件映射到内存逻辑地址，加快访问速度
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);//避免文件描述法浪费和占用
    return FILE_REQUEST;//文件存在且可以访问
}

//释放mmap创建的内存空间
void http_conn::unmap() {
    if(m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

//将响应报文发送给浏览器端
bool http_conn::write() {
    int temp = 0;
    if(bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }
    while(1) {
        //writev循环发送响应报文数据
        temp = writev(m_sockfd, m_iv, m_iv_count);
        //writev单次发送不成功
        if(temp < 0) {
            //写缓冲区满了
            if(errno == EAGAIN) {
                //注册写事件，等待下一次写事件触发（当写缓冲区从不可写变为可写，触发epollout)
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            //写缓冲区没满
            unmap();
            return false;
        }

        bytes_have_send += temp;//更新已发送字节
        bytes_to_send -= temp;
        //第一个iovec头部信息数据已发送完，发送第二个数据iovec
        if(bytes_have_send >= m_iv[0].iov_len) {
            //不再发送头部信息
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        //继续发送第一个iovec头部信息数据
        else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        //数据已全部发完
        if(bytes_to_send <= 0) {
            unmap();
            //在epoll树上重置EPOLLONESHOT事件
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            //浏览器请求为长连接
            if(m_linger) {
                init();//重新初始化HTTP对象
                return true;
            }
            else {
                return false;
            }

        }
    }
}

//添加响应报文，被后续各种添加函数所调用
bool http_conn::add_response(const char *format, ...) {
    if(m_write_idx >= WRITE_BUFFER_SIZE) return false;//写入内容超过m_write_buf大小则报错
    va_list arg_list; //定义可变参数列表
    va_start(arg_list,format); //将变量arg_list初始化为传入参数
    //将数据format从可变参数列表写入缓冲区写，返回写入数据的长度
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    //如果写入数据长度超过缓冲区剩余空间，则报错
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;//更新位置
    va_end(arg_list);//清空可变参数列表

    LOG_INFO("request:%s", m_write_buf);
    return true;
}

//添加状态行
bool http_conn::add_status_line(int status,const char *title) {
    return add_response("%s %d %s\r\n","HTTP/1.1", status, title);
}
//添加消息报头
bool http_conn::add_headers(int content_len) {
    return add_content_length(content_len) && add_linger() && add_blank_line();
}
//添加文本类型，这里是html
bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n","text/html");
}
//响应报文长度
bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length:%d\r\n",content_len);
}
//添加连接状态，通知浏览器端保持连接还是关闭
bool http_conn::add_linger() {
    return add_response("Connection:%s\r\n",(m_linger == true) ? "keep-alive" : "close");
}
//添加空行
bool http_conn::add_blank_line() {
    return add_response("%s","\r\n");
}
//添加文本content
bool http_conn::add_content(const char *content) {
    return add_response("%s",content);
}

bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
    case INTERNAL_ERROR: //内部错误
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if(!add_content(error_500_form)) return false;
        break;
    }
    case BAD_REQUEST: //报文语法错误
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if(!add_content(error_404_form)) return false;
        break;
    }  
    case FORBIDDEN_REQUEST: //资源没有访问权限
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if(!add_content(error_403_form)) return false;
        break;
    }  
    case FILE_REQUEST: //文件存在
    {
        add_status_line(200, ok_200_title);
        if(m_file_stat.st_size != 0) { //如果请求资源存在
            add_headers(m_file_stat.st_size);
            //第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            //第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            //发送的全部数据为 响应报文头部信息 和 文件大小
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else {
            //请求资源大小为0，返回空白html文件
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if(!add_content(ok_string)) return false;
        }
    } 
    default:
        return false;
    }
    //除FILE_REQUEST外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

//任务处理
void http_conn::process() {
    HTTP_CODE read_ret = process_read(); //报文解析
    if(read_ret == NO_REQUEST) {
        //注册并监听读事件（注意是EPOLLIN）
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    bool write_ret = process_write(read_ret);//报文响应
    if(!write_ret) close_conn();
    //注册并监听写事件（注意是EPOLLOUT）
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}