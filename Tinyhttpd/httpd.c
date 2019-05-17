/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
// HTTP请求报文由三部分组成 (请求行 + 请求头 + 请求体)
// 范例:请求行   <method> <request-URL> <version>
//     请求头   
//              \r\n
//     请求体    
// 宏定义,判断是否空格
#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

// 
void* accept_request(void*);
// 
void bad_request(int);
// 
void cat(int, FILE *);
// 
void cannot_execute(int);
// 将错误信息写到perror,然后退出主线程
void error_die(const char *);
// 
void execute_cgi(int, const char *, const char *, const char *);
// 
int get_line(int, char *, int);
// 
void headers(int, const char *);
// 当请求文件在服务器端不存在的情况
void not_found(int);
// 
void serve_file(int, const char *);
// 初始化httpd服务,包括建立套接字,绑定端口,进行监听等
int startup(unsigned short *);
// 返回给浏览器,表明收到的HTTP请求报文中所用的请求方法暂不支持(暂支持GET,POST)
void unimplemented(int);

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the listenfd */
/**********************************************************************/
// 一个客户端请求对应一个线程处理函数(短连接下,一个客户端请求之后,就会断开连接)
// void* listenfd: 已连接描述符指针
void* accept_request(void* client) {
    int listenfd = *(int*)client;
    char buf[1024];
    int numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;      /* becomes true if server decides this is a CGI program */
    char *query_string = NULL;

    // 得到HTTP请求报文的请求行
    numchars = get_line(listenfd, buf, sizeof(buf));
    i = 0; 
    j = 0; // 请求行的检索下标
    // 得到请求行里面的请求方法(默认请求行不含前缀空格)
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1)) {
        method[i] = buf[j];
        i++; 
        j++;
    }
    method[i] = '\0';

    // 请求方法既不是 GET 又不是 POST,则返回501(服务端不具备完成请求的功能)
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
        unimplemented(listenfd);
        return NULL;
    }

    // 请求方法 POST,则开启CGI
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;
    
    i = 0;
    // 跳过请求方法和url之间的空格
    while (ISspace(buf[j]) && (j < sizeof(buf))) 
        j++;
    // 获得url地址
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf))) {
        url[i] = buf[j];
        i++; 
        j++;
    }
    url[i] = '\0';

    // 处理请求方法 GET
    if (strcasecmp(method, "GET") == 0) {
        // 
        query_string = url;
        // 跳过 url ?前面的部分,去获取? 后面的参数
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        // 获取 GET 方法, ? 后面的参数
        if (*query_string == '?') {
            // 如果待有查询参数,需要执行cgi,解析参数,设置标志位为1
            cgi = 1;
            // 将解析参数截取下来
            *query_string = '\0';
            query_string++;
        }
    }

    // 格式化 url 到 path 数组, html 文件都在 htdocs 中
    sprintf(path, "htdocs%s", url);

    // 如果path是一个目录,默认设置为该目录下的index.html
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");

    // 函数定义: int stat(const char *file_name, struct stat *buf);
    // 函数说明: 通过文件名获取文件信息,并报存在buf所指的结构体stat中
    // 返回值:   执行成功返回0,失败返回-1,错误代码存在errno
    if (stat(path, &st) == -1) {
        // path路径下,找不到该文件,则收完剩余的请求报文头
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(listenfd, buf, sizeof(buf));
        // 回应客户端找不到该网页
        not_found(listenfd);
    } else {
        // 如果 path 是个目录,则默认使用该目录下的 index.html 文件
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");
        if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
            cgi = 1;
        if (!cgi)
            // 直接返回服务端的静态网页
            serve_file(listenfd, path);
        else
            // 执行 CGI
            execute_cgi(listenfd, path, method, query_string);
    }

    // 断开和客户端的连接 (HTTP1.0特点:短连接)
    close(listenfd);
    return NULL;
}

/**********************************************************************/
/* Inform the listenfd that a request it has made has a problem.
 * Parameters: listenfd socket */
/**********************************************************************/
void bad_request(int listenfd) {
    char buf[1024];
    // 响应报文的 状态行
    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(listenfd, buf, sizeof(buf), 0);
    // 响应报文的 首部字段
    sprintf(buf, "Content-type: text/html\r\n");
    send(listenfd, buf, sizeof(buf), 0);
    // 响应报文的 空行(CR + LF)
    sprintf(buf, "\r\n");
    send(listenfd, buf, sizeof(buf), 0);
    // 响应报文的 报文主体
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(listenfd, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(listenfd, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the listenfd socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
// 读取文件中的所有数据,并发送到listenfd中
void cat(int listenfd, FILE *resource) {
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    while (!feof(resource)) {
        send(listenfd, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/**********************************************************************/
/* Inform the listenfd that a CGI script could not be executed.
 * Parameter: the listenfd socket descriptor. */
/**********************************************************************/
// 回应浏览器, CGI 无法运行
void cannot_execute(int listenfd) {
    char buf[1024];
    // 响应报文的 状态行
    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(listenfd, buf, strlen(buf), 0);
    // 响应报文的 首部字段
    sprintf(buf, "Content-type: text/html\r\n");
    send(listenfd, buf, strlen(buf), 0);
    // 响应报文的 空行(CR + LF)
    sprintf(buf, "\r\n");
    send(listenfd, buf, strlen(buf), 0);
    // 响应报文的 报文主体
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(listenfd, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
// 出错信息处理
void error_die(const char *sc) {
    perror(sc);
    exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: listenfd socket descriptor
 *             path to the CGI script */
/**********************************************************************/
// 使用CGI动态解析浏览器的请求(请求方法是POST,或者GET带请求参数)
void execute_cgi(int listenfd, const char *path, const char *method, const char *query_string) {
    char buf[1024];
    // cgi_output是子进程(执行cgi的进程)的输出管道,子进程写,父进程读
    // cgi_input是子进程(执行cgi的进程)的输入管道,父进程写,子进程读
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A'; 
    buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0) {
        // 如果是GET请求,说明query_string中已经包含了请求参数
        // 读取HTTP头部剩余的部分,并丢弃
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(listenfd, buf, sizeof(buf));
    } else {   /* POST */
        numchars = get_line(listenfd, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf)) {
            // 如果是POST请求,需要得到Content-Length,该字符串长15位
            // 得到请求头的一行后,将第16位设置为空字符,进行比较
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
                // 从第17位就是content_length:请求体的长度
                content_length = atoi(&(buf[16]));
            numchars = get_line(listenfd, buf, sizeof(buf));
        }
        // 没有找到content_length
        if (content_length == -1) {
            // 错误请求
            bad_request(listenfd);
            return;
        }
    }

    // 返回正确响应码 200 ok
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(listenfd, buf, strlen(buf), 0);

    if (pipe(cgi_output) < 0) {
        cannot_execute(listenfd);
        return;
    }
    if (pipe(cgi_input) < 0) {
        cannot_execute(listenfd);
        return;
    }

    if ( (pid = fork()) < 0 ) {
        cannot_execute(listenfd);
        return;
    }
    if (pid == 0) { /* child: CGI script */
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        // CGI程序使用标准输入输出进行交互
        dup2(cgi_output[1], 1); // 将系统标准输出重定向为cgi_output[1]
        dup2(cgi_input[0], 0);  // 将系统标准输入重定向为cgi_input[0]
        // 关闭cgi_output的读入端
        close(cgi_output[0]);
        // 关闭cgi_input的写入端
        close(cgi_input[1]);
        // CGI标准需要将请求方法存储在环境变量中,然后才和CGI脚本进行交互
        // 存储 REQUEST_METHOD
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0) {
            // 存储 QUERY_STRING
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        } else {   /* POST */
            // 存储 CONTENT_LENGTH
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        // int execl(const char* path, const char* argv,...);
        // 执行参数path所代表的CGI程序,接下来的参数代表执行该程序所传递的参数,最后一个必须以NULL结束
        execl(path, path, NULL);
        exit(0);
    } else {    // 父进程
        // 关闭 cgi_output的写入端
        close(cgi_output[1]);
        // 关闭 cgi_input的读入端
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0) {
            for (i = 0; i < content_length; i++) {
                recv(listenfd, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        }
        // 读取子进程(cgi脚本)返回数据
        while (read(cgi_output[0], &c, 1) > 0)
            // 发送给浏览器
            send(listenfd, &c, 1, 0);

        // 关闭管道
        close(cgi_output[0]);
        close(cgi_input[1]);
        // 父进程等待子进程
        waitpid(pid, &status, 0);// 阻塞在这里,等待子进程的退出
    }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size) {
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n')) {
        n = recv(sock, &c, 1, 0);
        if (n > 0) {
            if (c == '\r') {
                n = recv(sock, &c, 1, MSG_PEEK);
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        } else
            c = '\n';
    }
    buf[i] = '\0';
 
    return (i);
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int listenfd, const char *filename) {
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */

    // 响应报文的 状态行
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(listenfd, buf, strlen(buf), 0);
    // 响应报文的 首部字段
    strcpy(buf, SERVER_STRING);
    send(listenfd, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(listenfd, buf, strlen(buf), 0);
    // 空行(CR + LF)
    strcpy(buf, "\r\n");
    send(listenfd, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a listenfd a 404 not found status message. */
/**********************************************************************/
// HTTP状态码负责表示客户端HTTP请求的返回结果,标记服务器端的处理是否正常,通知出现的错误等工作
// 4XX 服务器无法处理请求
//     404 服务器上没有请求的资源
void not_found(int listenfd) {
    char buf[1024];
    // 响应报文的 状态行
    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(listenfd, buf, strlen(buf), 0);
    // 响应报文的 首部字段
    sprintf(buf, SERVER_STRING);
    send(listenfd, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(listenfd, buf, strlen(buf), 0);
    // 响应报文的 空行(CR + LF)
    sprintf(buf, "\r\n");
    send(listenfd, buf, strlen(buf), 0);
    // 响应报文的 报文主体
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(listenfd, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(listenfd, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(listenfd, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(listenfd, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(listenfd, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the listenfd.  Use headers, and report
 * errors to listenfd if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int listenfd, const char *filename) {
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    // 读取请求报文剩余的请求头信息,直接丢弃
    buf[0] = 'A'; 
    buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
        numchars = get_line(listenfd, buf, sizeof(buf));

    // 打开filename表示的文件
    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(listenfd);
    else {
        // 写 HTTP 响应报文的头部
        headers(listenfd, filename);
        cat(listenfd, resource);
    }
    fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
int startup(unsigned short *port) {
    int httpd = 0;
    struct sockaddr_in name;

    // 建立 socket
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");
    // 如果当前指定端口是 0,则动态随机分配一个端口
    if (*port == 0) { /* if dynamically allocating a port */
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
            error_die("getsockname");
        *port = ntohs(name.sin_port);
    }
    // 开始监听该端口
    if (listen(httpd, 5) < 0)
        error_die("listen");
    return (httpd);
}

/**********************************************************************/
/* Inform the listenfd that the requested web method has not been
 * implemented.
 * Parameter: the listenfd socket */
/**********************************************************************/
// HTTP状态码负责表示客户端HTTP请求的返回结果,标记服务器端的处理是否正常,通知出现的错误等工作
// 5XX 服务器错误状态码 服务器处理请求出错
//     501 服务端不具备完成请求的功能
// 暂不支持对其他的HTTP请求方法的解析(目前只支持Get,Post请求方法的解析)
void unimplemented(int listenfd) {
    char buf[1024];
    // 响应报文的 状态行
    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(listenfd, buf, strlen(buf), 0);
    // 响应报文的 首部字段
    sprintf(buf, SERVER_STRING);
    send(listenfd, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(listenfd, buf, strlen(buf), 0);
    // 空行(CR + LF)
    sprintf(buf, "\r\n");
    send(listenfd, buf, strlen(buf), 0);
    // 响应报文的 报文主体
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(listenfd, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(listenfd, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(listenfd, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(listenfd, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void) {
    int server_sock = -1;
    unsigned short port = 0;
    int listenfd_sock = -1;
    struct sockaddr_in listenfd_name;
    socklen_t listenfd_name_len = sizeof(listenfd_name);
    pthread_t newthread;

    server_sock = startup(&port);
    printf("httpd running on port %d\n", port);

    while (1) {
        listenfd_sock = accept(server_sock, (struct sockaddr*)&listenfd_name, &listenfd_name_len);
        if (listenfd_sock == -1)
            error_die("accept");
        if (pthread_create(&newthread , NULL, accept_request, &listenfd_sock) != 0)
            perror("pthread_create");
    }

    close(server_sock);
    return (0);
}