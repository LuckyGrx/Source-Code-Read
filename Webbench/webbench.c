/*
 * (C) Radim Kolar 1997-2004
 * This is free software, see GNU Public License version 2 for
 * details.
 *
 * Simple forking WWW Server benchmark:
 *
 * Usage:
 *   webbench --help
 *
 * Return codes:
 *    0 - sucess
 *    1 - benchmark failed (server is not on-line)
 *    2 - bad param
 *    3 - internal error, fork failed
 * 
 */ 
#include "socket.c"
#include <unistd.h>
#include <sys/param.h>
#include <rpc/types.h>
#include <getopt.h>
#include <strings.h>
#include <time.h>
#include <signal.h>
// HTTP协议变更(和源码有关的部分)
// HTTP0.9 只支持GET请求方法,不支持在请求行中指定版本号,不支持请求头
// HTTP1.0 添加了请求方法 GET,HEAD
// HTTP1.1 增加了请求方法 OPTIONS,TRACE
/* values */
volatile int timerexpired = 0; // 判断压测时长是否已经到达设定的时间,1表示压测时间到
int speed = 0;                 // 记录进程成功得到服务器响应的数量
int failed = 0;                // 记录进程没有得到服务器响应的数量
int bytes = 0;                 // 记录进程成功读取的字节数
/* globals */
int http10 = 1;                // HTTP版本, 0表示HTTP0.9, 1表示HTTP1.0, 2表示HTTP1.1
/* Allow: GET, HEAD, OPTIONS, TRACE */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
#define PROGRAM_VERSION "1.5"
int method = METHOD_GET;       // 默认请求方式为GET,也支持HEAD, OPTIONS, TRACE
int clients = 1;               // 并发数目,默认只有一个进程发请求,通过-c参数设置
int force = 0;                 // 
int force_reload = 0;          //
int proxyport = 80;            // 代理服务器的端口(HTTP服务器的端口为80)
char *proxyhost = NULL;        // 
int benchtime = 30;            // 压测时间,默认为30秒,通过-t参数设置
/* internal */
int mypipe[2];                 // 使用管道进行父进程和子进程之间的通信
char host[MAXHOSTNAMELEN];     // 服务端ip
#define REQUEST_SIZE 2048
char request[REQUEST_SIZE];    // 所要发送的HTTP请求

static const struct option long_options[] = {
    {"force", no_argument, &force, 1},
    {"reload", no_argument, &force_reload, 1},
    {"time", required_argument, NULL, 't'},
    {"help", no_argument, NULL, '?'},
    {"http09", no_argument, NULL, '9'},
    {"http10", no_argument, NULL, '1'},
    {"http11", no_argument, NULL, '2'},
    {"get", no_argument, &method, METHOD_GET},
    {"head", no_argument, &method, METHOD_HEAD},
    {"options", no_argument, &method, METHOD_OPTIONS},
    {"trace", no_argument, &method, METHOD_TRACE},
    {"version", no_argument, NULL, 'V'},
    {"proxy", required_argument, NULL, 'p'},
    {"clients", required_argument, NULL, 'c'},
    {NULL, 0, NULL, 0}
};

/* prototypes */
static void benchcore(const char* host, const int port, const char *request);
static int bench(void);
static void build_request(const char *url);

// 到达结束时间时的信号处理函数
static void alarm_handler(int signal) {
    timerexpired = 1;
}	
// 使用webbench的提示函数
// -c: 指定并发进程的个数
// -t: 指定压测时间,以秒为单位
// 支持HTTP0.9,HTTP1.0,HTTP1.1
// 支持GET,HEAD,OPTIONS,TRACE四种请求方法
static void usage(void) {
    fprintf(stderr,
	    "webbench [option]... URL\n"
	    "  -f|--force               Don't wait for reply from server.\n"
	    "  -r|--reload              Send reload request - Pragma: no-cache.\n"
	    "  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
	    "  -p|--proxy <server:port> Use proxy server for request.\n"
	    "  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
	    "  -9|--http09              Use HTTP/0.9 style requests.\n"
	    "  -1|--http10              Use HTTP/1.0 protocol.\n"
	    "  -2|--http11              Use HTTP/1.1 protocol.\n"
	    "  --get                    Use GET request method.\n"
	    "  --head                   Use HEAD request method.\n"
	    "  --options                Use OPTIONS request method.\n"
	    "  --trace                  Use TRACE request method.\n"
	    "  -?|-h|--help             This information.\n"
	    "  -V|--version             Display program version.\n"
	);
};

int main(int argc, char *argv[]) {
    int opt = 0;
    int options_index = 0;
    char *tmp = NULL;

    if (argc == 1) {
	     usage();
        return 2;
    } 

    while((opt = getopt_long(argc, argv, "912Vfrt:p:c:?h", long_options, &options_index)) != EOF ) {
        switch (opt) {
            case 0: 
                break;
            case 'f': 
                force = 1;
                break;
            case 'r': 
                force_reload = 1;
                break; 
            case '9': 
                http10 = 0;
                break;
            case '1': 
                http10 = 1;
                break;
            case '2': 
                http10 = 2;
                break;
            case 'V': 
                printf(PROGRAM_VERSION"\n");
                exit(0);
            case 't': 
                benchtime = atoi(optarg);
                break;	     
            case 'p': 
	     /* proxy server parsing server:port */
	             tmp = strrchr(optarg, ':');
	             proxyhost = optarg;
	             if (tmp == NULL) {
		             break;
	             }
	             if (tmp == optarg) {
		             fprintf(stderr, "Error in option --proxy %s: Missing hostname.\n", optarg);
		             return 2;
	             }
	             if (tmp == optarg + strlen(optarg) -1) {
		             fprintf(stderr, "Error in option --proxy %s Port number is missing.\n", optarg);
		             return 2;
	             }
	             *tmp = '\0';
	             proxyport = atoi(tmp + 1);
                 break;
            case ':':
            case 'h':
            case '?': 
                 usage();
                 return 2;
                 break;
            case 'c': 
                 clients = atoi(optarg);
                 break;
        }
    }
 
    if (optind == argc) {
        fprintf(stderr, "webbench: Missing URL!\n");
		usage();
		return 2;
    }
    // 此处多做一次判断,可预防BUG,因为上文并发数目用户可能写为0
    if (clients == 0) 
        clients = 1;
    // 压力测试时间默认为30s,如果用户写成0,则修改为60s
    if (benchtime == 0) 
        benchtime = 60;
    fprintf(stderr, "Webbench - Simple Web Benchmark "PROGRAM_VERSION"\n" 
                    "Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n");

    // 构建HTTP请求报文的请求行以及请求头
    build_request(argv[optind]);

    // 在屏幕上打印测试信息,如HTTP协议,请求方法,并发个数,请求时间等
    printf("\nBenchmarking: ");
    switch (method) {
	    case METHOD_GET:
	    default:
		    printf("GET");
            break;
	    case METHOD_OPTIONS:
		    printf("OPTIONS");
            break;
	    case METHOD_HEAD:
		    printf("HEAD");
            break;
	    case METHOD_TRACE:
		    printf("TRACE");
            break;
    }
    printf(" %s", argv[optind]);
    switch (http10) {
	    case 0: 
            printf(" (using HTTP/0.9)");
            break;
	    case 2: 
            printf(" (using HTTP/1.1)");
            break;
    }
    printf("\n");
    if (clients == 1) 
        printf("1 client");
    else
        printf("%d clients", clients);

    printf(", running %d sec", benchtime);
    if (force) 
        printf(", early socket close");
    if (proxyhost != NULL) 
        printf(", via proxy server %s:%d", proxyhost, proxyport);
    if (force_reload) 
        printf(", forcing reload");
    printf(".\n");

    // 调用bench函数,开始压力测试,bench()为压力测试核心代码
    return bench();
}
// 该函数生成一个HTTP请求,填充到全局变量request中
// 一个典型的HTTP GET请求如下:
// GET /test.jpg HTTP/1.1
// User-Agent: WebBench 1.5
// Host:192.168.10.1
// Pragma: no-cache
// Connection: close
void build_request(const char *url) {
    // 函数说明:
    // extern char* strstr(char *str1, char *str2);
    //
    // 
    char tmp[10];
    int i;

    bzero(host, MAXHOSTNAMELEN);
    bzero(request, REQUEST_SIZE); // request:所要发送的HTTP请求

    // 调整参数
    // 页面缓存,代理模式, HTTP1.0及以上才支持,重新设定HTTP版本为1.0
    if (force_reload && proxyhost != NULL && http10 < 1) 
        http10 = 1;
    // HEAD 请求方法, HTTP1.0及以上支持,重新设定HTTP版本为1.0
    if (method == METHOD_HEAD && http10 < 1) 
        http10 = 1;
    // OPTIONS 和 TRACE 请求方法, HTTP1.1及以上支持,重新设定HTTP版本为1.1
    if (method == METHOD_OPTIONS && http10 < 2) 
        http10 = 2;
    if (method == METHOD_TRACE && http10 < 2) 
        http10 = 2;

    switch (method) {
	    default:
	    case METHOD_GET: 
            strcpy(request, "GET");
            break;
	    case METHOD_HEAD: 
            strcpy(request, "HEAD");
            break;
	    case METHOD_OPTIONS: 
            strcpy(request, "OPTIONS");
            break;
	    case METHOD_TRACE: 
            strcpy(request, "TRACE");
            break;
    }
	// 添加请求方法后的空格	  
    strcat(request, " ");
    // strstr(str1, str2) 判断str2是否是str1的子串,若是返回str2在str1中首次出现的位置,否则,返回NULL
    // url中不包含://,说明url不合法
    if (NULL == strstr(url, "://")) {
        fprintf(stderr, "\n%s: is not a valid URL.\n", url);
	    exit(2);
    }
    // url太长,不合法
    if (strlen(url) > 1500) {
        fprintf(stderr, "URL is too long.\n");
	    exit(2);
    }
    if (proxyhost == NULL) {
        // 未使用代理服务器的情况下,只允许使用HTTP协议
	    if (0 != strncasecmp("http://", url, 7)) { 
            fprintf(stderr, "\nOnly HTTP protocol is directly supported, set --proxy for others.\n");
            exit(2);
        }
    }
    // 指向"://"后的第一个字母
    i = strstr(url, "://") - url + 3;

    // strchr(const char *s, char c) 查找字符c在字符串s中首次出现的位置
    // URL末尾必须为'/'
    if (strchr(url + i, '/') == NULL) {
        fprintf(stderr, "\nInvalid URL syntax - hostname don't ends with '/'.\n");
        exit(2);
    }
    if (proxyhost == NULL) {
        if (index(url + i, ':') != NULL && index(url + i, ':') < index(url + i, '/')) {
	        strncpy(host, url + i, strchr(url + i, ':') - url - i);
	        bzero(tmp, 10);
	        strncpy(tmp, index(url + i, ':') + 1, strchr(url + i, '/') - index(url + i, ':') - 1);
	        proxyport = atoi(tmp);
	        if (proxyport == 0) 
               proxyport = 80;
        } else {
            strncpy(host, url + i, strcspn(url + i, "/"));
        }
        strcat(request + strlen(request), url + i + strcspn(url + i, "/"));
    } else {
        strcat(request, url);
    }
    // 在请求行上添加HTTP协议版本号(HTTP0.9,不需要添加版本号)
    if (http10 == 1)
	    strcat(request, " HTTP/1.0");
    else if (http10 == 2)
	    strcat(request, " HTTP/1.1");
    // 在请求行添加\r\n    
    strcat(request, "\r\n");
    // HTTP1.0及以上版本,需要在请求头上添加user-agent字段
    if (http10 > 0)
	    strcat(request, "User-Agent: WebBench "PROGRAM_VERSION"\r\n");
    // 
    if (proxyhost == NULL && http10 > 0) {
	    strcat(request, "Host: ");
	    strcat(request, host);
	    strcat(request, "\r\n");
    }
    if (force_reload && proxyhost != NULL) {
	    strcat(request, "Pragma: no-cache\r\n");
    }

    // HTTP1.0及以上版本,需要在请求头上添加connection字段,close表示短连接
    if (http10 > 1)
	    strcat(request, "Connection: close\r\n");

    // HTTP1.0及以上版本,需要再添加一个空行
    if(http10 > 0) 
        strcat(request, "\r\n"); 
}

static int bench(void) {
    int i, j, k;
    pid_t pid = 0;
    FILE *f;

    // 测试下该socket连接是否可以建立
    i = Socket(proxyhost == NULL ? host : proxyhost, proxyport);
    if (i < 0) { 
	    fprintf(stderr, "\nConnect to server failed. Aborting benchmark.\n");
        return 1;
    }
    close(i);
    // 创建管道,管道用于子进程向父进程汇报数据
    if (pipe(mypipe)) {
        // 错误处理
	    perror("pipe failed.");
	    return 3;
    }

    // fork出来clients数量的子进程进行测试
    for (i = 0; i < clients; i++) {
	    pid = fork();  // pid = 0 子进程
                       // pid < 0 出错
	    if (pid <= (pid_t)0) {
            //子进程跳出循环 or fork出错,父进程跳出循环
	        sleep(1); // 子进程睡眠一秒,想让父进程先运行后面的代码
		    break;
	    }
    }

    if (pid < (pid_t)0) { // fork 出错
        // 错误处理
        fprintf(stderr, "problems forking worker no. %d\n", i);
	    perror("fork failed.");
	    return 3;
    }

    if (pid == (pid_t)0) { // 子进程
        if (proxyhost == NULL)
            benchcore(host, proxyport, request);
        else
            benchcore(proxyhost, proxyport, request);

        // 子进程将测试结果输出到管道
	    f = fdopen(mypipe[1], "w");
	    if (f == NULL) {
            // 错误处理
		    perror("open pipe for writing failed.");
		    return 3;
	    }
        // speed: 记录子进程成功得到服务器响应的数量
        // failed: 记录子进程未成功得到服务器响应的数量
        // bytes: 记录子进程成功读取的字节数

        // 子进程将测试结果写进管道
	    fprintf(f, "%d %d %d\n", speed, failed, bytes);
	    fclose(f);
	    return 0;
    } else {// 父进程,从管道读取子进程输出,并汇总
	    f = fdopen(mypipe[0], "r");
	    if (f == NULL) {
		    perror("open pipe for reading failed.");
		    return 3;
	    }
	    setvbuf(f, NULL, _IONBF, 0);
	    speed = 0;
        failed = 0;
        bytes = 0;

	    while (1) {
		    pid = fscanf(f, "%d %d %d", &i, &j, &k);
		    if (pid < 2) {
                fprintf(stderr, "Some of our childrens died.\n");
                break;
            }
		    speed += i;
		    failed += j;
		    bytes += k;
		    if (--clients == 0) 
                break;
	    }
	    fclose(f);

        printf("\nSpeed=%d pages/min, %d bytes/sec.\nRequests: %d susceed, %d failed.\n",
		      (int)((speed + failed) / (benchtime / 60.0f)),
		      (int)(bytes / (float)benchtime),
		      speed,
		      failed);
    }
    return i;
}

// benchcore 函数是子进程进行压力测试的函数,被每个子进程调用
// host: 主机ip地址
// port: 端口
// req:  HTTP请求报文的请求行和请求头
void benchcore(const char *host, const int port, const char *req) {
    int rlen;
    char buf[1500];
    int s, i;
    struct sigaction sa;

    /* setup alarm signal handler */
    // 当程序执行到指定的秒数之后,发送SIGALRM信号,即设置alarm_handler函数为信号处理函数
    sa.sa_handler = alarm_handler;
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, NULL))
        exit(3);
    // 开始计时(每个子进程独立计时)
    alarm(benchtime); //

    rlen = strlen(req);

// 无限执行请求,直到子进程收到SIGALRM信号将timerexpired设置为1时
nexttry:
    while (1) {
        // 一旦超时,则返回
        if (timerexpired) {
            // ? 这里没看懂
            if (failed > 0) {
                failed--;
            }
            return;
        }
        s = Socket(host, port);                          
        if (s < 0) { 
            failed++;
            continue;
        } 
        if (rlen != write(s, req, rlen)) {
            failed++;
            close(s);
            continue;
        }
        if (http10 == 0) {
	        if (shutdown(s, 1)) { 
                failed++;
                close(s);
                continue;
            }
        }
        // force 表示是否需要等待读取从server返回的数据,0表示要等待读取
        if (force == 0) {
             /* read all available data from socket */
	        while (1) {
                if(timerexpired) 
                    break; 
	            i = read(s,buf,1500);
                /* fprintf(stderr,"%d\n",i); */
	            if (i < 0) { 
                    failed++;
                    close(s);
                    goto nexttry;
                } else {
		            if (i == 0) 
                        break;
		            else
			            bytes += i;
                }
	        }
        }
        if (close(s)) {
            failed++;
            continue;
        }
        speed++;
    }
}
