#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>

#define MAX_EVENT_NUMBER 1024
#define TCP_BUFFER_SIZE 512
#define UDP_BUFFER_SIZE 512

// 设置套结字fd为非阻塞
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将套结字添加到epollfd指向的内核事件表中
void addfd(int epollfd, int fd) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

int main(int argc, char **argv) {
    if (argc <= 2) {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    
    // TCP socket
    int tcpsock = socket(AF_INET, SOCK_STREAM, 0);
    assert(tcpsock >= 0);
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(ip);
    servaddr.sin_port = htons(port);

    int ret;
    ret = bind(tcpsock, (struct sockaddr*)&servaddr, sizeof(servaddr));
    assert(ret != -1);
    ret = listen(tcpsock, 5);
    assert(ret != -1);

    //UDP socket
    int  udpsock = socket(AF_INET, SOCK_DGRAM, 0);
    assert(udpsock >= 0);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(ip);
    servaddr.sin_port = htons(port);

    ret = bind(udpsock, (struct sockaddr*)&servaddr, sizeof(servaddr));
    assert(ret != -1);

    // epoll
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, tcpsock);
    addfd(epollfd, udpsock);

    while (1) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0) {
            printf("epoll failure\n");
            break;
        }
        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;
            // TCP 监听到连接请求
            if (sockfd == tcpsock) {
                struct sockaddr_in clientaddr;
                socklen_t clientlen = sizeof(clientaddr);
                int connfd = accept(tcpsock, (struct sockaddr*)&clientaddr, &clientlen);
                addfd(epollfd, connfd);
            }
            // UDP 收到数据
            else if (sockfd == udpsock) {
                struct sockaddr_in clientaddr;
                socklen_t clientlen = sizeof(clientaddr);
                char buf[UDP_BUFFER_SIZE];
                memset(buf, '\0', sizeof(buf));

                int count = recvfrom(udpsock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&clientaddr, &clientlen);
                if (count > 0)
                    sendto(udpsock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&clientaddr, clientlen);
            }
            // TCP 收到数据
            else if (events[i].events & EPOLLIN) {
                char buf[TCP_BUFFER_SIZE];
                memset(buf, '\0', sizeof(buf));
                int count = recv(sockfd, buf, sizeof(buf) - 1, 0);
                if (count <= 0) {
                    close(sockfd);
                    break;
                }
                else {
                    printf("get %d bytes of content: %s\n", count, buf);
                    send(sockfd, buf, count, 0);
                }
            }
            else 
                printf("something else happened\n");
        }

    }
    close(tcpsock);
    close(udpsock);
    return 0;
}