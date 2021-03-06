#ifndef MYLOCKER
#define MYLOCKER
/*
    1.线程同步机制的包装类:
    包括信号量，互斥锁和条件变量
*/

#include <pthread.h>
#include <semaphore.h>
#include <exception>


/* 封装信号量的类 */
class sem
{
    private:
        sem_t m_sem;
    public:
        /* 创建并初始化信号量 */
        sem()
        {
            if(sem_init(&m_sem, 0, 0) != 0) throw std::exception();     //抛出异常报告错误
        }

        /* 销毁信号量 */
        ~sem() { sem_destroy(&m_sem); }

        /* 等待信号量 */
        bool wait() { return sem_wait(&m_sem) == 0; }

        /* 增加信号量 */
        bool post() { return sem_post(&m_sem) == 0; }
};

/* 封装互斥锁的类 */
class locker
{
    private:
        pthread_mutex_t m_mutex;
    public:
        locker()
        {
            if(pthread_mutex_init(&m_mutex, NULL) != 0) throw std::exception();     //抛出异常报告错误
        }

        ~locker() { pthread_mutex_destroy(&m_mutex); }
        bool lock() { pthread_mutex_lock(&m_mutex); }       //获取互斥锁
        bool unlock() { pthread_mutex_unlock(&m_mutex); }       //释放互斥锁
};

/* 封装条件变量的类 */
class cond
{
    private:
        pthread_mutex_t m_mutex;
        pthread_cond_t m_cond;
    public:
        cond()
        {
            if(pthread_mutex_init(&m_mutex, NULL) != 0) throw std::exception();
            if(pthread_cond_init(&m_cond, NULL) != 0)
            {
                pthread_mutex_destroy(&m_mutex);        //构造函数出现问题时释放已分配的资源
                throw std::exception();
            }
        }
        ~cond()
        {
            pthread_mutex_destroy(&m_mutex);
            pthread_cond_destroy(&m_cond);
        }

        /*等待条件变量*/
        bool wait()
        {
            int ret = 0;
            pthread_mutex_lock(&m_mutex);
            ret = pthread_cond_wait(&m_cond, &m_mutex);
            pthread_mutex_unlock(&m_mutex);
            return ret == 0;
        }

        /*唤醒等待条件变量的线程*/
        bool signal()
        {
            return pthread_cond_signal(&m_cond);
        }
};


#endif