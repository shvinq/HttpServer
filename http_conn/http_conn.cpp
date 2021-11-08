#include "http_conn.h"

const char* ok_200_title = "OK";
const char* error_400_title = "Bad_Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "404\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "500\n";

const char* doc_root = "./";

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;


/* 事件源辅助函数 */

int setnoblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}


void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if(one_shot) event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnoblocking(fd);
}


void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}


void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}


/* 类成员函数 */

void http_conn::init(int socketfd, const sockaddr_in &addr)
{
    m_sockfd = socketfd;
    m_address = addr;

    addfd(m_epollfd, socketfd, true);
    m_user_count++;
    
    init();
}


void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_method = GET;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    m_check_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    m_start_line = 0;

    m_url = 0;
    m_host = 0;
    m_version = 0;
    m_linger = false;
    m_content_length = 0;
    memset(m_real_file, '\0', FILENAME_LEN);
}


void http_conn::close_conn(bool real_close)
{
    if(real_close && m_sockfd != -1)
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}


void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    bool write_ret = process_write(read_ret);
    if(!write_ret) close_conn();
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}


bool http_conn::read()
{
    if(m_read_idx > READ_BUFFER_SIZE) return false;
    int bytes_read = 0;
    while(true)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE, 0);
        if(bytes_read == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK) break;
            return false;
        }
        else if(bytes_read == 0) return false;

        m_read_idx += bytes_read;
    }
    return true;
}


bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    
    if(bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while(true)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp <= -1)
        {
            if(errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;

        if(bytes_have_send >= bytes_to_send)
        {
            unmap();
            if(m_linger)
            {
                init();
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return true;
            }
            else
            {
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}


/* 主状态机 */
http_conn::HTTP_CODE http_conn::process_read()
{
    HTTP_CODE ret = NO_REQUEST;
    LINE_STATUS  line_status = LINE_OK;
    char * text = 0;

    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();
        m_start_line = m_check_idx;
        printf("got 1 http line: %s\n", text);

        switch(m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                printf("解析请求行： ");
                ret = parse_request_line(text);
                
                if(ret == BAD_REQUEST) return BAD_REQUEST;
                break;
            }

            case CHECK_STATE_HEADER:
            {
                printf("解析请求头： ");
                ret = parse_headers(text);

                if(ret == BAD_REQUEST) return BAD_REQUEST;
                else if(ret == GET_REQUEST) return do_request();
                break;
            }

            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                
                if(ret == GET_REQUEST) return do_request();
                line_status = LINE_OPEN;
                break;
            }

            default:
            {
                return INTERVAL_ERROR;
            }
        }
    }

    return NO_REQUEST;
}

bool http_conn::process_write(HTTP_CODE ret)
{
    switch(ret)
    {
        case INTERVAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)) return false;
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)) return false;
            break;
        }
        case NO_RESOURCE:
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form)) return false;
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)) return false;
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200, ok_200_title);
            if(m_file_stat.st_size != 0)
            {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            else
            {
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string)) return false;
            }
        }
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

/* 从状态机：处理头部每行信息 */
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for(; m_check_idx < m_read_idx; m_check_idx++)
    {
        temp = m_read_buf[m_check_idx];
        if(temp == '\r')
        {
            if(m_check_idx + 1 == m_read_idx) return LINE_OPEN;         //达到头部数据末尾
            else if(m_read_buf[m_check_idx + 1] == '\n')                //遇到\r\n行结尾
            {
                m_read_buf[m_check_idx++] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n')
        {
            if(m_check_idx > 1 && m_read_buf[m_check_idx - 1] == '\r')
            {
                m_read_buf[m_check_idx-1] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }

    return LINE_OPEN;
}


http_conn::HTTP_CODE http_conn::parse_request_line(char * text)
{
    m_url = strpbrk(text, " \t");           //在text中查找" \t"，返回第一个匹配的字符及其后面的字符串
    if(!m_url) return BAD_REQUEST;

    *m_url++ = '\0';                        //清除url中的 \t

    char * method = text;
    if(strcasecmp(method, "GET") == 0) m_method = GET;          //忽略大小写比较method和“GET”，若相同则返回0。
    else return BAD_REQUEST;

    m_url += strspn(m_url, " \t");                          //返回在m_url中第一个不在字符串" \t"中出现的字符下标
    m_version = strpbrk(m_url, " \t");
    if(!m_version) return BAD_REQUEST;

    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if(strcasecmp(m_version, "HTTP/1.1") != 0) return BAD_REQUEST;

    if(strncasecmp(m_url, "http//", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if(!m_url || m_url[0] != '/') return BAD_REQUEST;
    
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}


http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
    if(text[0] == '\0')
    {
        if(m_content_length != 0)           //如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，状态机转移到CHECK_STATE_CONTENT状态
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;             
        }
        return GET_REQUEST;                 //否则说明已经得到了一个完整的HTTP请求
    }

    else if(strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0) m_linger = true;
    }

    else if(strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }

    else if(strncasecmp(text+2, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
        // sleep(5);
    }
    else
    {
        printf("oop! unknow header %s\n", text);
    }
    return NO_REQUEST;
}


http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
    if(m_read_idx >= (m_content_length + m_check_idx))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}


http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);
    int len  = strlen(doc_root);
    
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    printf("文件名： %s\n", m_real_file);
    if(stat(m_real_file, &m_file_stat) < 0) return NO_RESOURCE;
    if(!(m_file_stat.st_mode & S_IROTH)) return FORBIDDEN_REQUEST;
    if(S_ISDIR(m_file_stat.st_mode)) return BAD_REQUEST;

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}


/*对内存映射区执行munmap操作*/
void http_conn::unmap()
{
    if(m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}


bool http_conn::add_response(const char* format, ...)
{
    if(m_write_idx >= WRITE_BUFFER_SIZE) return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) return false;
    m_write_idx += len;
    va_end(arg_list);
    return true;
}


bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}


bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}


bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}


bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}


bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}


