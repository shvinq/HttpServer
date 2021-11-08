#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include "timer.h"

#define FD_LIMIT 65535
#define MAX_EVENT_NUMBER 1024
#define TIMESLOT 5

static int pipefd[2];
static int epollfd = 0;

int setnoblockinig(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnoblockinig(fd);
}

void sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    int sendBytes = send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

void addsig(int sig)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

// void timer_handler()
// {
//     timer_heap->tick();
//     alarm(TIMESLOT);
// }

/* 定时器回调函数，删除非活动连接socket的注册事件，并关闭它 */
void cb_func(client_data* user_data)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    printf("close fd %d\n", user_data->sockfd);
}

int main(int argc, char* argv[])
{
    const char* ip = argv[1];
    int port = atoi(argv[2]);

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    int ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    ret = listen(listenfd, 5);

    static time_heap * timer_heap = new time_heap(10);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    addfd(epollfd, listenfd);

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnoblockinig(pipefd[1]);
    addfd(epollfd, pipefd[0]);

    addsig(SIGALRM);
    addsig(SIGTERM);
    bool stop_server = false;
    client_data* users = new client_data[FD_LIMIT];
    alarm(TIMESLOT);                                //定时,这里有个问题，当新连接conn进来时在T周期某个时间点t，所以不会触发SIGALRM信号，所以conn连接超时周期是expire+(T-建立连接时的t)
                                                    //例如现在事件cur是0，周期T=5，在第3秒进来，我的超时时间理论是cur+3*T=15，但是要过了2秒后才有信号产生，产生了信号才会去执行函数检测，所以总超时时间是17秒。
                                                    //所以T越大则超时偏差越大。当在新连接进来是重新alarm(T)可以临时解决，但是连接多了的话会后面新连接会导致前面的旧连接异常


    while(!stop_server)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if(number < 0 && errno != EINTR) { printf("epoll failure\n"); break; }

        for(int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            
            if(sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_address_len = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_address_len);
                addfd(epollfd, connfd);
                users[connfd].address = client_address;
                users[connfd].sockfd = connfd;

                /* 创建定时器。设置回调函数与超时时间，然后绑定定时器与用户数据，最后加入时间堆 */
                heap_timer* timer = new heap_timer;
                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                printf("THE init: this conn expire= %ld, now cur= %ld\n", timer->expire, cur);
                users[connfd].timer = timer;
                timer_heap->add_timer(timer);
            }
            else if(sockfd == pipefd[0] && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if(ret == -1) continue;
                else if(ret == 0) continue;
                else
                {
                    for(int i = 0; i < ret; i++)
                    {
                        switch(signals[i])
                        {
                            case SIGALRM:
                            {
                                timer_heap->tick();
                                alarm(TIMESLOT);
                                break;
                            }
                            case SIGTERM:
                            {
                                stop_server = true;
                            }
                        }
                    }
                }
            }
            else if(events[i].events & EPOLLIN)
            {
                memset(users[sockfd].buf, '\0', BUFFER_SIZE);
                ret = recv(sockfd, users[sockfd].buf, BUFFER_SIZE, 0);
                printf("get %d bytes of client data %s", ret, users[sockfd].buf);
                heap_timer* timer = users[sockfd].timer;
                if(ret < 0)
                {
                    if(errno != EAGAIN)             //发生读错误则关闭连接，移除对应的定时器
                    {
                        cb_func(&users[sockfd]);
                        if(timer) timer_heap->del_timer(timer);
                    }
                }
                else if(ret == 0)               //如果对方关闭连接，则本端也关闭，并移除定时器
                {
                    if(timer) timer_heap->del_timer(timer);
                }
                else
                {
                    if(timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        printf("adjust timer once\n");
                    }
                }
            }
            else 
            {
                //其他
            }
        }

        /* 最后处理定时事件，因为IO事件优先级更高，但这会导致定时任务不能精确按照预期事件执行 */
        /*if(timeout)
        {
            printf("timeout == true, do timer_handler\n");
            timer_handler();
            timeout = false;
        }*/
    }
    close(listenfd);
    close(pipefd[0]);
    close(pipefd[1]);
    delete [] users;
    return 0;
}