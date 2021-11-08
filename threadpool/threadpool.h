#ifndef THREADPOOL_H
#define THREADPOOL_H

/*
    2.线程池模块：
    半同步/半反应堆线程池，其使用一个工作队列解除主线程和工作线程的耦合关系
    主线程往工作队列中插入任务，工作线程通过竞争来取得任务并执行任务。
*/

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>

#include "locker.h"

template<typename T>
class threadpool
{
    public:
        threadpool( int thread_number = 8, int max_requests = 10000 );
        ~threadpool();
        bool append(T * request);       //往请求队列中添加任务
    
    private:
        /* 工作线程运行的函数，其不断从工作队列中取出任务并执行 */
        static void * worker(void * arg);
        void run();
        
    private:
        int m_thread_number;            //线程池中的线程数
        int m_max_requests;             //请求队列中允许的最大请求数量
        pthread_t * m_threads;          //描述线程池的数组，大小为m_thread_number
        std::list<T*> m_workqueue;      //请求队列
        locker m_queuelocker;           //保护请求队列的互斥锁
        sem m_queuestat;                //是否有任务需要处理
        bool m_stop;                    //是否结束线程
};


/* 实现部分 */

/* 线程池构造函数实现 */
template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) : m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL)
{
    if(thread_number <= 0 || max_requests <= 0) throw std::exception();

    m_threads = new pthread_t[m_thread_number];
    if(!m_threads) throw std::exception();

    /* 创建指定数量线程， */
    for(int i = 0; i < thread_number; i++)
    {
        printf("create the %dth thread\n", i);
        if(pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete [] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i]))
        {
            delete [] m_threads;
            throw std::exception();
        }
    }
}


/* 线程池析构函数实现 */
template<typename T>
threadpool<T>::~threadpool()
{
    delete [] m_threads;
    m_stop = true;
}

/* 向队列中添加任务函数实现 */
template<typename T>
bool threadpool<T>::append(T * request)
{
    m_queuelocker.lock();                       //因为工作队列被所有线程共享，所以操作时需要加锁
    if(m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();                         //通知工作线程有任务加入
    return true;
}

/* 线程运行函数实现 */
template<typename T>
void * threadpool<T>::worker(void * arg)
{
    threadpool * pool = (threadpool * ) arg;
    pool->run();
    return pool;
}


template<typename T>
void threadpool<T>::run()
{
    while(!m_stop)
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request) continue;
        request->process();                 //线程进行任务处理
    }
}


#endif
