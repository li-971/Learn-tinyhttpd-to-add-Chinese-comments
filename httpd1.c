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
#include <stdint.h>

// 函数指针宏定义 
// int isspace（int x）这是标准库的判断字符是否为空格字符的函数 空格字符返回0 其他返回非零值
#define ISspace(x) isspace((int)(x))
// server_string 服务器字符串
#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"
// 文件描述符 
#define STDIN   0
#define STDOUT  1
#define STDERR  2

void accept_request(void *);// 主要业务逻辑
void bad_request(int);// 错误请求回复
void cat(int, FILE *);// 读文件内容并发给客户端
void cannot_execute(int);// 内部错误回复
void error_die(const char *);// 打印错误，并退出
void execute_cgi(int, const char *, const char *, const char *);// 设置从请求中获取的数据作为环境变量与cgi进行交互，执行程序或回复请求
int get_line(int, char *, int);// 将客户端的一行数据读取到buffer中 （一个请求中包含很多行数据）
void headers(int, const char *);// 服务器响应内容的头部信息
void not_found(int);// 实现指定资源未找到的回复
void serve_file(int, const char *);// 发给客户端回复头
int startup(u_short *);// 启动服务器 并返回绑定的端口号
void unimplemented(int);// 实现请求方法未定义的回复

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void accept_request(void *arg)
{
    // 将参数还原
    int client = (intptr_t)arg;
    char buf[1024];// 存放客户端发来的数据
    size_t numchars;// 存放客户端发来的数据的长度 
    char method[255];// 存放方法的字符串
    char url[255];// URL是Uniform Resource Locator的缩写，意思是统一资源定位符，是一个指向互联网上特定信息资源的位置。
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;/* becomes true if server decides this is a CGI program 如果服务器判断这是一个CGI程序，则为真 */
    char *query_string = NULL;


    // 获取客户端的数据
    numchars = get_line(client, buf, sizeof(buf));
    i = 0; j = 0;
    // 如果buf[i]不是空格并且i要小于254时，将buf的数据赋给method
    while (!ISspace(buf[i]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[i];
        i++;
    }
    j=i;
    // 将method的最后一位赋'\0' 使用strcasecmp的前提条件 以'\0'结尾
    method[i] = '\0';

    // 比较大小 如果method的字符串大于"get"与"post" 则不是get与post方法
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) 
    {
        // 发送方法请求未定义的回复
        unimplemented(client);
        // 结束子进程
        return;
    }
    
    // post方法实例：POST /test.php HTTP/1.1\r\nHost: example.com\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: length\r\n\r\ndata=value&data2=value2

    // 如果是post方法
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

    i = 0;
    // 跳过空格
    while (ISspace(buf[j]) && (j < numchars))
        j++;
    // 如果buf[j]不是空格并且i要小于254且j在接受到的字符串范围内时，将buf的数据赋给url
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numchars))
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';

    // get方法实例：GET /test.php?data=value&data2=value2 HTTP/1.1\r\nHost: example.com\r\n\r\n

    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;// 如果是get方法 字符串为 /test.php?data=value&data2=value2 
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }
    // url 确定为"/test.php"

    // path路径为htdocs文件夹下+url的地址
    sprintf(path, "htdocs%s", url);
    // 如果路径以斜杠/结尾，则将index.html附加到路径末尾。它的作用是在请求的路径是目录时，可以显示该目录下的index.html文件。
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
    
    // 使用stat函数检查文件路径是否有效，如果结果为-1，表明该文件路径无效。
    if (stat(path, &st) == -1) {
        // 如果客户端发来数据，并且"\n"的值大于buf时 
        //（比'\n'小的值包括：'\t'、'\r'、' '、'!'、'"'、'#'、'$'、'%'、'&'、'''、'('、')'、'*'、'+'、','、'-'、'.'。）
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers 读取和丢弃头部信息 */
            // 丢弃头部信息是由于它们只包含有关请求的信息，但不包含有关响应的信息，而服务器要进行响应，所以就需要丢弃头部信息
            numchars = get_line(client, buf, sizeof(buf));
        
        not_found(client);
    }
    else //文件路径有效
    {
        // 如果if条件为真，则表示该文件是一个目录，并返回true
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            // 则将/index.html附加到路径末尾。它的作用是在请求的路径是目录时，可以显示该目录下的index.html文件。
            strcat(path, "/index.html");

        // 如果if条件为真，则表示该文件有可执行权限，并返回true。
        // 具体的权限分别是st.st_mode & S_IXUSR(用户权限)，st.st_mode & S_IXGRP(组权限)和st.st_mode & S_IXOTH(其他权限)
        if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
            cgi = 1;
        // 如果cgi为0，
        if (!cgi)
            // 没有cgi参与 服务器将指定文件传输给客户端
            serve_file(client, path);
        else
            // 与cgi配合，回复客户端数据
            execute_cgi(client, path, method, query_string);
    }

    // 关闭客户端套接字，结束子线程
    close(client);
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");// 浏览器发送了错误的请求
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");// 例如没有内容长度的帖子。
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
    char buf[1024];
    // 读取一行数据
    fgets(buf, sizeof(buf), resource);
    // 如果文件没有读到末尾
    while (!feof(resource))
    {
        // 不发末尾行
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");// 这是一个HTTP协议状态码，表示服务器在处理请求时发生了内部错误。
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");// 错误禁止CGI执行。
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
    perror(sc);
    exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path,
        const char *method, const char *query_string)
{
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A'; buf[1] = '\0';
    // 如果是get方法
    // get方法实例：GET /test.php?data=value&data2=value2 HTTP/1.1\r\nHost: example.com\r\n\r\n
    // query_string 目前为 data=value&data2=value2
    if (strcasecmp(method, "GET") == 0)
        // 只要buf中有数据且buf不等于'\n' 就继续读下去
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
    // 如果是post方法        
    else if (strcasecmp(method, "POST") == 0) /*POST*/
    {
        // post方法实例：POST /test.php HTTP/1.1\r\nHost: example.com\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: length\r\n\r\ndata=value&data2=value2

        // 截取Content-Length:字段后的数据，并将其转换为整数
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        // 如果长度等于-1 应该是个错误的请求
        if (content_length == -1) {
            // 返回错误请求响应
            bad_request(client);
            return;
        }
    }
    else/*HEAD or other*/ // HEAD是HTTP/1.1协议中的一种方法，它要求服务器返回与一个特定资源相关的首部信息，而不传回该资源的内容
    {
    }

    // 创建cgi_output的输入输出文件描述符
    if (pipe(cgi_output) < 0) {
        cannot_execute(client);
        return;
    }
    // 创建cgi_input的输入输出文件描述符
    if (pipe(cgi_input) < 0) {
        cannot_execute(client);
        return;
    }
    // 创建子进程
    if ( (pid = fork()) < 0 ) {
        cannot_execute(client);
        return;
    }
    // 发送报文头/回复客户端头部信息
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    
    // 子进程
    if (pid == 0)  /* child: CGI script */
    {
        char meth_env[255];// 请求方法
        char query_env[255];
        char length_env[255];
        // Dup2是一种Linux系统调用，用于复制文件描述符并关闭旧的文件描述符。
        // 它可用于重定向标准输入/输出，也可用于实现更复杂的文件I/O功能。
        dup2(cgi_output[1], STDOUT);
        dup2(cgi_input[0], STDIN);
        // 关闭父进程使用的文件描述符
        close(cgi_output[0]);
        close(cgi_input[1]);

        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        // putenv()是一个C语言标准库函数，用于将给定变量添加到当前进程的环境变量列表中。
        putenv(meth_env);
        
        // 如果是get方法
        if (strcasecmp(method, "GET") == 0) {
            // QUERY_STRING是一个HTTP环境变量，用来检索URL中的查询字符串（即，?后的字符串）。它可以被应用程序使用来处理HTTP GET请求。
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else {   /* POST */
            // 将长度设置为进程环境变量
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        // execl是一个C语言标准库函数，用于在当前进程中执行另一个程序。它接受一组参数，指定要执行的文件名和文件参数。
        // 执行 index.html或其他可执行文件
        execl(path, NULL);
        // 退出子进程
        exit(0);
    } else {    /* parent */
        // 关闭子进程使用的文件描述符
        close(cgi_output[1]);
        close(cgi_input[0]);

        if (strcasecmp(method, "POST") == 0)
            // 将content_length的后面长度的内容发给cgi
            for (i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        // 读cgi返回的字符串，发给客户端    
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);
        // 关闭文件描述符
        close(cgi_output[0]);
        close(cgi_input[1]);
        // 等待子进程退出
        waitpid(pid, &status, 0);
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
 * 从socket获取一行，无论该行以换行符、回车符还是CRLF(/r/n)组合结束。以空字符结束读取的字符串。
 * 如果在缓冲区末尾之前没有找到换行符，则字符串以null结尾。
 * 如果读取了上述3个行终止符中的任何一个，则字符串的最后一个字符将是换行符，字符串将以空字符结束。
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    // 将从客户端读取的数据存入buf中，数据遇到'\r' '\n' '\r\n' 时截至，返回接受到的字符个数 buffer结尾数据样式为 正常数据串+'\n'+'\0'
    while ((i < size - 1) && (c != '\n'))
    {
        // 从socket中获取一个字符存入c中 （阻塞）
        n = recv(sock, &c, 1, 0); 
        /* DEBUG printf("%02X\n", c); */
        if (n > 0) // <0 出错 =0 连接关闭 >0 接收到数据大小
        {
            // 如果字符中出现'\r'时，将'\r'替换为'\n'
            if (c == '\r') 
            {
                // MSG_PEEK 标志 可以读取缓存中的数据，但缓存区中的数据无变化，还可被其他程序读取。
                n = recv(sock, &c, 1, MSG_PEEK);

                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n')) // 如果有数据并且数据为'\n'时，c='\n'
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';

            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';

    return(i);
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type 可以使用filename确定文件类型 */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");//服务器无法完成任务
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");//您的请求是因为指定的资源
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");//不可用或不存在。
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A'; buf[1] = '\0';
    // 读 然后丢弃头部数据
    // 在处理http请求时，服务器会接收客户端发送的一个get/post请求，其中包含了请求行、请求报头信息和请求实体等内容，
    // 由于请求报头信息和请求实体会存储着客户端发送给服务器的数据，而请求行则只包含客户端想要访问服务器上的哪个资源以及使用何种协议，
    // 所以服务器只需要读取并丢弃请求行，不必读取请求报头信息和请求实体
    while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
        numchars = get_line(client, buf, sizeof(buf));

    // 读文件，返回文件描述符
    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
        // 向客户端发送 头部信息与文件内容
        headers(client, filename);
        cat(client, resource);
    }
    // 关闭文件描述符
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
int startup(u_short *port)
{
    int httpd = 0;
    int on = 1;
    struct sockaddr_in name;

    // 创建服务器套接字 （IP/TCP协议）
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");
    
    // 将ip.addr设为0.0.0.0 （获取本机ip.addr）
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    // SO_REUSEADDR是让端口释放后立即就可以被再次使用。     
    // SO_REUSEADDR用于对TCP套接字处于TIME_WAIT状态下的socket，允许重复绑定使用。
    // 设置httpd套接字属性。
    if ((setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)  
    {  
        error_die("setsockopt failed");
    }
    // 将套接字与name进行绑定
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");
    
    if (*port == 0)  /* if dynamically allocating a port 如果动态分配端口 */ 
    {
        socklen_t namelen = sizeof(name);
        // 获取与套接字绑定的name的信息
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
            error_die("getsockname");
        // 获取动态分配端口的
        *port = ntohs(name.sin_port);
    }
    // 监听端口 将套接字改为监听状态
    if (listen(httpd, 5) < 0)
        error_die("listen");
    // 返回端口号
    return(httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
    char buf[1024];
    // http请求的不是get和post方法，所以拒绝了请求 
    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    // 发送服务器版本字符串
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    // 标识发送到客户端的内容类型为HTML文本
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    
    // \r\n 标识thml段落的开始与结束
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");// 未实现的方法
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");// 不支持HTTP请求方法
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void)
{
    // 初始化参数
    int server_sock = -1;
    u_short port = 4000;
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t  client_name_len = sizeof(client_name);
    pthread_t newthread;

    // 启动服务器
    server_sock = startup(&port);
    printf("httpd running on port %d\n", port);

    // 循环等待客户端连接
    while (1)
    {
        // 等待客户端连接
        client_sock = accept(server_sock,(struct sockaddr *)&client_name,&client_name_len);
        if (client_sock == -1)
            error_die("accept");
        
        // 业务逻辑在子线程
        // 主线程继续等待客户端连接
        /* accept_request(&client_sock); */
        // 创建一个线程，使用默认属性，线程业务实现为 accept_request 给线程传递一个参数 为client_sock
        if (pthread_create(&newthread , NULL, (void *)accept_request, (void *)(intptr_t)client_sock) != 0)
            perror("pthread_create");
    }

    // 关闭服务器的套接字
    close(server_sock);

    return(0);
}
