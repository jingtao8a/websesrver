/*
 * @Descripttion: 
 * @version: 
 * @Author: yuxintao
 * @Date: 2022-05-05 12:12:27
 * @LastEditors: yuxintao
 * @LastEditTime: 2022-05-08 14:48:47
 */

#include "http_conn.hpp"
#include <iostream>
#define D std::cout << __FUNCTION__ << "\t" <<__LINE__ << "\t" << __FILE__  << std::endl;
#define P(arg) std::cout << arg << std::endl; 

static const char* ok_200_title = "OK";
static const char* error_400_title = "Bad Request";
static const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
static const char* error_403_title = "Forbidden";
static const char* error_403_form = "You do not have permission to get file form this server.\n";
static const char* error_404_title = "Not Found";
static const char* error_404_form = "The requested file was not found on this server.\n";
static const char* error_500_title = "Internal Error";
static const char* error_500_form = "There was an unusual problem serving the request file.\n";


int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;
locker http_conn::m_lock;
std::map<std::string, std::string> http_conn::m_user;

void http_conn::init(int sockfd, struct sockaddr_in addr, char* root, int TRIGMode, int close_log) {
    this->m_sockfd = sockfd;
    this->m_address =addr;
    this->doc_root = root;
    this->m_TRIGMode = TRIGMode;//connfd的模式LT / ET
    this->m_close_log = close_log;
    
    Utils::addfd(this->m_epollfd, sockfd, true, m_TRIGMode);//将套接字加入epollfd，为onceshot，监听读事件
    http_conn::m_user_count++;//用户连接加1

    init();
}

void http_conn::init() {
    this->mysql = NULL;
    this->bytes_to_send = 0;
    this->bytes_have_send = 0;
    this->m_check_state = CHECK_STATE_REQUESTLINE;
    this->m_linger = false;
    this->m_method = GET;
    this->m_url = 0;
    this->m_version = 0;
    this->m_content_length = 0;
    this->m_host = 0;

    this->m_start_line = 0;
    this->m_checked_idx = 0;
    this->m_read_idx = 0;
    this->m_write_idx = 0;

    this->cgi = 0;
    this->m_state = 0;
    this->timer_flag = 0;
    this->improv = 0;

    memset(this->m_read_buf, 0, http_conn::READ_BUFFER_SIZE);
    memset(this->m_write_buf, 0, http_conn::WRITE_BUFFER_SIZE);
    memset(this->m_real_file, 0, http_conn::FILENAME_LEN);
}

void http_conn::initmysql_result(connection_pool* connPool) {
    MYSQL* mysql = connPool->GetConnection();

    connectionRAII(&mysql, connPool);

    mysql_query(mysql, "SELECT username, password FROM user"); 
    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD* fields = mysql_fetch_fields(result);
    
    //从结果集中取下一行，将对应的用户名和密码存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        std::string temp1(row[0]);
        std::string temp2(row[1]);
        http_conn::m_user[temp1] = temp2;
    }
}

void http_conn::close_conn(bool real_close) {
    if (real_close && (this->m_sockfd != -1)) {
        printf("close %d\n", this->m_sockfd);
        fflush(stdout);
        Utils::removefd(this->m_epollfd, this->m_sockfd);//移除epollfd
        this->m_sockfd = -1;
        http_conn::m_user_count--;
    }
}

bool http_conn::read_once() {
    if (this->m_read_idx >= http_conn::READ_BUFFER_SIZE) {//读缓冲区溢出,一般不会发生
        return false;
    }
    int bytes_read = 0;

    //LT模式,不需要一次性读完
    if (this->m_TRIGMode == 0) {
        bytes_read = recv(this->m_sockfd, this->m_read_buf + this->m_read_idx, READ_BUFFER_SIZE, 0);
        if (bytes_read <= 0) return false;//出错,一般不发生
        this->m_read_idx += bytes_read;
        return true;
    } else {//ET模式需要一次性将数据读完
        while (true) {
            bytes_read = recv(this->m_sockfd, this->m_read_buf + this->m_read_idx, http_conn::READ_BUFFER_SIZE - this->m_read_idx, 0);
            if (bytes_read = -1) { 
                if (errno == EAGAIN || errno == EWOULDBLOCK)//表示读缓冲区中数据已经读完
                    break;
                return false;//连接异常,一般不发生
            } else if (bytes_read == 0) {//连接断开
                return false;
            }
            this->m_read_idx += bytes_read;
        }
        return true;
    }
}

void http_conn::process() {
    HTTP_CODE read_ret = process_read();//解析请求报文
    
    //表示请求不完整, 需要继续接受请求数据
    if (read_ret == NO_REQUEST) {
        Utils::modfd(this->m_epollfd, this->m_sockfd, EPOLLIN, this->m_TRIGMode);//将监听套接字重新加入epoll，监听读事件
        return;
    }

    //请求已经接收完毕，现在根据解析结果写相应报文
    bool write_ret = process_write(read_ret);
    if (write_ret == false) {//返回false,一般不会发生
        this->close_conn();//解析结果不对，这样会将该套接字移出epoll，当定时器超时时会删除连接
        return;
    }
    //已经准备好发送相应报文了
    Utils::modfd(this->m_epollfd, this->m_sockfd, EPOLLOUT, this->m_TRIGMode);//将监听套接字重新加入epoll，监听写事件
}

//从状态机，用于分析一行的内容，如果取得一整行，即以/r/n结尾的行返回LINE_OK,同时将/r/n修改为\0\0
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for (; this->m_checked_idx < this->m_read_idx; ++this->m_checked_idx) {
        temp = this->m_read_buf[this->m_checked_idx];
        if (temp == '\r') {
            if ((this->m_checked_idx + 1) == this->m_read_idx)
                return LINE_OPEN;
            else if (this->m_read_buf[this->m_checked_idx + 1] == '\n') {
                this->m_read_buf[this->m_checked_idx++] = '\0';
                this->m_read_buf[this->m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (this->m_checked_idx > 1 && this->m_read_buf[this->m_checked_idx - 1] == '\r') {
                this->m_read_buf[this->m_checked_idx - 1] = '\0';
                this->m_read_buf[this->m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

//process read
http_conn::HTTP_CODE http_conn::process_read() {
    //初始化从状态机状态、HTTP请求解析结果
    LINE_STATUS line_status;
    HTTP_CODE ret;
    char* text = 0;
    
    while ((this->m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)) {//先由从状态机解析parse_line
        text = get_line();
        P(text)
        this->m_start_line = this->m_checked_idx;
        LOG_INFO("%s", text);//将从状态机解析的这一行数据写入日志
        switch (this->m_check_state) {
            case CHECK_STATE_REQUESTLINE:
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            case CHECK_STATE_HEADER:
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) {
                    D
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST) {//如果是请求解析成功,开始do_request,否则接着继续解析请求报文
                    return do_request();//开始处理
                }
                break;
            case CHECK_STATE_CONTENT:
                ret = parse_content(text);
                if (ret == GET_REQUEST) {//如果请求解析成功,开始开始do_request
                    return do_request();//开始处理
                }
                line_status = LINE_OPEN;
                break;
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(this->m_real_file, this->doc_root);
    int len = strlen(doc_root);
    
    const char* p = strchr(this->m_url, '/');
    //cgi = 1表明这是一个cgi请求
    if (this->cgi == 1 && (p[1] == '2' || p[1] == '3')) {
        //根据标志判断是登入检测还是注册检测
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, this->m_url + 2);
        strncpy(this->m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);//释放堆空间
        
        //将用户名和密码提取出来
        char name[100], password[100];
        int i;
        for (i = 5; this->m_string[i] != '&'; ++i) {
            name[i - 5] = this->m_string[i];
        }
        name[i - 5] = '\0';
        int j = 0;
        for (i = i + 10; this->m_string[i] != '\0'; ++i, ++j) {
            password[j] = this->m_string[i];
        }
        password[j] = '\0';
        if (p[1] == '3') {//如果是注册,检查是否有重名的,若没有重名，进行增加数据
            char* sql_insert = (char* )malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, password) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "','");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (http_conn::m_user.find(name) == http_conn::m_user.end()) {//没有重名
                http_conn::m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                http_conn::m_user.insert({name, password});
                http_conn::m_lock.lock();

                if (!res) {
                    strcpy(this->m_url, "/log.html");//注册成功,返回登入页面
                } else {
                    strcpy(this->m_url, "/registerError.html");
                }
            } else {//重名
                strcpy(this->m_url, "/registerError.html");
            }
        } else if (p[1] == '2') {//登入
            if (http_conn::m_user.find(name) != this->m_user.end() && http_conn::m_user[name] == password) {
                strcpy(this->m_url, "/welcome.html");
            } else {
                strcpy(this->m_url, "/logError.html");
            }
        }
    }

    if (p[1] == '0') {
        char *m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(this->m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else if (p[1] == '1') {
        char *m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(this->m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else if (p[1] == '4') {
        char *m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/stu_id.html");
        strncpy(this->m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    } else if (p[1] == '5') {
        char *m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/photo.html");
        strncpy(this->m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }else {
        strncpy(this->m_real_file + len, this->m_url, FILENAME_LEN - len - 1);
    }

    if (stat(this->m_real_file, &this->m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    if (!(this->m_file_stat.st_mode & S_IROTH)) return FORBIDDEN_REQUEST;
    if (S_ISDIR(this->m_file_stat.st_mode)) {
        D
        return BAD_REQUEST;
    }

    int fd = open(this->m_real_file, O_RDONLY);
    this->m_file_address = (char*)mmap(0, this->m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);//开启文件内存映射
    close(fd);
    return FILE_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    this->m_url = strpbrk(text, " \t");
    //找不到url,直接返回报文BAD_REQUEST解析结果
    
    if (this->m_url == 0) return BAD_REQUEST;

    *m_url++ = '\0';
    char* method = text;
    //只接受GET或POST请求方式，其余方式都看作错误请求，直接返回报文BAD_REQUEST解析结果
    if (strcasecmp(method, "GET") == 0) this->m_method = GET;
    else if (strcasecmp(method, "POST") == 0) {
        this->m_method = POST;
        this->cgi = 1;
    } else {
        return BAD_REQUEST;
    }
    this->m_url += strspn(this->m_url, " \t");
    this->m_version = strpbrk(this->m_url, " \t");
    //找不到http版本号,直接返回报文BAD_REQUEST解析结果
    if (this->m_version == 0) return BAD_REQUEST;

    *m_version++ = '\0';
    this->m_version += strspn(this->m_version, " \t");
   
    //如果http版本号不为1.1，直接返回报文BAD_REQUEST解析结果
    if (strncasecmp(this->m_version, "HTTP/1.1", 8) != 0) return BAD_REQUEST;
   
    //锁定uri
    if (strncasecmp(this->m_url, "http://", 7) == 0) {
        this->m_url += 7;
        this->m_url = strchr(this->m_url, '/');
    }
    //锁定uri失败，直接返回报文BAD_REQUEST解析结果
    if (!this->m_url || this->m_url[0] != '/') return BAD_REQUEST;

    //当url为/时，显示默认网页
    if (strlen(m_url) == 1)
        strcat(this->m_url, "judge.html");
    this->m_check_state = CHECK_STATE_HEADER;//将主状态机状态改为CHECK_STATE_HEADER
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
    if (text[0] == '\0') {//表示已经解析到请求头的最后一行了
        if (this->m_content_length != 0) {//若content_length字段不为0,则为POST请求
            this->m_check_state = CHECK_STATE_CONTENT;//将主状态机状态改为CHECK_STATE_CONTENT
            return NO_REQUEST;//请求还未解析完，主状态机将进入CHECK_STATE_CONTENT
        }
        return GET_REQUEST;//请求已经解析完毕了,因为请求体没有数据
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            this->m_linger = true;
        }
    } else if (strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        this->m_content_length = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        this->m_host = text;
    } else {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;//请求还未解析完
}

http_conn::HTTP_CODE http_conn::parse_content(char* text) {
    text[this->m_content_length] = '\0';
    //POST请求中最后为输入的用户名和密码
    this->m_string = text;
    return GET_REQUEST;//请求解析完毕，已经确定了这是POST请求
}


//process_write
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form)) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form)) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title);
            if (this->m_file_stat.st_size != 0) {
                add_headers(this->m_file_stat.st_size);
                //响应状态行和响应头部的长度
                this->m_iv[0].iov_base = this->m_write_buf;
                this->m_iv[0].iov_len = this->m_write_idx;
                //响应文件的长度
                this->m_iv[1].iov_base = this->m_file_address;//文件内存映射地址
                this->m_iv[1].iov_len = this->m_file_stat.st_size;//文件大小
                this->m_iv_count = 2;
                //需发送的长度为
                this->bytes_to_send = this->m_write_idx + this->m_file_stat.st_size;
                return true;
            } else {
                //发送空文件
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string)) return false;
            }
        default:
            return false;
    }
    //发送相应报文，只使用一个向量
    this->m_iv[0].iov_base = this->m_write_buf;
    this->m_iv[0].iov_len = this->m_write_idx;
    this->m_iv_count = 1;
    this->bytes_to_send = this->m_write_idx;
    return true;
}

bool http_conn::add_response(const char* format, ...) {
    if (this->m_write_idx >= WRITE_BUFFER_SIZE) return false; //写出缓冲区满
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(this->m_write_buf + this->m_write_idx, WRITE_BUFFER_SIZE - this->m_write_idx - 1, format, arg_list);
    if (len >= WRITE_BUFFER_SIZE - 1 - this->m_write_idx) {
        va_end(arg_list);
        return false;
    }
    this->m_write_idx += len;
    va_end(arg_list);
    LOG_INFO("request: %s", this->m_write_buf);
    return true;
}

bool http_conn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len) {
    return add_content_length(content_len) && add_linger() && add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length:%d\r\n", content_len);
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_linger() {
    return add_response("Connection:%s\r\n", (this->m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char* content) {
    return add_response("%s", content);
}

void http_conn::unmap() {
    if (this->m_file_address) {
        munmap(this->m_file_address, this->m_file_stat.st_size);//关闭文件内存映射
        this->m_file_address = 0;
    }
}

bool http_conn::write() {
    int temp = 0;

    while (1) {
        temp = writev(this->m_sockfd, this->m_iv, this->m_iv_count);
        if (temp < 0) {
            if (errno == EAGAIN) {//写缓冲区满了
                Utils::modfd(this->m_epollfd, this->m_sockfd, EPOLLOUT, this->m_TRIGMode);//重新监听该套接字的写事件
                return true;
            }
            unmap();//一般不发生
            return false;
        }
        bytes_have_send += temp;
        bytes_to_send -= temp;
        if (bytes_have_send >= this->m_iv[0].iov_len) {//FILE_REQUEST中，已经将状态行和响应头发送完了，只剩下文件内容未发送完毕
        //这同时也说明这是一个回应文件的响应
            this->m_iv[0].iov_len = 0;
            this->m_iv[1].iov_base = this->m_file_address + (bytes_have_send - this->m_write_idx);
            this->m_iv[1].iov_len = bytes_to_send;
        } else {
            this->m_iv[0].iov_base = this->m_write_buf + this->bytes_have_send;
            this->m_iv[0].iov_len = this->m_iv[0].iov_len - this->bytes_have_send;
        }

        if (bytes_to_send <= 0) {
            unmap();//关闭文件内存映射
            Utils::modfd(this->m_epollfd, this->m_sockfd, EPOLLIN, m_TRIGMode);//发送数据完毕重新监听该套接字的读事件

            if (this->m_linger) {//keep alive
                init();//准备接受新的请求
                return true;
            } else {
                return false;//表示请求报文的Connect为close,短连接,HTTP/1.1不存在这种情况
            }
        }
    }
}
