// Super basic socket server that sends 1000 bytes of random data to the client
// And then closes the connection

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/mptcp.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>

int main(int argc, char **argv)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_MPTCP);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8888);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    listen(sockfd, 10);
    while (1)
    {
        int newsockfd = accept(sockfd, NULL, NULL);
        char *buffer = malloc(1000);
        memset(buffer, 'a', 1000);
        send(newsockfd, buffer, 1000, 0);
        close(newsockfd);
    }
    close(sockfd);
    return 0;
}