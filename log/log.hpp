/*
 * @Descripttion: 
 * @version: 
 * @Author: yuxintao
 * @Date: 2022-05-03 20:07:14
 * @LastEditors: yuxintao
 * @LastEditTime: 2022-05-07 20:43:30
 */
#ifndef LOG_HPP
#define LOG_HPP

#include <stdio.h>
#include <string.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include "block_queue.hpp"

//单例模式
class Log{
    public:
        //单例模式
        static Log* get_instance() {
            static Log instance;
            return &instance;
        }
        //初始化相应参数
        void init(const char* file_name, int close_log, int log_buf_size = 8192, int split_lines = 50000000, int max_queue_size = 0);
        //同步或异步写日志(同步直接往文件里写，异步往阻塞队列里加)
        void write_log(int level, const char* format, ...);
        //清空缓冲区
        void flush();
    
    private:
        Log() = default;
        ~Log();
        
        //异步写日志时用于开启线程
        static void* flush_log_thread(void* args) {
            Log::get_instance()->async_write_log();
            return NULL;
        }

        void async_write_log() {//异步写日志
            std::string single_log;
            while (m_log_queue->pop(single_log)) {
                 //多线程互斥访问资源this->m_fp，需加锁
                this->m_mutex.lock();
                fputs(single_log.c_str(), this->m_fp);
                this->m_mutex.unlock();
            }
        }

    private:
        char dir_name[128];//路径名
        char log_name[128];//log文件名
        
        int m_split_lines;//日志最大行数,一篇日志如果超过这个数字，就需要重新打开另一篇日志了
        
        int m_log_buf_size;//日志缓冲区大小
        char* m_buf;//缓冲区指针

        long long m_count;//日志行数记录
        int m_today;//日志按天分类，记录当前时间是哪一天

        FILE* m_fp;//打开log文件的指针
        bool m_is_async;//是否同步写标志
        int m_close_log;//关闭日志标志
        block_queue<std::string>* m_log_queue;//阻塞队列用于实现日志异步写
        locker m_mutex;//互斥锁,用于读写文件时的加锁
};

#define LOG_DEBUG(format, ...) if (this->m_close_log == 0) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if (this->m_close_log == 0) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if (this->m_close_log == 0) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if (this->m_close_log == 0) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif