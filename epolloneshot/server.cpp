#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <assert.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <pthread.h>

#define MAX_EVENT_NUMBER 1024
#define BUFFER_SIZE 20

typedef struct fds {
    int epollfd;
    int sockfd;
}fds_t;

int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将fd上的EPOLLIN和EPOLLET事件注册到epollfd指示的epoll内核事件表
// 参数oneshot指定是否注册fd上的EPOLLONESHOT事件
void addfd(int epollfd, int fd, bool oneshot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    if (oneshot) 
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event); 
    setnonblocking(fd);
}

// 重置fd上的事件。这样操作之后，尽管fd上的EPOLLONESHOT事件被注册，
// 但是操作系统仍然会触发fd上的EPOLLIN事件，且只触发一次
void reset_oneshot(int epollfd, int fd) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 工作线程
void *worker(void *arg) {
    pthread_detach(pthread_self());
    int epollfd = ((fds*)arg)->epollfd;
    int sockfd = ((fds *)arg)->sockfd;

    char buf[BUFFER_SIZE];
    memset(buf, 0, sizeof(buf));
    // 循环读取sockfd上的数据，直到遇到EAGAIN错误
    while (1) {
        int ret = recv(sockfd, buf, BUFFER_SIZE - 1, 0);
        if (ret == 0) {
            close(sockfd);
            printf("client closed the connection\n");
            break;
        }
        else if (ret < 0) {
            if ((errno == EAGAIN) | (errno == EWOULDBLOCK)) {
                reset_oneshot(epollfd, sockfd);
                printf("read later\n");
                break;
            }
            break;
        }
        else {
            printf("get content: %s\n", buf);
            // 休眠5s，模拟数据处理过程           
            sleep(5);
        }
    }
    printf("end thread receiving data on fd: %d\n", sockfd);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc <= 2) {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(ip);
    servaddr.sin_port = htons(port);

    // socket && bind && listen
    int ret;
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    ret = bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    assert(ret != -1);
    ret = listen(listenfd, 5);
    assert(ret != -1);

    // 注册listenfd上的EPOLLIN事件，但是不能再listenfd上注册EPOLLONESHOT事件
    // 否则应用程序只能处理一个客户连接。因为后续的客户连接请求将不再触发listenfd
    // 上的EPOLLIN事件
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    addfd(epollfd, listenfd, false);
    while (1) {
        int ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (ret < 0) {
            printf("epoll failure\n");
            break;
        }
        for (int i = 0; i < ret; i++) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                struct sockaddr_in clientaddr;
                socklen_t socklen = sizeof(clientaddr);
                int connfd = accept(listenfd, (struct sockaddr*)&clientaddr, &socklen);
                addfd(epollfd, connfd, true);
            }
            else if (events[i].events & EPOLLIN) {
                // 启动一个新线程处理
                pthread_t pid;
                fds_t fds_worker;
                fds_worker.epollfd = epollfd;
                fds_worker.sockfd = sockfd;
                pthread_create(&pid, NULL, worker, (void *)&fds_worker);
            }
            else 
                printf("something else happened\n");
        }
    }
    close(listenfd);
    return 0;
}