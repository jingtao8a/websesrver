/*
 * @Descripttion: 
 * @version: 
 * @Author: yuxintao
 * @Date: 2022-05-03 21:30:56
 * @LastEditors: yuxintao
 * @LastEditTime: 2022-05-04 17:07:11
 */

#include "config.hpp"

Config::Config() {
    this->PORT = 8888;//默认端口号8888 p
    this->LOGWrite = 0;//默认日志写入方式为同步 l
    this->TRIGMode = 0;//触发组合模式，默认listenfd LT + connfd LT m
    /*
    LT + LT 0
    LT + ET 1
    ET + LT 2
    ET + Et 3
    */
    this->OPT_LINGER = 0;//默认不使用(优雅关闭) o
    this->sql_num = 8; //数据库池的连接数量 s
    this->thread_num = 8;//线程池的线程数量 t
    this->close_log = 0;//日志默认不关闭 c
    this->actor_model = 0; //默认是proactor a
}

void Config::parse_arg(int argc, char** argv) {
    int opt;
    const char* str = "p:l:m:o:s:t:c:a:";
    while ((opt = getopt(argc, argv, str)) != -1) {
        switch (opt) {
            case 'p':
                this->PORT = atoi(optarg);
                break;
            case 'l':
                this->LOGWrite = atoi(optarg);
                break;
            case 'm':
                this->TRIGMode = atoi(optarg);
                break;
            case 'o':
                this->OPT_LINGER = atoi(optarg);
                break;
            case 's':
                this->sql_num = atoi(optarg);
                break;
            case 't':
                this->thread_num = atoi(optarg);
                break;
            case 'c':
                this->close_log = atoi(optarg);
                break;
            case 'a':
                this->actor_model = atoi(optarg);
                break;
        }
    }
}