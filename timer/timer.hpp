/*
 * @Descripttion: 
 * @version: 
 * @Author: yuxintao
 * @Date: 2022-05-04 19:55:09
 * @LastEditors: yuxintao
 * @LastEditTime: 2022-05-07 21:01:05
 */
#ifndef TIMER_HPP
#define TIMER_HPP
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <signal.h>
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
#include "../http/http_conn.hpp"

class util_timer;

struct client_data{
    struct sockaddr_in address;
    int sockfd;
    util_timer* timer;
};

class util_timer{
    public:
        time_t expire;//long long
        void (*cb_func)(client_data*);
        
        client_data* user_data;
        util_timer* prev;
        util_timer* next;
    public:
        util_timer() : prev(NULL), next(NULL) {}
};

class sort_timer_lst{
    public:
        sort_timer_lst() : head(NULL), tail(NULL) {}
        ~sort_timer_lst();

        void add_timer(util_timer* timer);//添加一个timer,保证双向链表的有序性
        void adjust_timer(util_timer* timer);//将指定的timer根据expire重新排列
        void del_timer(util_timer* timer);//将指定的timer删除
        void tick();//将超时的timer删除,并且移出epoll,close,http_conn::m_count--
    
    private:
        void add_timer(util_timer* timer, util_timer* lst_head);
    private:
        util_timer* head;
        util_timer* tail;
};

class Utils{
    public:
        static int *u_pipefd;
        static int u_epollfd;
        sort_timer_lst m_timer_lst;
        int m_TIMESLOT;//闹钟时间默认为5秒
    public:
        //从内核事件表删除描述符
        static void removefd(int epollfd, int fd);

        //对文件描述符设置非阻塞
        static int setnonblocking(int fd);
        
        //将内核事件表注册读事件，ET模式，选择性开启EPOLLONESHOT,最后默认将fd设置为非阻塞
        static void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);
        
        //将事件重置为EPOLLSHOT
        static void modfd(int epollfd, int fd, int ev, int TRIGMode);
        
        //信号处理函数,使用u_pipefd[1]发送sig给u_pipefd[0]
        static void sig_handler(int sig);
        
        //设置信号函数
        static void addsig(int sig, void(*handler)(int), bool restart = true);

        //发送错误消息给客户端
        static void show_error(int connfd, const char* info);

        void init(int timeslot);
        //定时处理任务，重新定时可以不断触发SIGALRM信号
        void timer_handler();//将超时连接删除，并重新设置闹钟
};

void cb_func(client_data* user_data);

#endif