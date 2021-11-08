#include "timer.h"


/* 构造函数之一：初始化一个大小为cap的空堆 */
time_heap::time_heap(int cap) : capacity(cap), cur_size(0)
{
    printf("the cur_size= %d\n", cur_size);
    array = new heap_timer* [capacity];                       //创建堆数组
    if(!array) throw std::exception();

    for(int i = 0; i < capacity; i++) array[i] = NULL;
}


/* 构造函数之二：用已有数组来初始化堆 */
time_heap::time_heap(heap_timer** init_array, int size, int capacity) : cur_size(size), capacity(capacity)
{
    if(capacity < size) throw std::exception();
    array = new heap_timer* [capacity];                       //创建堆数组
    if(!array) throw std::exception();

    for(int i = 0; i < capacity; i++) array[i] = NULL;

    if(size != 0)
    {
        for(int i = 0; i < size; i++) array[i] = init_array[i];
        for(int i = (cur_size-1)/2; i >= 0; i--) percolate_down(i);         //堆数组中的第[(cur_size-1)/2] ~ 0个元素执行下沉操作
    }
}


void time_heap::add_timer(heap_timer* timer) 
{
    if(!timer) return;
    if(cur_size >= capacity) resize();                          //如果堆容量不足则扩容1倍
    /* 新加一个元素，当前堆大小加1，hole是新建节点的位置 */
    int hole = cur_size++;
    int parent = 0;

    /* 对从新节点位置到根节点的路径上的所有节点执行上滤操作 */
    for(; hole > 0; hole = parent)
    {
        parent = (hole - 1) / 2;
        if(array[parent]->expire <= timer->expire) break;
        array[hole] = array[parent];
    }
    array[hole] = timer;
}


void time_heap::del_timer(heap_timer* timer)
{
    if(!timer) return;
    timer->cb_func = NULL;              //仅仅将目标定时器的回调函数置为空，即所谓的延迟销毁，这将节省删除该定时器造成的开销，但容易使堆数组膨胀
}


heap_timer* time_heap::top() const
{
    if(empty()) return NULL;
    return array[0];
}


void time_heap::pop_timer()
{
    if(empty()) return;
    if(array[0])
    {
        delete array[0];
        array[0] = array[--cur_size];           //将原来堆顶元素替换为堆数组最后一个元素
        percolate_down(0);                      //对新堆顶元素进行下沉操作
    }
}


void time_heap::tick()
{
    heap_timer* tmp = array[0];
    time_t cur = time(NULL);

    /* 循环处理堆中到期的定时器 */
    while(!empty())
    {
        if(!tmp) break;
        printf("this conn expire= %ld, now cur= %ld\n", tmp->expire, cur);
        if(tmp->expire > cur) break;                                    //如果堆顶元素未到期则退出循环

        if(array[0]->cb_func) array[0]->cb_func(array[0]->user_data);   //否则执行堆顶定时器的任务

        pop_timer();
        tmp = array[0];    
    }
}


void time_heap::percolate_down(int hole)
{
    heap_timer* tmp = array[hole];
    int child = 0;

    for(; (hole*2+1) <= (cur_size-1); hole = child)
    {
        child = hole * 2 + 1;
        if(child < (cur_size - 1) && array[child+1]->expire < array[child]->expire) child++;

        if(array[child]->expire < tmp->expire) array[hole] = array[child];
        else break;
    }
    array[hole] = tmp;
}


void time_heap::resize()
{
    printf("do resize1\n");
    heap_timer** tmp = new heap_timer* [2 * capacity];
    printf("do resize2\n");
    for(int i = 0; i < 2 * capacity; i++) tmp[i] = NULL;

    if(!tmp) throw std::exception();

    capacity *= 2;
    for(int i = 0; i < cur_size; i++) tmp[i] = array[i];
    delete [] array;
    array = tmp;
}