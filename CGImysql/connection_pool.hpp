/*
 * @Descripttion: 
 * @version: 
 * @Author: yuxintao
 * @Date: 2022-05-03 16:27:32
 * @LastEditors: yuxintao
 * @LastEditTime: 2022-05-07 20:27:07
 */
#ifndef CONNECTION_POOL_HPP
#define CONNECTION_POOL_HPP

#include <mysql/mysql.h>
#include <stdio.h>
#include <error.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <iostream>
#include <list>
#include "../lock/lock.hpp"
#include "../log/log.hpp"

//单例模式
class connection_pool {
    public:
        std::string m_url; //localhost
        std::string m_User; //登入数据库用户名
        std::string m_PassWord; //登入数据库密码
        std::string m_DatabaseName; //使用数据库的名称
        int m_Port; //数据库端口号,默认3306
        int m_MaxConn; //最大连接数
        int m_CurConn; //当前已经使用的连接数
        int m_FreeConn; // 当前空闲连接数
        int m_close_log; //日志开关
        
        locker lock;
        sem reserve;
        std::list<MYSQL*> connList; //连接池 工作队列

    public:
        MYSQL* GetConnection();//获得连接
        bool ReleaseConnection(MYSQL* conn); //释放连接
        void DestroyPool();
        void init(std::string url, std::string User, std::string PassWord, std::string DBName, 
                    int Port, int MaxConn, int close_log);
    private:
        connection_pool() = default;
        ~connection_pool();
    public:
        //单例模式
    static connection_pool* GetInstance() {
        static connection_pool connPool;
        return &connPool;
    }
};


class connectionRAII {
    public:
        connectionRAII(MYSQL **con, connection_pool* connPool) {
            *con = connPool->GetConnection();
            this->conRAII = *con;
            this->poolRAII = connPool;
        }

        ~connectionRAII() {
            this->poolRAII->ReleaseConnection(this->conRAII);
        }
    private:
        MYSQL *conRAII;
        connection_pool *poolRAII;
};
#endif