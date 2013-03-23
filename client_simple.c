#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <string.h>

#define BUFSIZE 512

int main(int argc, char *argv[])
{
	int client;
	struct sockaddr_in server_addr;
	int portnumber;
	int z;
	char reqBuf[BUFSIZE];
	struct hostent *host;
	if(argc != 3)
	{
		fprintf(stderr,"Usage : %s hostname portnumber\n",argv[0]);
		exit(1);
	}
	if((host = gethostbyname(argv[1])) == NULL)
	{
		fprintf(stderr,"Usage : %s hostname portnumber\n",argv[0]);
		exit(1);
	}
	if((portnumber = atoi(argv[2])) < 0)
	{
		fprintf(stderr,"Usage : %s hostname portnumber\n",argv[0]);
		exit(1);
	}
	if((client = socket(PF_INET, SOCK_STREAM, 0)) == -1)
	{
		fprintf(stderr,"Socket error: %s\n",strerror(errno));
		exit(1);
	}
	
	memset(&server_addr, 0, sizeof server_addr);
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(portnumber);
	server_addr.sin_addr = *((struct in_addr*)(host->h_addr));

	if((connect(client,(struct sockaddr*)(&server_addr), sizeof server_addr)) == -1)
	{
		fprintf(stderr,"Connect error: %s\n",strerror(errno));
		exit(1);
	}
	for(;;)
	{
		if(!fgets(reqBuf,sizeof reqBuf,stdin))
		{
			printf("\n");
			break;
		}
		z = strlen(reqBuf);
		reqBuf[z] = 0;
		write(client,reqBuf,sizeof reqBuf);
	}
}
