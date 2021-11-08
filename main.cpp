#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "timer/timer.h"
#include "threadpool/locker.h"
#include "threadpool/threadpool.h"
#include "http_conn/http_conn.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
#define FD_LIMIT 65535
#define TIMESLOT 5

static int pipefd[2];
static int epollfd = 0;
static client_data * clientUsers = NULL;
static time_heap * timer_heap = new time_heap(10);          //创建时间堆存放定时任务

extern int setnoblocking(int fd);
extern int removefd(int epollfd, int fd);
extern int addfd(int epollfd, int fd, bool one_shot);

/* 定时器回调函数，删除非活动连接socket的注册事件，并关闭它 */
void cb_func(client_data* user_data)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    printf("close fd %d\n", user_data->sockfd);
}

void sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    int sendBytes = send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart) sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void show_error(int connfd, const char* info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

/*******************定时器相关函数**********************/
void setTimer(int connfd, sockaddr_in& client_address)
{
    clientUsers[connfd].address = client_address;
    clientUsers[connfd].sockfd = connfd;

    /* 创建定时器。设置回调函数与超时时间，然后绑定定时器与用户数据，最后加入时间堆 */
    heap_timer* timer = new heap_timer;
    timer->user_data = &clientUsers[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    printf("THE init: this conn expire= %ld, now cur= %ld\n", timer->expire, cur);
    clientUsers[connfd].timer = timer;
    timer_heap->add_timer(timer);
}

void dealTimerSIG()
{
    int sig;
    char signals[1024];
    int ret = recv(pipefd[0], signals, sizeof(signals), 0);
    if(ret == -1) return;
    else if(ret == 0) return;
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
                    /*TODO*/
                }
            }
        }
    }
}

void adjustTimer(int sockfd)
{
    heap_timer* timer = clientUsers[sockfd].timer;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    printf("adjust timer once\n");

}


int main(int argc, char * argv[])
{
    /* 初始化服务器socket */
    const char* ip = argv[1];
    int port = atoi(argv[2]);
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    struct linger tmp = {1, 0};
    setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    ret = listen(listenfd, 5);
    
    /*
    SIGPIPE:如果socket在接收到了RST之后，程序仍然向这个socket写入数据就会产生SIGPIPE信号,默认情况下这个信号会终止整个进程
    SIG_IGN:忽略信号的处理程序
    */
    addsig(SIGPIPE, SIG_IGN);

    /* 创建线程池和http连接数组httpUsers */
    threadpool<http_conn>* pool = NULL;
    try
    {
        pool = new threadpool<http_conn>;
    }
    catch(...)
    {
        return 1;
    }

    http_conn* httpUsers = new http_conn[MAX_FD];
    assert(httpUsers);
    int user_cont = 0;

    /* 创建epoll对象 */
    epoll_event events[MAX_EVENT_NUMBER];
    epollfd  = epoll_create(5);
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    /* 设置定时信号传输管道，添加SIGALRM信号，创建客户端信息数组clientUsers */
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnoblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false);
    addsig(SIGALRM, sig_handler);
    addsig(SIGTERM, sig_handler);
    clientUsers= new client_data[FD_LIMIT];
    alarm(TIMESLOT);

    while(true)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if(number < 0 && errno != EINTR)
        {
            printf("epoll failure\n");
            break;
        }
        for(int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_address_len = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_address_len);
                if(connfd < 0)
                {
                    printf("errno is : %d\n", errno);
                    continue;
                }
                if(http_conn::m_user_count >= MAX_FD)
                {
                    show_error(connfd, "Internal Server Busy");
                    continue;
                }
                httpUsers[connfd].init(connfd, client_address);
                setTimer(connfd, client_address);
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP |EPOLLERR)) httpUsers[sockfd].close_conn();
            else if(events[i].events & EPOLLIN)
            {
                if(sockfd == pipefd[0]) dealTimerSIG();
                if(httpUsers[sockfd].read()) pool->append(httpUsers + sockfd), adjustTimer(sockfd);
                else httpUsers[sockfd].close_conn();
            }
            else if(events[i].events & EPOLLOUT)
            {
                if(!httpUsers[sockfd].write()) httpUsers[sockfd].close_conn();
            }
            else {}
        }
    }
    close(epollfd);
    close(listenfd);
    delete [] httpUsers;
    delete pool;
    return 0;
}