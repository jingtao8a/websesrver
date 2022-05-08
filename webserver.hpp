/*
 * @Descripttion: 
 * @version: 
 * @Author: yuxintao
 * @Date: 2022-05-04 10:35:10
 * @LastEditors: yuxintao
 * @LastEditTime: 2022-05-07 20:56:48
 */
#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/epoll.h>
#include "./threadpool/threadpool.hpp"
#include "./http/http_conn.hpp"

const int MAX_FD = 65536; //最大文件描述符个数
const int MAX_EVENT_NUMBER = 10000;//最大事件数
const int TIMESLOT = 5;//闹钟时间默认为5秒

class webserver{
    int m_port;//端口号
    int m_log_write;//日志写模式(同步或异步)
    
     //触发模式 start
    int m_TRIGMode;
    int m_LISTENTrigmode;
    int m_CONNTrigmode;
    //触发模式end

    int m_OPT_LINGER;////默认不使用(优雅关闭)
    
    //数据库池连接数量 start
    int m_sql_num;
    connection_pool * m_connPool;
    std::string m_user; //登入数据库的用户名
    std::string m_password; //登录数据库密码
    std::string m_databaseName;//使用的数据库名
    //数据库池连接数量 end

    //线程池线程数量 start
    int m_thread_num;
    threadpool<http_conn> *m_pool;
    //线程池数量 end

    int m_close_log;//日志关闭标志
    int m_actor_model;//线程池工作模式

    char* m_root;//web服务的根目录(当前目录下的root子目录),构造时初始化//这是默认的,可以手动更改资源所在目录

    int m_pipefd[2];//用于进程自己向自己发信号
    int m_epollfd;//epoll反应堆

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;//监听套接字
    
    http_conn* users;
    
//定时器相关
    client_data* users_timer;//连接的用户定时器，构造时初始化
    Utils utils;//工具对象

    public:
        webserver();
        ~webserver();
        void init(int port, std::string user, std::string passWord, std::string databaseName,
                int log_write, int opt_linger, int trigmode, int sql_num, 
                int thread_num, int close_log, int actor_model);
        void trig_mode();
        void log_write();
        void sql_pool();
        void thread_pool();
        void eventListen();
        void eventLoop();
    private:
        void timer(int connfd, struct sockaddr_in client_adress);
        void adjust_timer(util_timer* timer);
        void deal_timer(util_timer* timer, int sockfd);
        
        void dealclientdata();
        void dealwithsignal(bool &timeout, bool& stop_server);
        void dealwithread(int sockfd);
        void dealwithwrite(int sockfd);
};

#endif