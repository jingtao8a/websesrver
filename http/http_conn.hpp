#ifndef HTTP_CONN_HPP
#define HTTP_CONN_HPP

#include <unistd.h>
#include <signal.h>
#include <sys/epoll.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <map>
#include "../CGImysql/connection_pool.hpp"
#include "../timer/timer.hpp"
#include "../log/log.hpp"

class http_conn{
    public:
        //设置读取文件的名称大小
        static const int FILENAME_LEN = 200;
        //设置读缓冲区大小
        static const int READ_BUFFER_SIZE = 2048;
        //设置写缓冲区大小
        static const int WRITE_BUFFER_SIZE = 1024;
        //报文的请求方法，本项目只用到GET和POST
        enum METHOD{
            GET, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATH
        };
        //主状态机的状态
        enum CHECK_STATE{
            CHECK_STATE_REQUESTLINE,CHECK_STATE_HEADER, CHECK_STATE_CONTENT
        };
        //报文解析的结果
        enum HTTP_CODE{
            NO_REQUEST, //请求还未解析完
            GET_REQUEST, //请求已经解析成功
            BAD_REQUEST,//400 请求错误
            NO_RESOURCE,//404 请求资源不存在
            FORBIDDEN_REQUEST,//403 请求被拒绝
            FILE_REQUEST, //200 文件请求
            INTERNAL_ERROR,//500 服务器内部错误
            CLOSED_CONNECTION                           
        };
        //从状态机的结果
        enum LINE_STATUS{
            LINE_OK, LINE_BAD, LINE_OPEN
        };
    
    public:
        //初始化套接字地址，函数内部调用私有方法init
        void init(int sockfd, struct sockaddr_in addr, char* root, int TRIGMode, int close_log);
  
        //关闭http连接
        void close_conn(bool real_close = true);
        
        void process();
        //读取浏览器端发来的全部数据
        bool read_once();
        //相应报文写入函数
        bool write();
        struct sockaddr_in* get_address() {
            return &this->m_address;
        }
        //同步线程初始化数据库读取表
        static void initmysql_result(connection_pool* connPool);
    private:
        void init();
        //从m_read_buf读缓冲区读取，并处理请求报文
        HTTP_CODE process_read();
        //向m_write_buf写入相应报文数据
        bool process_write(HTTP_CODE ret);
        //主状态机解析报文中的请求行数据
        HTTP_CODE parse_request_line(char* text);
        //主状态机解析报文中的请求头数据
        HTTP_CODE parse_headers(char* text);
        //主状态解析报文中的请求内容
        HTTP_CODE parse_content(char* text);
        //生成响应报文
        HTTP_CODE do_request();

        //get_line用于将指针向后偏移，指向未处理的字符
        char* get_line() {
            return this->m_read_buf + this->m_start_line;//m_start_line是已经解析的字符
        }
        //从状态机读取一行，分析时请求报文的哪一部分
        LINE_STATUS parse_line();

        void unmap();

        //根据响应报文格式，生成对应8个部分，以下函数均由process_write调用
        bool add_response(const char* format, ...);
        bool add_status_line(int status, const char* title);
        bool add_content(const char* content);
        bool add_headers(int content_length);
        bool add_content_type();
        bool add_content_length(int content_length);
        bool add_linger();
        bool add_blank_line();
    public:
        static int m_epollfd;//epoll套接字
        static int m_user_count;//用户连接数量
        int timer_flag;
        int improv;
    private:
    //init 1
        int m_sockfd;//处理连接的服务端套接字
        struct sockaddr_in m_address;//客户端地址
        char* doc_root;//默认资源目录
        int m_TRIGMode;
        int m_close_log;

    //init 2
    public:
        MYSQL* mysql;//用户获得的数据库连接
    private:
        int bytes_to_send;//剩余发送字节数
        int bytes_have_send;//已发送字节数

        char m_read_buf[READ_BUFFER_SIZE];//读缓冲区
        int m_read_idx;//m_read_buf中数据的最后一个字节的下一个位置,指示m_read_buf中的长度
        int m_checked_idx;//m_read_buf读取的位置m_checked_idx,从状态机使用的游标
        int m_start_line;//m_read_buf中已经解析的字符个数,get_line函数使用的游标

        char m_write_buf[WRITE_BUFFER_SIZE];//写缓冲区
        int m_write_idx;//指示m_write_buf中的长度
        //主状态机的状态
        CHECK_STATE m_check_state;//初始化为CHECK_STATE_REQUESTLINE

        //请求方法
        METHOD m_method;
        //以下为解析请求报文中对应的6个变量
        char m_real_file[FILENAME_LEN];//请求文件名
        char* m_url;//请求url
        char* m_version;//请求http版本
        char* m_host;//主机host
        int m_content_length;
        bool m_linger;//初始化为false，keep-alive or close

        char* m_file_address; //内存映射的文件地址,
        struct stat m_file_stat;//文件信息
        struct iovec m_iv[2]; //io向量机制iovec，用于writev
        int m_iv_count;//writev的第三个参数
        
        //当请求方法为POST时,将请求体数据放存储下来
        char* m_string; //存储请求体数据
        
        int cgi; //是否启用的POST,在解析请求方法时，如果为POST，那么cgi被设置为1
    public:
        int m_state;//reactor模式下,工作线程根据这个判断读写操作,0为读, 1为写,proactor模式下不需要

        static std::map<std::string, std::string> m_user;
        static locker m_lock;
};

#endif