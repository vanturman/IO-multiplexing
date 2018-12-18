#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/epoll.h>

#define MAX_EVENT_NUMBER 1024
#define BUFFER_SIZE 10

// 将文件描述符设置为非阻塞的
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
} 

// 将文件描述符 fd 上的 EPOLLIN 注册到 epollfd 指示的 epoll 内核事件表中
// 参数 enable_et 指定是否对 fd 启用 ET 模式
void addfd(int epollfd, int fd, bool enable_et) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN;
    if (enable_et)  // 启用 ET 模式
        event.events |= EPOLLET;

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// LT 模式的工作流程
void lt(epoll_event *events, int number, int epollfd, int listenfd) {
    char buf[BUFFER_SIZE];
    for (int i = 0; i < number; i++) {
        int sockfd = events[i].data.fd;
        if (sockfd == listenfd) {
            struct sockaddr_in clientaddr;
            socklen_t socklen = sizeof(clientaddr);
            int connfd = accept(listenfd, (struct sockaddr*)&clientaddr, &socklen);
            printf("accept ok\n");
            addfd(epollfd, connfd, false);
        }
        else if (events[i].events & EPOLLIN) {
            // LT Model:只要socket读该缓存中还有未读出的数据，这段代码就触发
            printf("level trigger once\n");
            memset(buf, 0, sizeof(buf));
            int ret = recv(sockfd, buf, BUFFER_SIZE - 1, 0);
            if (ret <= 0) {
                close(sockfd);
                continue;
            }
            printf("recv ok\n");
            printf("get %d bytes of content: %s\n", ret, buf);
        }
        else 
            printf("something else happened\n");
    }
}

// ET 模式的工作流程
void et(epoll_event *events, int number, int epollfd, int listenfd) {
    char buf[BUFFER_SIZE];
    for (int i = 0; i < number; i++) {
        int sockfd = events[i].data.fd;
        if (sockfd == listenfd) {
            struct sockaddr_in clientaddr;
            socklen_t socklen = sizeof(clientaddr);
            int connfd = accept(listenfd, (struct sockaddr*)&clientaddr, &socklen);
            printf("accept ok\n");
            addfd(epollfd, connfd, true);
        }
        else if (events[i].events & EPOLLIN) {
            printf("event trigger once\n");
            // 这段代码不会被重复触发，所以循环读取数据,以确保把socket读缓存中的所有数据读出
            while (1) {
                memset(buf, 0, sizeof(buf));
                int ret = recv(sockfd, buf, BUFFER_SIZE - 1, 0);
                if (ret < 0) {
                    // 对于非阻塞IO,下面的条件成立表示数据已经完全读取完毕。此后，epoll就能
					// 再次触发sockfd上的EPOLLIN事件，以驱动下一次读操作
                    if ((errno == EAGAIN) && (errno == EWOULDBLOCK)) {
                        printf("read later\n");
                        break;
                    }
                    close(sockfd);
                    break;
                }
                else if (ret == 0) {
                    close(sockfd);
                    // break;
                }
                else
                    printf("get %d bytes of content: %s\n", ret, buf);
            }
        }
        else
            printf("something else happened\n");
    }
}

// ET 模式的工作流程 2
void et2(epoll_event *events, int number, int epollfd, int listenfd) {
    char buf[BUFFER_SIZE];
    for (int i = 0; i < number; i++) {
        int sockfd = events[i].data.fd;
        if (sockfd == listenfd) {
            struct sockaddr_in clientaddr;
            socklen_t socklen = sizeof(clientaddr);
            int connfd = accept(listenfd, (struct sockaddr*)&clientaddr, &socklen);
            printf("accept ok\n");
            addfd(epollfd, connfd, true);
        }
        else if (events[i].events & EPOLLIN) {
            printf("event trigger once\n");
            // ET模式中，这段代码不会被重复触发，
            // 如果不循环读取数据，缓冲区中还会有未处理的数据，但是不会触发event
            memset(buf, 0, sizeof(buf));
            int ret = recv(sockfd, buf, BUFFER_SIZE - 1, 0);
            if (ret <= 0) {
                close(sockfd);
                continue;
            }
            printf("get %d bytes of content: %s\n", ret, buf);
        }
        else
            printf("something else happened\n");
    }
}

int main(int argc, char **argv) {
    if (argc <= 2)  {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);
    int ret, listenfd;
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    // servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_addr.s_addr = inet_addr(ip);
    servaddr.sin_port = htons(port);
    
    // socket && bind && listen
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    printf("socket ok\n");
    ret = bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    assert(ret != -1);
    printf("bind ok\n");

    ret = listen(listenfd, 5);
    assert(ret != -1);
    printf("listen ok\n");
    
    // epoll
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);  //linux
    addfd(epollfd, listenfd, true);
    while (1) {
        int ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);    // 注意最后一个参数
        if (ret < 0) {
            printf("epoll failure\n");
            break;
        }
        printf("epoll_wait ok\n");

        // lt( events, ret, epollfd, listenfd );	//使用 LT 模式
        et( events, ret, epollfd, listenfd );	//使用 ET 模式(loop)
        // et2( events, ret, epollfd, listenfd );	//使用 ET 模式(without loop)
    }
    close(listenfd);
    return 0;
}