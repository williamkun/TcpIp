#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <assert.h>

#include "list.h"

#define BUFSIZE 512 
#define MAXCONN 200
#define MAX_EVENTS MAXCONN

struct sock_opt
{
	int fd;
	int (* do_task)(struct sock_opt *p_so);
	struct hlist_node hlist;
};

typedef struct sock_opt SOCKOPT;

int send_reply(struct sock_opt *);
int create_conn(struct sock_opt *);
int init(int);
int intHash(int);
void setnonblocking(int);

struct hlist_head fd_hash[MAXCONN];
int epfd;
int num;
struct epoll_event *events;

static void bail(const char *on_what)
{
	fprintf(stderr,"%s",strerror(errno));
	fprintf(stderr,":");
	fprintf(stderr,"%s",on_what);
	fprintf(stderr,"\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	int listen_fd;
	struct sockaddr_in server_addr;
	int portnumber;
	int nev;
	unsigned hash;
	struct hlist_node *n;
	struct sock_opt *p_so;

	if(argc != 2)
	{
		fprintf(stderr,"Usage : %s portnumber\n",argv[0]);
		exit(1);
	}
	epfd = epoll_create(MAXCONN);
	if((portnumber = atoi(argv[1])) < 0)
	{
		fprintf(stderr,"Usage : %s portnumber\n",argv[0]);
		exit(1);
	}
	if((listen_fd = socket(PF_INET, SOCK_STREAM, 0)) == -1)
	{
		fprintf(stderr,"Socket error: %s\n",strerror(errno));
		exit(1);
	}
	setnonblocking(listen_fd);
	memset(&server_addr, 0, sizeof server_addr);
	server_addr.sin_family = PF_INET;
	server_addr.sin_port = htons(portnumber);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(listen_fd, (struct sockaddr *)(&server_addr), sizeof server_addr) == -1)
	{
		fprintf(stderr,"Bind error: %s\n",strerror(errno));
		exit(1);
	}
	if(listen(listen_fd, 5) == -1)
	{
		fprintf(stderr,"Listen error: %s\n",strerror(errno));
		exit(1);
	}
	if(init(listen_fd))
		bail("Init()");
	printf("Server is waiting for connect now...\n");
	int i;
	for(;;)
	{
		nev = epoll_wait(epfd, events, MAX_EVENTS,-1);
		if(nev < 0)
		{
			fprintf(stderr,"Epoll_wait error: %s\n",strerror(errno));
			free(events);
			exit(1);
		}
		for(i = 0; i < nev; ++i)
		{
			hash = intHash(events[i].data.fd) & MAXCONN;
			hlist_for_each_entry(p_so, n, &fd_hash[hash], hlist)
			{
				if(p_so->fd == events[i].data.fd)
					p_so->do_task(p_so);
			}
		}
	}
	return 0;
}

void setnonblocking(int fd)
{
	int opts;
	opts = fcntl(fd, F_GETFL);
	if(opts < 0)
		bail("Fcntl()");
	opts |= O_NONBLOCK;
	if(fcntl(fd, F_SETFL, opts))
		bail("Fcntl()");
}

int intHash(int fd)
{
	fd += ~(fd << 15);
	fd ^= (fd >> 10);
	fd += (fd << 3);
	fd ^= (fd >> 6);
	fd += ~(fd << 11);
	fd ^= (fd >> 16);
	return fd;
}

int init(int fd)
{
	struct sock_opt *p_so;
	struct epoll_event ev;
	int ret;
	unsigned int hash;
	assert(hlist_empty(&fd_hash[0]));
	num = 0;
	if((p_so = malloc(sizeof(SOCKOPT))) == NULL)
	{
		fprintf(stderr,"Malloc error : %s\n",strerror(errno));
		exit(1);
	}
	if((events = malloc(sizeof(struct epoll_event) * MAXCONN)) == NULL)
	{
		fprintf(stderr,"Malloc error\n");
		exit(1);
	}
	p_so->fd = fd;
	p_so->do_task = create_conn;
	hash = intHash(fd) & MAXCONN;
	hlist_add_head(&p_so->hlist, &fd_hash[hash]);
	ev.data.fd = fd;
	ev.events = EPOLLIN;
	ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
	if(ret)
		bail("Epoll_ctl()");
	num++;
	return 0;
}

int create_conn(struct sock_opt *p_so)
{
	struct sockaddr_in client_addr;
	unsigned int hash;
	int ret;
	struct epoll_event ev;
	int client_fd;
	socklen_t sin_size;
	sin_size = sizeof(struct sockaddr_in);
	if((client_fd = accept(p_so->fd, (struct sockaddr *)(&client_fd), &sin_size)) == -1)
	{
		fprintf(stderr,"Accept error : %s\n",strerror(errno));
		exit(1);
	}
	setnonblocking(client_fd);
	hash = intHash(client_fd) & MAXCONN;
	if((p_so = malloc(sizeof(SOCKOPT))) == NULL)
	{
		fprintf(stderr,"Malloc Error\n");
		exit(1);
	}
	p_so->fd = client_fd;
	p_so->do_task = send_reply;
	hlist_add_head(&p_so->hlist, &fd_hash[hash]);
	num++;
	ev.data.fd = client_fd;
	ev.events = EPOLLIN;
	ret = epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev);
	if(ret)
		bail("Epoll_ctl()");
	return 0;
}

int send_reply(struct sock_opt *p_so)
{
	char reqBuf[BUFSIZE];
	int z;

	if(( z = read(p_so->fd, reqBuf, sizeof(reqBuf))) <= 0)
	{
		close(p_so->fd);
		hlist_del(&p_so->hlist);
		free(p_so);
		if(z < 0 && errno != ECONNRESET)
			bail("Read()");
	}
	else
	{
		reqBuf[z] = 0;
		printf("The message form client is below:\n");
		printf("%s\n",reqBuf);
		z = write(p_so->fd, reqBuf, strlen(reqBuf));
		if(z < 0)
			bail("Write()");
	}
	return 0;
}
