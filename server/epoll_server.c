#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define MAXEVENTS 		64
#define DEFAULT_PORT		2060
#define MAXCLIENTS		1024

#define SHOW_LINKLIST		true

struct client_info {
       int fd;
       char ipaddr[NI_MAXHOST];
       char port[NI_MAXSERV];
       char type;
       char username[255];
       char group[20];
       char message[50];
       struct client_info* next;
};

struct client_info *client_head = NULL;
struct client_info *client_current = NULL;
struct client_info *client_prev = NULL;
struct client_info *client_end = NULL;

static char reply1[] = "HIBP/1.0 200 OK\r\n";
static char reply2[] = "HIBP/1.0 400 Failure\r\n";
static char reply3[] = "HIBP/1.0 420 Unknown Protocol\r\n";

/*
 *  function name : make_socket_non_blocking
 *  parameters    : integer [file descriptor id]
 *  return        : 0 for sucessful and -1 for failure
 */
static int make_socket_non_blocking (int sfd)
{
    int flags, s;

    flags = fcntl (sfd, F_GETFL, 0);
    if (flags == -1) {
        perror ("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    s = fcntl (sfd, F_SETFL, flags);
    if (s == -1) {
      perror ("fcntl");
      return -1;
    }

    return 0;
}

/*
 *  function name : create_and_bind
 *  parameters    : character pointer [port]
 *  return        : file descriptor sfd or -1 if is failure
 */
static int create_and_bind (char *port)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s, sfd;

    memset (&hints, 0, sizeof (struct addrinfo));
    hints.ai_family = AF_UNSPEC;     /* Return IPv4 and IPv6 choices */
    hints.ai_socktype = SOCK_STREAM; /* We want a TCP socket */
    hints.ai_flags = AI_PASSIVE;     /* All interfaces */

    s = getaddrinfo (NULL, port, &hints, &result);
    if (s != 0) {
        fprintf (stderr, "getaddrinfo: %s\n", gai_strerror (s));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket (rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) continue;

        s = bind (sfd, rp->ai_addr, rp->ai_addrlen);
        if (s == 0) {
          /* We managed to bind successfully! */
          break;
        }

        close (sfd);
    }

    if (rp == NULL) {
        fprintf (stderr, "Could not bind\n");
        return -1;
    }

    freeaddrinfo (result);

    return sfd;
}


char replymessage[1000];
int  message_flag = 0;

char* process_read(struct client_info *current, char *buff, ssize_t size)
{
    char typestr[10];

    memset(typestr, 0, 10);
    memset(replymessage, 0, 1000);
    if (strncasecmp(buff, "REGISTER", 8) == 0) {
       bcopy(reply1, replymessage, strlen(reply1));
       sscanf(buff, "REGISTER username=%s type=%s\r\n", current->username, typestr);
       if( strncasecmp(typestr, "user", 4) == 0) {
           current->type = 'u';
       } else if( strncasecmp(typestr, "group", 4) == 0) {
           current->type = 'g';
       }
       printf("Current User : fd = %d, ipaddr = %s, port = %s, username = %s user-type = %c \n", current->fd, current->ipaddr, current->port, current->username, current->type);
    } else if (strncasecmp(buff, "JOIN", 4) == 0) {
       if (current->type == 'u') {
           bcopy(reply1, replymessage, strlen(reply1));
           memset(current->group, 0, 20);
           sscanf(buff, "JOIN group=%s\r\n",  current->group);
           printf("Current User : fd = %d, ipaddr = %s, port = %s, username = %s user-type = %c group = %s\n", current->fd, current->ipaddr, current->port, current->username, current->type, current->group);
       } else {
           bcopy(reply3, replymessage, strlen(reply3));
       }
    } else if (strncasecmp(buff, "MESSAGE", 7) == 0) {
       bcopy(reply1, replymessage, strlen(reply1));
       memset(current->message, 0, 50);
       bcopy(buff, current->message, 50);
       printf("Current User : fd = %d, ipaddr = %s, port = %s, username = %s message = %s\n", current->fd, current->ipaddr, current->port, current->username, current->message);
       message_flag = 1;
    } else if (strncasecmp(buff, "LOGOUT", 6) == 0) {
       bcopy(reply1, replymessage, strlen(reply1));
    } else if (strncasecmp(buff, reply1, strlen(reply1)) >= 0) {
       memset(replymessage, 0, 1000);
    } else {
       bcopy(reply3, replymessage, strlen(reply3));
    }
    if (replymessage == NULL) bcopy(reply2, replymessage, strlen(reply2));
    return replymessage;
}

int process_message(struct client_info *current)
{
    int s=0;

    client_current = client_head;
    while (client_current != NULL) {
        if ((client_current->type == 'u') && (strcmp(current->username, client_current->group) == 0) ) {
            /* Write the reply to connection */
            s = write (client_current->fd, current->message, strlen(current->message));
            if (s == -1) {
                perror ("write");
                abort ();
            } /* if (s == -1) */
            
        }
        client_end = client_current;
        client_current = client_current->next;
    } /* while (client_current != NULL) */
}

/*
 *  main function
 */
int main(int argc, char *argv[])
{

    char portnum[6];
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV], rbuf[1024];
    char *reply = NULL;
    int  sfd, s;
    int  efd;
    int  infd;
    int  i, n;    
    int  done = 0;
    struct epoll_event event;
    struct epoll_event *events;
    struct sockaddr in_addr;
    socklen_t in_len;
    ssize_t count;
    
    /* epoll_server [port] */
    if (argc == 2) {  
        memset(portnum, 0, sizeof portnum);
        sprintf(portnum, "%s", argv[1]);
    } else if(argc == 1) {
        memset(portnum, 0, sizeof portnum);
        sprintf(portnum, "%i", DEFAULT_PORT);
    }

    sfd = create_and_bind (portnum);
    if (sfd == -1) abort ();

    s = make_socket_non_blocking (sfd);
    if (s == -1) abort ();

    s = listen (sfd, SOMAXCONN);
    if (s == -1) {
        perror ("listen");
        abort ();
    }

    efd = epoll_create1 (0);
    if (efd == -1) {
        perror ("epoll_create");
        abort ();
    }

    event.data.fd = sfd;
    event.events = EPOLLIN | EPOLLET;
    s = epoll_ctl (efd, EPOLL_CTL_ADD, sfd, &event);
    if (s == -1) {
        perror ("epoll_ctl");
        abort ();
    }

    /* Buffer where events are returned */
    events = calloc (MAXEVENTS, sizeof event);

    /* The event loop */
    while (1) {

        n = epoll_wait (efd, events, MAXEVENTS, -1);
        for (i = 0; i < n; i++) {
             if ((events[i].events & EPOLLERR) ||
                 (events[i].events & EPOLLHUP) ||
                 (!(events[i].events & EPOLLIN))) {
                 /* An error has occured on this fd, or the socket is not
                  * ready for reading (why were we notified then?) */
	         fprintf (stderr, "epoll error. events=%u\n", events[i].events);
                 client_current = client_head;
                 while (client_current != NULL) {
                     if (client_current->fd == events[i].data.fd) break;
                     client_prev = client_current;
                     client_current = client_current->next;
                 } /* while (client_current != NULL) */

                 if (client_current != client_head) {
                     client_prev->next = client_current->next;
                 } else {
                     client_head = client_current->next;
                 } /* if (client_current != client_head) */
 
                 free(client_current);
#if defined(SHOW_LINKLIST)
                 client_current = client_head;
                 while (client_current != NULL) {
                     printf("client list : fd = %d (%s:%s)\n", client_current->fd, client_current->ipaddr, client_current->port);
                     client_end = client_current;
                     client_current = client_current->next;
                 } /* while (client_current != NULL) */
#endif
	         close (events[i].data.fd);
	         continue;
             } else if (sfd == events[i].data.fd) {
                 /* We have a notification on the listening socket, which
                  * means one or more incoming connections. */
                 while (1) {
                     in_len = sizeof in_addr;
                     infd = accept (sfd, &in_addr, &in_len);
                     if (infd == -1) {
                         printf("errno=%d, EAGAIN=%d, EWOULDBLOCK=%d\n", errno, EAGAIN, EWOULDBLOCK);
                         if ((errno == EAGAIN) ||
                             (errno == EWOULDBLOCK)) {
                             /* We have processed all incoming
                              * connections. */
                             printf ("processed all incoming connections.\n");
                             break;
                         } else {
                             perror ("accept");
                             break;
                         } /* if (( errno == EAGAIN) || ... */
                     } /* if (infd == -1) */
                         
                     s = getnameinfo (&in_addr, in_len,
                                      hbuf, sizeof hbuf,
                                      sbuf, sizeof sbuf,
                                      NI_NUMERICHOST | NI_NUMERICSERV);                  
                     if (s == 0) {
                         printf("Accepted connection on descriptor %d "
                                "(host=%s, port=%s)\n", infd, hbuf, sbuf);
                         client_current = malloc(sizeof(struct client_info));

                         bcopy(hbuf, client_current->ipaddr, sizeof hbuf);
                         bcopy(sbuf, client_current->port, sizeof sbuf);
                         client_current->fd = infd;
                         client_current->type = 0;
                         memset(client_current->username, 0, 255);
                         client_current->next = NULL;
                         if (client_head == NULL) {
                             client_head = client_current;
                             client_end = client_head;
                         } else {
                             client_end->next = client_current;
                             client_end = client_current;
                         } /* if (client_head == NULL) */
#if defined(SHOW_LINKLIST)
                         client_current = client_head;
                         while (client_current != NULL) {
                             printf("client list : fd = %d (%s:%s)\n", client_current->fd, client_current->ipaddr, client_current->port);
                             client_current = client_current->next;
                         } /* while (client_current != NULL) */
#endif
                     } /* if (s == 0) */

                     /* Make the incoming socket non-blocking and add it to the
                      * list of fds to monitor. */
                     s = make_socket_non_blocking (infd);
                     if (s == -1) abort ();

                     event.data.fd = infd;                     
                     event.events = EPOLLIN | EPOLLET;
                     printf("set events %u, infd=%d\n", event.events, infd);
                     s = epoll_ctl (efd, EPOLL_CTL_ADD, infd, &event);
                     if (s == -1) {
                         perror ("epoll_ctl");
                         abort ();
                     } /* if (s == -1) */
                 } /* while (1) */

             } else {
                 /* We have data on the fd waiting to be read. Read and
                  * display it. We must read whatever data is available
                  * completely, as we are running in edge-triggered mode
                  * and won't get a notification again for the same
                  * data. */
                 done = 0;
                 while (1) {
                     memset(rbuf, 0, sizeof(rbuf) );
                     count = read(events[i].data.fd, rbuf, sizeof(rbuf));
                     if (count == -1) {
                         /* If errno == EAGAIN, that means we have read all
                          * data. So go back to the main loop. */
                         if (errno != EAGAIN) {
                             perror ("read");
                             done = 1;
                         } /* if (errno != EAGAIN) */
                         break;
                     } else if (count == 0) {
                         /* End of file. The remote has closed the
                          * connection. */
                         done = 1;
                         break;
                     } 

                     client_current = client_head;
                     while (client_current != NULL) {
                         if (client_current->fd == events[i].data.fd) break;
                         client_current = client_current->next;
                     } /* while (client_current != NULL) */
                     printf("Recv : (%s:%s) fd = %i => %s\n", client_current->ipaddr, client_current->port, events[i].data.fd, rbuf);

                     reply = process_read(client_current, rbuf, count);

                     if(reply[0] == 0x00) continue;

                     /* Write the reply to connection */
                     s = write (events[i].data.fd, reply, strlen(reply));
                     if (s == -1) {
                         perror ("write");
                         abort ();
                     } /* if (s == -1) */

                     if (message_flag != 0) {
                         process_message(client_current);
                     }

                 } /* while (1) */

                 if (done) {
                     printf ("Closed connection on descriptor %d\n",
                              events[i].data.fd);
                     client_current = client_head;
                     while (client_current != NULL) {
                         if (client_current->fd == events[i].data.fd) break;
                         client_prev = client_current;
                         client_current = client_current->next;
                     } /* while (client_current != NULL) */

                     if (client_current != client_head) {
                         client_prev->next = client_current->next;
                     } else {
                         client_head = client_current->next;
                     } /* if (client_current != client_head) */

                     free(client_current);
#if defined(SHOW_LINKLIST)
                     client_current = client_head;
                     while (client_current != NULL) {
                         printf("client list : fd = %d (%s:%s)\n", client_current->fd, client_current->ipaddr, client_current->port);
                         client_end = client_current;
                         client_current = client_current->next;
                     } /* while (client_current != NULL) */

#endif
                     /* Closing the descriptor will make epoll remove it
                      * from the set of descriptors which are monitored. */
                     close (events[i].data.fd);
                 } /* if (done) */

             } /* if ((events[i].events & EPOLLERR) || ... */

        } /* for (i=0; i < n; i++) */

    } /* while (1) */

    free (events);

    client_current = client_head;
    while (client_current != NULL) {
       client_prev = client_current;
       client_current = client_current->next;
       free(client_prev);       
    } /* while (client_current != NULL) */

    if (sfd > 0) close (sfd);

    return EXIT_SUCCESS;
}




