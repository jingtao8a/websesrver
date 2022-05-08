/*
 * @Descripttion: 
 * @version: 
 * @Author: yuxintao
 * @Date: 2022-05-03 15:43:52
 * @LastEditors: yuxintao
 * @LastEditTime: 2022-05-08 12:57:43
 */

#ifndef THREADPOOL_HPP
#define THREADPOOL_HPP
#include "../CGImysql/connection_pool.hpp"

template <typename T>
class threadpool{
    public:
        threadpool(int actor_model, connection_pool* connPool, int thread_number = 8, int max_request = 10000);
        ~threadpool();
        void append(T* request, int state);//带状态的append,reactor
        void append_p(T* request);//proactor
    private:
        static void* worker(void* arg);//工作线程函数
        void run();
    private:
        int m_actor_model;//工作模式reactor or proactor
        connection_pool* m_connPool;//连接的数据库池
        int m_thread_number;//最大线程数
        int m_max_requests;//最大请求数

        pthread_t* m_threads;//线程id
        
        std::list<T*> m_workqueue;//工作队列
        locker m_queuelocker;//互斥锁
        sem m_queuestat;//信号量
};

template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool* connPool, int thread_number, int max_request) {
    if (thread_number <= 0 || max_request <= 0) throw std::exception();
    this->m_actor_model = actor_model;
    this->m_connPool = connPool;
    this->m_thread_number = thread_number;
    this->m_max_requests = max_request;

    this->m_threads = new pthread_t[thread_number];
    if (m_threads == NULL) throw std::exception();
    for (int i = 0; i < m_thread_number; ++i) {
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
}

template <typename T>
void threadpool<T>::append(T* request, int state) {//加入的http_conn对象,指定该http_conn对象写(1)还是读(0),reactor模式下
    this->m_queuelocker.lock();
    if (this->m_workqueue.size() >= this->m_max_requests) {
        m_queuelocker.unlock();
        return;
    }
    request->m_state = state;
    this->m_workqueue.push_back(request);
    this->m_queuelocker.unlock();
    this->m_queuestat.post();
    return;
}

template <typename T>
void threadpool<T>::append_p(T* request) {//proactor模式下
    this->m_queuelocker.lock();
    
    if (this->m_workqueue.size() >= this->m_max_requests) {
        this->m_queuelocker.unlock();
        return;
    }
    //生产
    this->m_workqueue.push_back(request);
    
    this->m_queuelocker.unlock();
    this->m_queuestat.post();
    return;
}

template <typename T>
void* threadpool<T>::worker(void *arg) {
    threadpool<T>* pool = (threadpool<T>*)arg;
    pool->run();
    return NULL;
}

template <typename T>
void threadpool<T>::run() {
    while (true) {
        this->m_queuestat.wait();
        
        this->m_queuelocker.lock();
        //消费
        T* request = this->m_workqueue.front();
        this->m_workqueue.pop_front();
        this->m_queuelocker.unlock();


        //工作线程处理request
        if (this->m_actor_model == 1) {//reactor模式

            if (request->m_state == 0) {//为读模式
                if (request->read_once()) {//read_once
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, this->m_connPool);//request->mysql获取连接，RAII机制
                    request->process();
                } else {//读出错
                    request->improv = 1;
                    request->timer_flag = 1;//删除连接定时器标志
                }
            } else {                   //为写模式
                if (request->write()) {
                    request->improv = 1;
                } else {
                    request->improv = 1;
                    request->timer_flag = 1;//删除连接定时器标志
                }
            }
            
        } else {//proactor模式
            connectionRAII mysqlcon(&request->mysql, this->m_connPool);//request->mysql获取连接，RAII机制
            request->process();
        }
    }
}

#endif