/* 
 * tcpclient.c - A simple TCP client
 * usage: tcpclient <host> <port>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 

#define IOTCOMM_PORT 2060

/* 
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(0);
}

static char reply1[] = "HIBP/1.0 200 OK";
static char reply2[] = "HIBP/1.0 400 Failure";
static char reply3[] = "HIBP/1.0 420 Unknown Protocol";

int main(int argc, char **argv) {
    int sockfd, portno, n;
    int recvn;
    struct sockaddr_in serveraddr;
    //struct sockaddr serveraddr;
    struct hostent *server;
    char *hostname;
    char *message , server_reply[2000];

    /* check command line arguments */
    if (argc != 2) {
       fprintf(stderr,"usage: %s <hostname>\n", argv[0]);
       exit(0);
    }
    hostname = argv[1];
    portno = IOTCOMM_PORT;

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
	  (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(portno);

    /* connect: create a connection with the server */
    if (connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) 
      error("ERROR connecting");

    puts("Connected\n");
     
    //keep communicating with server
    while (1) {
        //printf("Enter message : ");
        message = "REGISTER username=A1234567980 type=user\r\n";
         
        //Send some data
        if( send(sockfd , message , strlen(message) , 0) < 0) {
            puts("Send failed");
            return 1;
        }
         
        //Receive a reply from the server
        if( (recvn=recv(sockfd , server_reply , 2000 , 0)) < 0) {
            puts("recv failed");
            break;
        }
         
        puts("Server reply :");
        puts(server_reply);
        memset(server_reply, 0, 2000);

        if (strncasecmp(reply1, server_reply, strlen(reply1)) >= 0 ) {
           message = "JOIN group=YDH01002";
        } else {
           message = "LOGOUT";
        }

        memset(server_reply, 0, 2000);
        //Send some data
        if( send(sockfd , message , strlen(message) , 0) < 0) {
            puts("Send failed");
            return 1;
        }
         
        //Receive a reply from the server
        if( recv(sockfd , server_reply , 2000 , 0) < 0) {
            puts("recv failed");
            break;
        }
         
        puts("Server reply :");
        puts(server_reply);
        memset(server_reply, 0, 2000);

        while(1) {
            //Receive a message from the server
            if( recv(sockfd , server_reply , 2000 , 0) < 0) {
                puts("recv failed");
                break;
            }
         
            puts("Server message :");
            puts(server_reply);
            memset(server_reply, 0, 2000);

            message = "HIBP/1.0 200 OK\r\n";
            //Send some data
            if( send(sockfd , message , strlen(message) , 0) < 0) {
                puts("Send failed");
                return 1;
            }           
        }
        break;
    }
    close(sockfd);
    return 0;
}
