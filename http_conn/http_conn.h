#ifndef http_conn_H
#define http_conn_H

/*
    http连接封装类
*/
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include<stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "../threadpool/locker.h"

class http_conn
{
    public:
        static const int FILENAME_LEN = 200;
        static const int READ_BUFFER_SIZE = 2048;
        static const int WRITE_BUFFER_SIZE = 1024;
        enum METHOD
        {
            GET = 0,
            POST,
            HEAD,
            PUT,
            DELETE,
            TRACE,
            OPTIONS,
            CONNECT,
            PATCH
        };
        enum CHECK_STATE
        {
            CHECK_STATE_REQUESTLINE = 0,
            CHECK_STATE_HEADER,
            CHECK_STATE_CONTENT
        };
        enum HTTP_CODE
        {
            NO_REQUEST = 0,
            GET_REQUEST,
            BAD_REQUEST,
            NO_RESOURCE,
            FORBIDDEN_REQUEST,
            FILE_REQUEST,
            INTERVAL_ERROR,
            CLOSED_CONNECTION
        };
        enum LINE_STATUS
        {
            LINE_OK = 0,
            LINE_BAD,
            LINE_OPEN
        };

    /* 成员接口函数 */
    public:
        http_conn(){};
        ~http_conn(){};

        void init(int socketfd, const sockaddr_in &addr);   //初始化连接
        void close_conn(bool real_close = true);            //关闭连接
        void process();                                     //入口函数
        bool read();
        bool write();
    
    private:
        void init();
        HTTP_CODE process_read();                           //处理请求消息
        bool process_write(HTTP_CODE ret);                  //根据解析结果处理响应消息

        LINE_STATUS parse_line();
        char *get_line() { return m_start_line + m_read_buf; }
        HTTP_CODE parse_request_line(char * text);
        HTTP_CODE parse_headers(char * text);
        HTTP_CODE parse_content(char * text);
        HTTP_CODE do_request();                             //请求消息处理的返回值函数

        void unmap();
        bool add_response(const char * format, ...);
        bool add_content(const char * content);
        bool add_status_line(int status, const char *title);
        bool add_headers(int content_length);
        bool add_content_length(int content_length);
        bool add_linger();
        bool add_blank_line();


    /* 成员变量 */
    public:
        static int m_epollfd;
        static int m_user_count;
    
    private:
        CHECK_STATE m_check_state;
        METHOD m_method;
        int m_sockfd;
        sockaddr_in m_address;
        
        char m_read_buf[READ_BUFFER_SIZE];
        char m_write_buf[WRITE_BUFFER_SIZE];
        int m_read_idx;
        int m_write_idx;
        int m_check_idx;
        int m_start_line;

        char * m_url;
        char * m_host;
        char * m_version;
        bool m_linger;
        int m_content_length;
        char m_real_file[FILENAME_LEN];

        struct stat m_file_stat;
        char * m_file_address;
        struct iovec m_iv[2];
        int m_iv_count;
};





#endif
