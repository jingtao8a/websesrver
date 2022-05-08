/*
 * @Descripttion: 
 * @version: 
 * @Author: yuxintao
 * @Date: 2022-05-03 16:27:41
 * @LastEditors: yuxintao
 * @LastEditTime: 2022-05-07 20:30:51
 */

#include "connection_pool.hpp"

//class connection_pool

//私有成员函数
connection_pool::~connection_pool() {
    this->DestroyPool();
}

//公有成员函数
void connection_pool::init(std::string url, std::string User, std::string PassWord, std::string DBName, 
                            int Port, int MaxConn, int close_log) {   
    this->m_url = url;
    this->m_Port = Port;
    this->m_User = User;
    this->m_PassWord = PassWord;
    this->m_DatabaseName = DBName;
    this->m_close_log = close_log;
    this->m_CurConn = 0;
    this->m_FreeConn = 0;
    
    for (int i = 0; i < MaxConn; ++i) {
        MYSQL* con = NULL;
        con = mysql_init(con);
        if (con == NULL) {
            LOG_ERROR("mysql error");
            exit(1);
        }
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);
        if (con == NULL) {
            LOG_ERROR("mysql error");
            exit(1);
        }
        connList.push_back(con);
        ++m_FreeConn;
    }
    this->reserve = sem(m_FreeConn);//信号量初始值就是m_FreeConn
    this->m_MaxConn = this->m_FreeConn;
}

MYSQL* connection_pool::GetConnection() {
    MYSQL* con = NULL;
    this->reserve.wait();
    this->lock.lock();
    
    con = this->connList.front();
    this->connList.pop_front();
    --this->m_FreeConn;
    ++this->m_CurConn;

    this->lock.unlock();
    return con;
}

bool connection_pool::ReleaseConnection(MYSQL* con) {
    if (con == NULL) return false;

    this->lock.lock();

    this->connList.push_back(con);
    ++this->m_FreeConn;
    --this->m_CurConn;

    this->lock.unlock();
    this->reserve.post();
    return true;
}

void connection_pool::DestroyPool() {
    this->lock.lock();

    if (this->connList.size() > 0) {
        std::list<MYSQL*>::iterator iter = this->connList.begin();
        while (iter != this->connList.end()) {
            mysql_close(*iter);
            ++iter;
        }
    }
    this->lock.unlock();
}