#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <signal.h>
#include <netdb.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <stdbool.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <limits.h>
#include <time.h>
#include <arpa/inet.h>

#define BACKLOG 3

#define ERR(source) (perror(source),\
		     fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
		     exit(EXIT_FAILURE))
		     
#define HERR(source) (fprintf(stderr,"%s(%d) at %s:%d\n",source,h_errno,__FILE__,__LINE__),\
		     exit(EXIT_FAILURE))

int sethandler( void (*f)(int), int sigNo);

int make_socket(int domain, int type);

int bind_tcp_socket(uint16_t port);

int add_new_client(int sfd);

ssize_t bulk_read(int fd, char *buf, size_t count);

ssize_t bulk_write(int fd, char *buf, size_t count);

size_t bulk_write_line(int fd, char *buf, size_t count);

struct sockaddr_in make_address(char *address, uint16_t port);

int connect_socket(char *name, uint16_t port);

int make_socketC(void);



