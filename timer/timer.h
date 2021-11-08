#ifndef TIMER_H
#define TIMER_H

/*
    基于时间堆的定时器类
*/

#include <iostream>
#include <netinet/in.h>
#include <time.h>

#define BUFFER_SIZE 64

class heap_timer;         //前向声明


/* 绑定socket和定时器 */
struct client_data
{
    sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    heap_timer * timer;
};


/* 定时器类 */
class heap_timer
{
    public:
        time_t expire;              //定时器生效的绝对时间
        void (*cb_func) (client_data *);        //定时器的回调函数
        client_data * user_data;
    public:
        heap_timer() : expire(0) {};
        heap_timer(int delay)
        {
            expire = time(NULL) + delay;
        }
};


/* 时间堆类 */
class time_heap
{
    private:
        heap_timer** array;                       //堆数组
        int capacity;                               //堆数组的容量
        int cur_size;                               //堆数组当前元素个数

    private:
        void percolate_down(int hole);              //小根堆的下沉操作
        void resize();                              //将堆数组容量扩大1倍
        
    public:
        time_heap(int cap);
        time_heap(heap_timer** init_array, int size, int capacity);       //重载构造函数，可以用已有数组来初始化堆
        ~time_heap()
        {
            for(int i = 0; i < cur_size; i++) delete array[i];
            delete [] array;
        }

    public:
        void add_timer(heap_timer * timer);       //添加目标定时器timer
        void del_timer(heap_timer * timer);       //删除目标定时器timer
        heap_timer * top() const;                 //获取堆顶的定时器
        void pop_timer();                           //删除堆顶定时器
        void tick();                                //心搏函数
        bool empty() const { return cur_size == 0; }
};


#endif