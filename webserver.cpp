/*
 * @Descripttion: 
 * @version: 
 * @Author: yuxintao
 * @Date: 2022-05-04 10:35:19
 * @LastEditors: yuxintao
 * @LastEditTime: 2022-05-08 11:35:03
 */
#include "webserver.hpp"
#include <iostream>
#define D std::cout << __FUNCTION__ << "\t" <<__LINE__ << "\t" << __FILE__  << std::endl;
#define P(arg) std::cout << arg << std::endl; 

webserver::webserver() {
    
    //root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);//获得当前工作目录
    char root[6] = "/root";
    this->m_root = (char*)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(this->m_root, server_path);
    strcat(this->m_root, root);
 
    //MAX_FD个client_data对象
    this->users_timer = new client_data[MAX_FD];
    //MAX_FD个http_conn对象
    this->users = new http_conn[MAX_FD];
}

webserver::~webserver() {
    close(this->m_epollfd);
    close(this->m_listenfd);
    close(this->m_pipefd[1]);
    close(this->m_pipefd[0]);
    delete[] this->users;
    delete[] this->users_timer;
    delete[] this->m_pool;
    delete[] this->m_root;
}

void webserver::init(int port, std::string user, std::string passWord, std::string databaseName,
                int log_write, int opt_linger, int trigmode, int sql_num, 
                int thread_num, int close_log, int actor_model) {
    this->m_port = port;
    this->m_user = user;
    this->m_password = passWord;
    this->m_databaseName = databaseName;
    this->m_log_write = log_write;
    this->m_OPT_LINGER = opt_linger;
    this->m_TRIGMode = trigmode;
    this->m_sql_num = sql_num;
    this->m_thread_num = thread_num;
    this->m_close_log = close_log;
    this->m_actor_model = actor_model;
}

void webserver::trig_mode() {
    switch (this->m_TRIGMode) {
        case 0://LT + LT
            this->m_LISTENTrigmode = 0;
            this->m_CONNTrigmode = 0;
            break;
        case 1://LT + ET
            this->m_LISTENTrigmode = 0;
            this->m_CONNTrigmode = 1;
            break;
        case 2://ET + LT
            this->m_LISTENTrigmode = 1;
            this->m_CONNTrigmode = 0;
            break;
        case 3://ET + ET
            this->m_LISTENTrigmode = 1;
            this->m_CONNTrigmode = 1;
            break;
    }
}

void webserver::log_write() {
    if (this->m_close_log == 0) {
        if (this->m_log_write == 1) {//异步写日志
            Log::get_instance()->init("./ServerLog", this->m_close_log, 2000, 800000, 800);
        } else {//同步写日志
            Log::get_instance()->init("./ServerLog", this->m_close_log, 2000, 800000);
        }
    }
}

void webserver::sql_pool() {
    this->m_connPool = connection_pool::GetInstance();
    this->m_connPool->init("localhost", this->m_user, this->m_password, this->m_databaseName, 3306, this->m_sql_num, this->m_close_log);
    //初始化数据库读取表
    http_conn::initmysql_result(this->m_connPool);//将数据库中的name,password信息全都存储在http_conn::map中
}

void webserver::thread_pool() {
    this->m_pool = new threadpool<http_conn>(this->m_actor_model, this->m_connPool, this->m_thread_num);
}

void webserver::eventListen() {
//**************************socket bind listen start*****************************
    this->m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(this->m_listenfd >= 0);

    int flag = 1;
    setsockopt(this->m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));//地址重用，当服务器重新开启时，可以立刻使用相同的端口

    if (this->m_OPT_LINGER == 0) {
        struct linger tmp = {0, 1};
        //用于设置TCP连接关闭时的行为方式，就是关闭连接时，发送缓冲区的数据如何处理
        setsockopt(this->m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    } else {//使用优雅关闭连接
        struct linger tmp = {1, 1};
        setsockopt(this->m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(this->m_port);
    
    ret = bind(this->m_listenfd, (struct sockaddr*)&address, sizeof(address)); 
    assert(ret >= 0);
    ret = listen(this->m_listenfd, 5);
    assert(ret >= 0);
//**************************socket bind listen end*****************************************
    //epoll创建内核事件表
    this->m_epollfd = epoll_create(5);
    assert(this->m_epollfd != -1);
    http_conn::m_epollfd = this->m_epollfd;//初始化http_conn中的m_epollfd
    Utils::u_epollfd = this->m_epollfd;//初始化Utils中的epollfd,u_epollfd只在cb_func中调用过

    Utils::addfd(this->m_epollfd, this->m_listenfd, false, this->m_LISTENTrigmode);//not onceshot

/*
Linux实现了一个源自BSD的socketpair调用，可以实现在同一个文件描述符中进行读写的功能。
该系统调用能创建一对已连接的UNIX族socket。
在Linux中，完全可以把这一对socket当成pipe返回的文件描述符一样使用，唯一的区别就是这一对文件描述符中的任何一个都可读和可写
*/
    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, this->m_pipefd);
    assert(ret != -1);

    Utils::setnonblocking(this->m_pipefd[1]);//设置为非阻塞
    Utils::addfd(this->m_epollfd, this->m_pipefd[0], false, 0);//LT模式, 只要读缓冲区有数据就一直触发,not onceshot,
    
    Utils::u_pipefd = this->m_pipefd;

    //当一个进程向某个已收到RST的套接字执行写操作时，(对端已经close(fd)了)
    //（此时写操作返回EPIPE错误）内核向该进程发送一个SIGPIPE信号，该信号的默认行为是终止进程，因此进程必须捕获它以免不情愿地被终止；
    Utils::addsig(SIGPIPE, SIG_IGN);
    Utils::addsig(SIGALRM, Utils::sig_handler, false);//挂接SIGALRM信号处理函数 闹钟信号
    Utils::addsig(SIGTERM, Utils::sig_handler, false);//挂接SIGTERM信号处理函数 程序处理信号

    this->utils.init(TIMESLOT);//设置闹钟时间
    alarm(TIMESLOT);//设置5秒闹钟，之后该进程会收到一个SIGALRM信号,信号处理函数会触发并且发消息给pipefd[0]
}


void webserver::timer(int connfd, struct sockaddr_in client_address) {
    this->users[connfd].init(connfd, client_address, this->m_root, this->m_CONNTrigmode, this->m_close_log);

    this->users_timer[connfd].address = client_address;
    this->users_timer[connfd].sockfd = connfd;
    
    util_timer* timer = new util_timer();
    timer->user_data = &this->users_timer[connfd];
    timer->cb_func = cb_func;
    
    this->users_timer[connfd].timer = timer;
    
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;//设定该连接超时时间为3 * TIMESLOT

    this->utils.m_timer_lst.add_timer(timer);
}

void webserver::adjust_timer(util_timer* timer) {
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;//重新将该连接的定时器设置为3 * TIMESLOT
    this->utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

void webserver::deal_timer(util_timer* timer, int sockfd) {
    timer->cb_func(&this->users_timer[sockfd]);
    if (timer) {
        this->utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", sockfd);
}

void webserver::dealclientdata() {//accept
    struct sockaddr_in client_address;
    socklen_t client_addrlength;

    if (this->m_LISTENTrigmode == 0) {//LT模式
        int connfd = accept(this->m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
        if (connfd < 0) {//一般不发生
            LOG_ERROR("%s: errno is:%d", "accept error", errno);
            return;
        }
        if (http_conn::m_user_count >= MAX_FD) {
            Utils::show_error(connfd, "Internal server busy");//虽然建立了TCP连接，但直接关闭，请求客户太多
            LOG_ERROR("%s", "Internal server busy");
            return;
        }
        timer(connfd, client_address);//添加用户信息
    } else {//ET模式
        while (1) {
            int connfd = accept(this->m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
            if (connfd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)//表示已经没有连接请求了
                    return;
                LOG_ERROR("%s: errno is:%d", "accept error", errno);//一般不发生
                return;
            }
            if (http_conn::m_user_count >= MAX_FD) {
                Utils::show_error(connfd, "Internal server busy");//虽然建立好了TCP连接，但是超过了最大连接数量，只能关闭连接
                LOG_ERROR("%s", "Internal server busy");
                return;
            }
            timer(connfd, client_address);//添加用户信息
        }
    }
}

void webserver::dealwithsignal(bool &timeout, bool& stop_server) {
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(this->m_pipefd[0], signals, sizeof(signals), 0);
    
    for (int i = 0; i < ret; ++i) {
        switch(signals[i]) {
            case SIGALRM:
                timeout = true;
                break;
            case SIGTERM:                    
                stop_server = true;
                break;
        }
    }
    return;
}


void webserver::dealwithread(int sockfd) {
    util_timer* timer = this->users_timer[sockfd].timer;
    if (this->m_actor_model == 1) { // reactor模式
        if (timer) {
            adjust_timer(timer);//连接定时器更新
        }
        this->m_pool->append(&this->users[sockfd], 0);//state = 0, 读模式,由工作线程进行读

        while (true) {
            if (this->users[sockfd].improv == 1) {//表示工作线程将这个http_conn取出来，并且读了一次
                if (this->users[sockfd].timer_flag == 1) {//读出错
                    deal_timer(timer, sockfd);//将这个连接sockfd移出epoll，close，并且删除连接定时器
                    this->users[sockfd].timer_flag = 0;
                }
                this->users[sockfd].improv = 0;
                break;//退出循环
            }
        }
    } else {//proactor
        if (this->users[sockfd].read_once()) {//主线程进行一次读操作,再将处理请求放入thread_pool
            LOG_INFO("deal with the client (%s)", inet_ntoa(this->users[sockfd].get_address()->sin_addr));
            //监听到读事件，将事件放入请求队列
            this->m_pool->append_p(&this->users[sockfd]);
            if (timer) {
                adjust_timer(timer);//连接定时器更新
            }
        } else {
            deal_timer(timer, sockfd);//移出epoll，close，http::m_count--,删除定时器
        }
    }
}

void webserver::dealwithwrite(int sockfd) {
    util_timer* timer = this->users_timer[sockfd].timer;
    if (this->m_actor_model == 1) {//reactor
        if (timer) {
            adjust_timer(timer);//连接定时器更新
        }
        this->m_pool->append(users + sockfd, 1);//state = 1, 写模式,由工作线程进行写

        while (true) {
            if (this->users[sockfd].improv == 1) {//表示工作线程将这http_conn取出来，并且写了一次
                if (this->users[sockfd].timer_flag == 1) {
                    deal_timer(timer, sockfd);
                    this->users[sockfd].timer_flag = 0;
                }
                this->users[sockfd].improv = 0;
                break;
            }
        }
    } else {//proactor
        if (this->users[sockfd].write()) {//主线程进行写操作,一次性写完
            LOG_INFO("send data to the client(%s)", inet_ntoa(this->users[sockfd].get_address()->sin_addr));
            if (timer) {
                adjust_timer(timer);//连接定时器更新
            }
        } else {
            deal_timer(timer, sockfd);//移出epoll，close，http::m_count--,删除定时器
        }
    }
}

void webserver::eventLoop() {
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server) {
        int number = epoll_wait(this->m_epollfd, this->events, MAX_EVENT_NUMBER, -1);//阻塞调用,IO多路复用
        for (int i = 0; i < number; ++i) {
            int sockfd = events[i].data.fd;

            if (sockfd == m_listenfd) {//有连接请求,添加定时器，以及添加http_conn
                dealclientdata();
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {//对端关闭连接
                //服务器关闭连接
                util_timer* timer = this->users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            } else if ((sockfd == this->m_pipefd[0]) && (events[i].events & EPOLLIN)) {
                dealwithsignal(timeout, stop_server); 
            } else if (events[i].events & EPOLLIN) {//读事件触发
                dealwithread(sockfd);
            } else if (events[i].events & EPOLLOUT) {//写事件触发
                dealwithwrite(sockfd);
            }
        }
        if (timeout) {//定时器到了，即过了TIMESLOT
            this->utils.timer_handler();//将超时连接删除，并且再次设置TIMESLOT的闹钟，结果就是m_pipefd[0]每隔TIMESLOT将收到一个SIGALARM信号
            
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}
