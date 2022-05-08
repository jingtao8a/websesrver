/*
 * @Descripttion: 
 * @version: 
 * @Author: yuxintao
 * @Date: 2022-05-03 20:08:22
 * @LastEditors: yuxintao
 * @LastEditTime: 2022-05-07 20:14:51
 */
#ifndef BLOCK_QUEUE_HPP
#define BLOCK_QUEUE_HPP

#include <iostream>
#include <stdlib.h>
#include <sys/time.h>
#include "../lock/lock.hpp"


template <class T>
class block_queue{
    public:
        block_queue(int max_size = 1000) {
            if (max_size <= 0) {
                exit(1);
            }
            this->m_max_size = max_size;
            this->m_arry = new T[max_size];
            this->m_size = 0;
            this->m_front = -1;
            this->m_back = -1;
        }

        void clear() {
            this->m_mutex.lock();

            this->m_size = 0;
            this->m_front = this->m_back = -1;

            this->m_mutex.unlock();
        }

        ~block_queue() {
            this->m_mutex.lock();
            if (this->m_arry != NULL)
                delete[] this->m_arry;
            this->m_mutex.unlock();
        }

        bool full() {
            this->m_mutex.lock();
            if (this->m_size >= this->m_max_size) {
                this->m_mutex.unlock();
                return false;
            }
            this->m_mutex.unlock();    
            return true;
        }
    public:
        bool push(const T& item) {
            this->m_mutex.lock();
            
            if (this->m_size >= this->m_max_size) {//队列满
                this->m_cond.broadcast();
                this->m_mutex.unlock();
                return false;
            }
            this->m_back = (this->m_back + 1) % this->m_max_size;
            this->m_arry[this->m_back] = item;
            ++this->m_size;

            this->m_cond.broadcast();

            this->m_mutex.unlock();
            return true;
        }

        bool pop(T& item) {
            this->m_mutex.lock();

            while (this->m_size <= 0) {
                if (this->m_cond.wait(this->m_mutex.get()) == false) {
                    this->m_mutex.unlock();
                    return false;
                }
            }
            this->m_front = (this->m_front + 1) % this->m_max_size;
            item = this->m_arry[this->m_front];
            --this->m_size;

            this->m_mutex.unlock();
            return true;
        }

        bool pop(T& item, int ms_timeout) {
            struct timespec t = {0, 0};
            struct timeval now = {0, 0};
            gettimeofday(&now, NULL);

            this->m_mutex.lock();
            
            if (m_size <= 0) {
                t.tv_sec = now.tv_sec + ms_timeout / 1000;
                t.tv_nsec = (ms_timeout % 1000) / 1000;
                if (this->cond.timewait(this->m_mutex.get(), t) == false) {
                    this->m_mutex.unlock();
                    return false;
                }
            }
            this->m_front = (this->m_front + 1) % this->m_max_size;
            item = this->m_arry[this->m_front];
            --this->m_size;

            this->m_mutex.unlock();
            return true;
        }
    private:
        locker m_mutex;//互斥锁
        cond m_cond;//条件变量

        T* m_arry;//循环队列
        int m_size;//队列实际size
        int m_max_size;//队列最大size
        int m_front;//队首
        int m_back;//队尾
};

#endif