#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define BUFSIZE 512

static void bail(const char *on_what)
{
	fputs(strerror(errno),stderr);
	fputs(":",stderr);
	fputs(on_what, stderr);
	fputc('\n',stderr);
	exit(1);
}

int main(int argc, char *argv[])
{
	int client;
	struct sockaddr_in server_addr;
	int portnumber;
	struct hostent *host;
	int z;
	char reqBuf[BUFSIZE];
	char stdBuf[BUFSIZE];
	fd_set orfds,rfds;
	int ret, maxfd = -1;
	if(argc != 3)
	{
		fprintf(stderr,"Usage :%s hostname portnumber.\n",argv[0]);
		exit(1);
	}
	if((host = gethostbyname(argv[1])) == NULL)
	{
		fprintf(stderr,"Gethostbyname error.\n",argv[0]);
		exit(1);
	}
	if((portnumber = atoi(argv[2])) < 0)
	{
		fprintf(stderr,"Usage : %s hostname portnumber.\n",argv[0]);
		exit(1);
	}
	if((client = socket(PF_INET,SOCK_STREAM,0)) == -1)
	{
		fprintf(stderr,"Socket error: %s\n",strerror(errno));
		exit(1);
	}

	memset(&server_addr, 0, sizeof server_addr);
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(portnumber);
	server_addr.sin_addr = *((struct in_addr*)(host->h_addr));

	if((connect(client, (struct sockaddr*)(&server_addr), sizeof server_addr)) == -1)
	{
		fprintf(stderr,"Connect error: %s\n",strerror(errno));
		exit(1);
	}

	FD_ZERO(&orfds);
	FD_SET(STDIN_FILENO, &orfds);
	maxfd = STDIN_FILENO;
	FD_SET(client,&orfds);
	if(client > maxfd)
		maxfd = client;
	printf("Connected to server...\n");
	for(;;)
	{
		rfds = orfds;
		printf("\nEntering some message to send to the server.\n");
		fflush(stdout);
		ret = select(maxfd + 1, &rfds, NULL, NULL, NULL);
		if(ret == -1)
		{
			fprintf(stderr,"Select error : %s\n",strerror(errno));
			break;
		}
		else
		{
			if(FD_ISSET(client,&rfds))
			{
				if((z = read(client, reqBuf, BUFSIZE)) <0)
				{
					bail("Read()");
				}
				printf("The message from server is :\n");
				printf("%s\n",reqBuf);
				if(z == 0)
				{
					printf("\nserver has closed the socket.\n");
					printf("Press any key to exit\n");
					getchar();
					break;
				}
			}
			else if(FD_ISSET(STDIN_FILENO, &rfds))
			{
				if(!fgets(stdBuf, BUFSIZE, stdin))
				{
					printf("\n");
					break;
				}	
				z = strlen(stdBuf);
				if(z > 0 && stdBuf[--z] == '\n')
					stdBuf[z] = 0;
				if(z == 0)
					continue;
				z = write(client, stdBuf, strlen(stdBuf));
				if(z < 0)
					bail("Write()");
							
			}
		}
	}
	close(client);
	return 0;
}
