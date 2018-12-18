#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#define PORT 8888
#define BUFFER_SIZE 10

void handler(int sig) {
	printf("catch a sigpipe signal\n");
	printf("server is closed\n");
	exit(0);
}

int main(int argc, char **argv) {
    const char *ip = "127.0.0.1";
    char buf[1024];
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in clientaddr;
    memset(&clientaddr, 0, sizeof(clientaddr));
    clientaddr.sin_family = AF_INET;
    clientaddr.sin_addr.s_addr = inet_addr(ip);
    clientaddr.sin_port = htons(PORT);

    signal(SIGPIPE, handler);

    while (1) {
        int ret = connect(sockfd, (struct sockaddr*)&clientaddr, sizeof(clientaddr));
        if (ret < 0) {
            printf("connect failure\n");
            sleep(1);
            continue;
        }
        break;
    } 
    while (1) {
        memset(buf, 0, sizeof(buf));
        fgets(buf, BUFFER_SIZE, stdin);
        send(sockfd, buf, BUFFER_SIZE - 1, 0);
    }
    close(sockfd);
    return 0;
}