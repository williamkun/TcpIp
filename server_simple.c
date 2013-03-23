#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>

#define BUFSIZE 512

int main(int argc, char *argv[])
{
	int listen_fd,conn_fd;
	struct sockaddr_in server_addr,client_addr;
	int portnumber;
	int z;
	socklen_t sin_size;
	char reqBuf[BUFSIZE];
	if(argc != 2)
	{
		fprintf(stderr,"Usage : %s portnumber\n",argv[0]);
		exit(1);
	}
	if((portnumber = atoi(argv[1])) < 0)
	{
		fprintf(stderr,"Usage : %s portnumber\n",argv[0]);
		exit(1);
	}
	if((listen_fd = socket(PF_INET,SOCK_STREAM,0)) == -1)
	{
		fprintf(stderr,"Socket error: %s\n",strerror(errno));
		exit(1);
	}

	memset(&server_addr, 0, sizeof server_addr);
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(portnumber);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	sin_size = sizeof(struct sockaddr_in);
	if(bind(listen_fd, (struct sockaddr*)(&server_addr), sizeof server_addr) == -1)
	{
		fprintf(stderr,"Bind error : %s\n",strerror(errno));
		exit(1);
	}
	if(listen(listen_fd,128) == -1)
	{
		fprintf(stderr,"Listen error : %s\n",strerror(errno));
		exit(1);
	}
	printf("Server is waiting for connect...\n");
	for(;;)
	{
		if((conn_fd = accept(listen_fd,(struct sockaddr*)(&client_addr),&sin_size)) == -1)
		{
			fprintf(stderr,"Accept error : %s\n",strerror(errno));
			exit(1);
		}
		for(;;)
		{
			z = read(conn_fd, reqBuf, sizeof reqBuf);
			if(z < 0)
			{
				fprintf(stderr,"Read error: %s\n",strerror(errno));
				exit(1);
			}
			if(z == 0)
			{
				close(conn_fd);
				break;
			}
			reqBuf[z] = 0;
			printf("%s",reqBuf);
		}
	}
	return 0;
}
