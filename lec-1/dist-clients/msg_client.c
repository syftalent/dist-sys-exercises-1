// CS 6421 - Simple Message Board Client in C
// Yifei Shen G49720084
// Compile with: gcc msg_client -o msg_client
// Run with:     ./msg_client

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>

int main(int argc, char ** argv)
{
    // check the number of arguments
    if(argc != 4){
        printf("the number of arguments isn't vaild\n");
    }else{
        char* server_port = "5555";
        char* server_ip = argv[1];
        struct addrinfo hints, *server;
        int rc;
        
        //create socket and connect to server
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        if ((rc = getaddrinfo(server_ip, server_port, &hints, &server)) != 0) {
                perror(gai_strerror(rc));
                exit(-1);
        }
        
        int sockfd = socket(server->ai_family, server->ai_socktype, server->ai_protocol);
        if (sockfd == -1) { 
                perror("ERROR opening socket");
                exit(-1);
        }
        rc = connect(sockfd, server->ai_addr, server->ai_addrlen);
        if (rc == -1) {
                perror("ERROR on connect");
                close(sockfd);
                exit(-1);
        }
        
        //send message to server then exit
        send(sockfd, argv[2], strlen(argv[2])+1, 0);
        send(sockfd, "\r\n", 2, 0);
        send(sockfd, argv[3], strlen(argv[3])+1, 0);
        send(sockfd, "\r\n", 2, 0);
        
        printf("Done.\n");
        close(sockfd);
        return 0;
    }
}
