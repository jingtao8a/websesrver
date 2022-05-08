/*
 * @Descripttion: 
 * @version: 
 * @Author: yuxintao
 * @Date: 2022-05-03 20:07:20
 * @LastEditors: yuxintao
 * @LastEditTime: 2022-05-08 14:54:02
 */
#include "log.hpp"

Log::~Log() {
    if (this->m_fp != NULL) {
        fclose(this->m_fp);
    }
    if (this->m_buf) delete[] this->m_buf;
    if (this->m_log_queue) delete this->m_log_queue;
}

void Log::init(const char* file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size) {
    this->m_count = 0;//日志行数,初始值为0
    this->m_is_async = false;//默认同步写日志

    if (max_queue_size >= 1) {
        this->m_is_async = true;//若阻塞队列长度大于1,设为异步
        this->m_log_queue = new block_queue<std::string>(max_queue_size);
        //创建线程，异步写日志
        pthread_t tid;
        pthread_create(&tid, NULL, Log::flush_log_thread, NULL);
        pthread_detach(tid);
    }

    this->m_close_log = close_log;//关闭日志标志
    this->m_log_buf_size = log_buf_size;//缓冲区大小
    this->m_buf = new char[log_buf_size];
    memset(m_buf, 0, sizeof(m_buf));
    this->m_split_lines = split_lines;//日志最大行数
    
    
    time_t t = time(NULL);//当前时间,long long
    struct tm* sys_tm = localtime(&t);//转化为年月日
    struct tm my_tm = *sys_tm;

    const char* p = strchr(file_name, '/');
    char log_full_name[256] = {0};//文件名
    //在文件名上加上时间戳
    int ret;
    if (p == NULL) {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    } else {
        strcpy(this->log_name, p + 1);
        strncpy(this->dir_name, file_name, p - file_name + 1);
        ret = snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", this->dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, this->log_name);
        if (ret < 0) exit(1);
    }
    
    this->m_today = my_tm.tm_mday;
    if ((this->m_fp = fopen(log_full_name, "a")) == NULL) {
        char tmp_buf[100];
        sprintf(tmp_buf, "%s %d error", __FILE__, __LINE__);
        perror(tmp_buf);
        exit(1);
    }
}

void Log::write_log(int level, const char* format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);//获取当前时间(由sec,usec表示)
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;//当前时间转为年月日表示形式

    char s[16] = {0};//存储的信息类型
    //判断写入的是什么信息
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    //多个线程访问互斥资源this->m_buf或者this->m_today或者this->m_count或者this->m_fp,加锁
    this->m_mutex.lock();
    int ret;
    this->m_count++;//日记行数加1
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {//若已经不是同一天，或当前日志的行数已经超过要求的行数
        char new_log[256] = {0};
        fflush(this->m_fp);
        fclose(this->m_fp);
        char tail[16] = {0};
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
        if (this->m_today != my_tm.tm_mday) {//若不是同一天
            ret = snprintf(new_log, 255, "%s%s%s", this->dir_name, tail, this->log_name);
            if (ret < 0) exit(1);
            this->m_today = my_tm.tm_mday;
            this->m_count = 0;
        } else {//若是同一天
            ret = snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
            if (ret < 0) exit(1);
        }
        this->m_fp = fopen(new_log, "a");
    }
    
    va_list valst;
    va_start(valst, format);
    
    std::string log_str;
    int n = snprintf(this->m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s", 
                    my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                    my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);//时间戳
    int m = vsnprintf(this->m_buf + n, m_log_buf_size - 1, format, valst);
    va_end(valst);
    this->m_buf[m + n] = '\n';
    this->m_buf[m + n + 1] = '\0';

    log_str = std::string(this->m_buf);

    this->m_mutex.unlock();

    if (this->m_is_async && !this->m_log_queue->full()) {
        this->m_log_queue->push(log_str);
    } else {
        //多线程互斥访问资源this->m_fp，需加锁
        this->m_mutex.lock();
        fputs(log_str.c_str(), this->m_fp);
        this->m_mutex.unlock();
    }
}

void Log::flush() {
    //多线程互斥访问资源this->m_fp，需加锁
    this->m_mutex.lock();
    fflush(this->m_fp);
    this->m_mutex.unlock();
}