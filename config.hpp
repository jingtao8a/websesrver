/*
 * @Descripttion: 
 * @version: 
 * @Author: yuxintao
 * @Date: 2022-05-03 21:30:42
 * @LastEditors: yuxintao
 * @LastEditTime: 2022-05-04 09:05:14
 */
#ifndef CONFIG_HPP
#define CONFIG_HPP
#include <unistd.h>
#include <stdlib.h>

class Config{
    public:
        Config();
        void parse_arg(int argc, char* argv[]);

    public:
        int PORT;//端口号
        int LOGWrite;//日志写入方式
        int TRIGMode;//触发组合模式
        int LISTENTrigmode; //listenfd触发模式
        int CONNTrigmode; //connfd触发模式
        int OPT_LINGER;//优雅关闭连接
        int sql_num;//数据库连接池数量
        int thread_num; //线程池内的线程数量
        int close_log;//是否关闭日志
        int actor_model;//并发模型选择
};


#endif