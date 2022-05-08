#include "timer.hpp"

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

sort_timer_lst::~sort_timer_lst() {
    util_timer* tmp = this->head;
    while (tmp) {
        this->head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer* timer) {
    if (!timer) return;
    if (!this->head) {
        this->head = this->tail = timer;
        return;
    }
    if (timer->expire < this->head->expire) {
        timer->next = this->head;
        this->head->prev = timer;
        this->head = timer;
        return;
    }
    this->add_timer(timer, head);
}

void sort_timer_lst::add_timer(util_timer* timer, util_timer* lst_head) {
    util_timer* prev = lst_head;
    util_timer* tmp = prev->next;
    while (tmp) {
        if (tmp->expire > timer->expire) {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
    }
    if (!tmp) {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        this->tail = timer;
    }
}

void sort_timer_lst::adjust_timer(util_timer* timer) {
    if (!timer) return;
    util_timer* tmp = timer->next;
    if (!tmp || (timer->expire < tmp->expire)) return;
    if (timer == this->head) {
        this->head = this->head->next;
        this->head->prev = NULL;
        timer->next = NULL;
        this->add_timer(timer, this->head);
    } else {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        this->add_timer(timer, tmp);
    }
}

void sort_timer_lst::del_timer(util_timer* timer) {
    if (!timer) return;
    if (timer == this->head && this->head == this->tail) {
        delete timer;
        this->head = this->tail = NULL;
        return;
    }
    if (timer == this->head) {
        this->head = this->head->next;
        this->head->prev = NULL;
        delete timer;
        return;
    }
    if (timer = this->tail) {
        this->tail = this->tail->prev;
        this->tail->next = NULL;
        delete timer;
        return;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

void sort_timer_lst::tick() {//将超时的用户关闭
    if (!this->head) return;
    time_t cur = time(NULL);
    util_timer* tmp = this->head;
    while (tmp) {
        if (cur < tmp->expire) break;
        cb_func(tmp->user_data);//将connfd移出epoll,并且关闭连接(超时处理)
        
        this->head = tmp->next;
        if (this->head) {
            this->head->prev = NULL;
        } else {
            this->tail = NULL;
        }
        delete tmp;
        tmp = this->head;
    }
}

//从内核事件表删除描述符
void Utils::removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//将事件重置为EPOLLSHOT
void Utils::modfd(int epollfd, int fd, int ev, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;
    if (TRIGMode == 1)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_short, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;
    //EDPOLLRDHUP仅意味着对等方关闭了其连接或仅关闭了一半的连接。
    //如果只关闭一半，则流套接字将变为单向只写连接。 写入连接可能仍然是，但是在消耗了任何(可能的)可读数据之后，将关闭对连接的读取
    if (TRIGMode == 1)//ET模式
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;//EPOLLRDHUP在对端关闭时会触发
    else//LT模式
        event.events = EPOLLIN | EPOLLRDHUP;//EPOLLRDHUP在对端关闭时会触发
    if (one_short)//是否只监听这个套接字一次，若只监听一次，下次还需监听，又要调用addfd
        event.events |= EPOLLONESHOT;
    
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    Utils::setnonblocking(fd);
}

//static信号处理函数
void Utils::sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
    send(Utils::u_pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

void Utils::addsig(int sig, void(*handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void Utils::show_error(int connfd, const char* info) {
    send(connfd, info, strlen(info), 0);//发回消息给客户端
    close(connfd);//关闭套接字
}

void Utils::init(int timeslot) {
    this->m_TIMESLOT = timeslot;
}

void Utils::timer_handler() {
    this->m_timer_lst.tick();
    alarm(this->m_TIMESLOT);
}

void cb_func(client_data* user_data) {
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;//用户连接减1 和http_conn的init相对应
}
