/*
 * @Descripttion: 
 * @version: 
 * @Author: yuxintao
 * @Date: 2022-05-03 21:30:35
 * @LastEditors: yuxintao
 * @LastEditTime: 2022-05-07 19:43:10
 */
#include "config.hpp"
#include "webserver.hpp"

#include <iostream>
#define D std::cout << __FUNCTION__ << "\t" <<__LINE__ << "\t" << __FILE__  << std::endl;

int main(int argc, char** argv) {
    //命令行解析
    Config config;
    config.parse_arg(argc, argv);

    webserver server;
    
    std::string user = "debian-sys-maint";
    std::string passwd = "CighasqWNOi1bu7h";
    std::string databasename = "yourdb";
    
    //初始化
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite, 
                config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num, 
                config.close_log, config.actor_model);
    
    server.trig_mode();

    server.log_write();

    server.sql_pool();

    server.thread_pool();

    server.eventListen();

    server.eventLoop();
    
    return 0;
}