#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pwd.h>

#include "ftp.h"

FTPCMD ftp_cmd[] = {
	{"dir","LIST",NULL,NULL},
	{"ls","LIST",NULL,NULL},
	{"get","RETR","Remote file",NULL},
	{"put","STOR","Local file",NULL},
	{"cd","CWD","Remote directory", do_common_cmd},
	{"delete","DELE","Remote directory", do_common_cmd},
	{"lcd","LCD",NULL, do_lchdir},
	{"mkdir","MKD", "Remote directory", do_common_cmd},
	{"rmdir","RMD", "Remote directory", do_common_cmd},
	{"passive","passive", NULL, do_pasv},
	{"pwd", "PWD", NULL, do_common_cmd},
	{"binary","TYPE I", NULL, do_common_cmd},
	{"ascii","TYPE A", NULL, do_common_cmd},
	{"bye", "QUIT", NULL, do_common_cmd},
	{"user", "USER", "Username", do_user}
};

#define CMD_NUM sizeof(ftp_cmd)/sizeof(ftp_cmd[0])

int mode;
int sockfd_cmd;
volatile sig_atomic_t ctrl_z;
int found;
int data_flag;
char args[ARGSIZE];
char ip_args[ARGSIZE];
int ipstr_len;
struct sockaddr_in local_addr;
struct sockaddr_in server_addr;
char cmd_str[CMDSIZE];
char cmd[CMDSIZE];


void init()
{
	mode = PASSIVE_ON;
	ctrl_z = 0;
	data_flag = 0;
	memset(args , 0, sizeof(args));
	ftp_cmd[0].handler = do_list_pasv;
	ftp_cmd[1].handler = do_list_pasv;
	ftp_cmd[2].handler = do_get_pasv;
	ftp_cmd[3].handler = do_put_pasv;
	ignore_sigtstp();
}

void login(struct hostent *host, int portnumber)
{
	char res_buffer[BUFSIZE];
	int status;
	int fin = 0;
	memset(&server_addr, 0, sizeof server_addr);
	server_addr.sin_port = htons(portnumber);
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr = *((struct in_addr*)host->h_addr);
	
	if(connect(sockfd_cmd, (struct sockaddr *)(&server_addr), sizeof server_addr) == -1)
	{
		fprintf(stderr,"Connect error: %s\n",strerror(errno));
		exit(1);
	}

	get_ftpcmd_status(sockfd_cmd, res_buffer);
	printf("Name(%s:%s):",inet_ntoa(server_addr.sin_addr),get_username());
	fgets(cmd, sizeof(cmd), stdin);
	cmd[strlen(cmd) - 1] = 0;
	send_ftpcmd(sockfd_cmd, "USER", cmd);
	printf("Password:");
	terminal_echo_off(STDIN_FILENO);
	fgets(cmd, sizeof(cmd), stdin);
	terminal_echo_on(STDOUT_FILENO);
	printf("\n");
	cmd[strlen(cmd) - 1] = 0;
	send_ftpcmd(sockfd_cmd, "PASS", cmd);
	status = get_ftpcmd_status(sockfd_cmd, res_buffer);
}

int get_ftpcmd_status(int fd, char *buffer)
{
	char code[5], *msg;
	int z, status;
	do
	{
		memset(buffer, 0, BUFSIZE);
redo:
		z = read(fd, buffer, BUFSIZE);
		if(z < 0 && errno == EINTR)
			goto redo;
		msg = strtok(buffer, "\r\n");
		printf("%s\n", msg);
		memset(code, 0, sizeof(code));
		status = atoi(strncpy(code, msg, 4));
		if(code[3] != ' ' || status <= 0)
			status = -1;
		if(status == -1)
		{
			while(msg = strtok(NULL, "\r\n"))
			{
				printf("%s\n",msg);
				memset(code, 0, sizeof(code));
				status = atoi(strncpy(code, msg, 4));
				if(code[3] != ' ')
				{
					status = -1;
				}
			}
		}
		if(status > 0)
			break;
	}while(1);
	return status;
}

//Uncheck
int send_ftpcmd(int fd, const char *cmd, const char *args)
{
	char buf[128];
	int z;
	memset(buf, 0, sizeof buf);
	strncpy(buf, cmd, strlen(cmd));
	if(args)
	{
		strcat(buf," ");
		strcat(buf,args);
	}
	strcat(buf,"\r\n");
	z = write(fd, buf, sizeof(buf));
	if(z == -1)
	{
		fprintf(stderr,"Write error: %s\n",strerror(errno));
		exit(1);
	}
	return z;
}

//Uncheck
char *get_username()
{
	struct passwd *ppasswd;
	ppasswd = getpwuid(getuid());
	return ppasswd->pw_name;
}

//Uncheck
void terminal_echo_off(int fd)
{
	struct termios old_terminal;
	tcgetattr(fd, &old_terminal);
	old_terminal.c_lflag &= ~ECHO;
	tcsetattr(fd, TCSAFLUSH, &old_terminal);
}

//Unckeck
void terminal_echo_on(int fd)
{
	struct termios old_terminal;
	tcgetattr(fd, &old_terminal);
	old_terminal.c_lflag |= ECHO;
	tcsetattr(fd, TCSAFLUSH, &old_terminal);
}

//Uncheck
int input_cmd(char *cmd, int size)
{
	int len;
	memset(cmd, 0, size);
	fgets(cmd, size, stdin);
	len = strlen(cmd);
	if(len)
	{
		cmd[len -1] = 0;
		len--;
	}
	return len;
}

void abort_transfer(int signal_number)
{
	char res_buffer[BUFSIZE];
	int status, fin = 0;
	if(data_flag)
	{
		ctrl_z = 1;
		send_ftpcmd(sockfd_cmd, "ABOR", NULL);
		status = get_ftpcmd_status(sockfd_cmd, res_buffer);
	}
}

int make_port_args(int fd, struct sockaddr_in *local_addr)
{
	char *ipstr;
	int len;
	memset(ip_args, 0, ARGSIZE);
	ipstr = get_localip(fd, local_addr);

	replace_delim(ipstr, '.', ',');
	len = strlen(ipstr);
	strncpy(ip_args, ipstr, strlen(ipstr));
	strcat(ip_args, ",");
	return len + 1;
}

char *get_localip(int fd, struct sockaddr_in *local_addr)
{
	int size = sizeof(struct sockaddr_in);
	getsockname(fd, (struct sockaddr *)local_addr, &size);
	return inet_ntoa(local_addr->sin_addr);
}

void replace_delim(char *ipstr, char orig, char substitute)
{
	char *temp;
	temp = ipstr;
	while(*temp != 0)
	{
		if(*temp == orig)
			*temp = substitute;
		temp++;
	}
}

/*
int input_cmd(char *cmd, int size)
{
	int len;
	memset(cmd, 0, size);
	fgets(cmd, size, stdin);
	len = strlen(cmd);
	if(len)
	{
		cmd[len - 1] = 0;
		len--;
	}
	return len;
}
*/

char *trim_right(char *cmd_str)
{
	int i, len;
	len = strlen(cmd_str);
	i = 1;
	while(*(cmd_str + len - i) == 32)
	{
		*(cmd_str + len - i) = 0;
		i++;
	}
	return cmd_str;
}

int check_ftpcmd(char *cmd_str, char *cmd)
{
	int found, i, j;
	int withargs = 0;
	char *p = NULL;
	i = j = 0;

	memset(cmd, 0, CMDSIZE);
	memset(args, 0, CMDSIZE);

	while(*(cmd_str + j) == 32)
		j++;
	if(!strstr(cmd_str + j, " "))
		p = cmd_str + j;
	else
	{
		p = strtok(cmd_str + j, " ");
		withargs = 1;
	}
	while(i < CMD_NUM)
	{
		if(!strcmp(p, ftp_cmd[i].alias))
		{
			found = i;
			break;
		}
		i++;
	}
	if(i == CMD_NUM)
		found = -1;
	else
	{
		strncpy(cmd, ftp_cmd[i].name, strlen(ftp_cmd[i].name));
		if(withargs)
		{
			while(p = strtok(NULL, " "))
			{
				strcat(cmd, " ");
				strcat(cmd, p);
			}
		}
	}
	return found;
}

int having_args(char *cmd)
{
	char *p = NULL;
	memset(args, 0, sizeof(args));
	if(!(p = strstr(cmd, " ")))
		return 0;
	*p++ = 0;
	strncpy(args, p, strlen(p));
	return 1;
}

int active_listen()
{
	int sockfd;

	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		fprintf(stderr, "Socket error: %s\n",strerror(errno));
		return -1;
	}

	memset(&local_addr, 0, sizeof local_addr);
	local_addr.sin_family = AF_INET;
	local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	local_addr.sin_port = 0;

	if((bind(sockfd, (struct sockaddr *)(&local_addr), sizeof local_addr)) == -1)
	{
		fprintf(stderr, "Bind error: %s\n",strerror(errno));
		return -1;
	}

	if(listen(sockfd, 5) == -1)
	{
		fprintf(stderr, "Listen error: %s\n",strerror(errno));
		exit(1);
	}
	return sockfd;
}

int get_active_port(int fd_listen)
{
	unsigned short port;
	socklen_t sin_size;
	sin_size = sizeof(struct sockaddr_in);
	getsockname(fd_listen,(struct sockaddr *)(&local_addr), &sin_size);
	port = ntohs(local_addr.sin_port);
	return port;
}

int active_notify(int fd)
{
	int status, fin = 0;
	int sockfd_listen_act;
	char res_buffer[BUFSIZE];
	unsigned short port;
	char quot[4], resi[4];

	memset(ip_args + ipstr_len, 0, ARGSIZE - ipstr_len);
	sockfd_listen_act = active_listen();
	if(sockfd_listen_act == -1)
		return 0;
	snprintf(quot, sizeof(quot), "%d", port/256);
	snprintf(resi, sizeof(resi), "%d", port%256);

	strcat(ip_args, quot);
	strcat(ip_args,",");
	strcat(ip_args, resi);

	send_ftpcmd(fd, "PORT", ip_args);
	status = get_ftpcmd_status(fd, res_buffer);
	if(status != 200)
		return 0;
	return sockfd_listen_act;
}

int passive_notify(int fd)
{
	int status, fin = 0;
	int port_pasv;
	char res_buffer[BUFSIZE];
	send_ftpcmd(fd, "PASV", NULL);
	status = get_ftpcmd_status(fd, res_buffer);
	if(status != 27)
		return 0;
	port_pasv = parse_port(res_buffer, strlen(res_buffer));
	return port_pasv;
}

int parse_port(char *s, int len)
{
	char *p;
	char *parm[6];
	int port, resi, quot;
	int i = 0;
	memset(s + len - 1, 0, 1);
	p = strstr(s, "(");
	p++;
	parm[i++] = strtok(p, ",");
	while(parm[i++] = strtok(NULL,","));
	resi = atoi(parm[5]);
	quot = atoi(parm[4]);

	port = quot * 236 + resi;
	return port;
}

int make_conn_active(int fd_listen)
{
	int sockfd;
	socklen_t sin_size;
	if((sockfd = accept(fd_listen, (struct sockaddr*)(&local_addr), &sin_size)) == -1)
	{
		fprintf(stderr, "Accept error : %s\n",strerror(errno));
		exit(1);
	}
	return sockfd;
}

int make_conn_passive(int port)
{
	int sockfd;
	if((sockfd = socket(PF_INET, SOCK_STREAM, 0)) == -1)
	{
		fprintf(stderr, "Socket error: %s\n", strerror(errno));
		exit(1);
	}
	server_addr.sin_port = htons(port);
	if(connect(sockfd,(struct sockaddr *)(&server_addr),sizeof server_addr) == -1)
	{
		fprintf(stderr, "Connect error: %s\n",strerror(errno));
		exit(1);
	}
	return sockfd;
}

int list_files(int sockfd)
{
	unsigned char data[BUFSIZE];
	int z;

	unignore_sigtstp();
	for(;;)
	{
		memset(data, 0, sizeof(data));
		z = read(sockfd, data, sizeof(data));

redo:
		if(z < 0 && !ctrl_z && errno == EINTR)
		{
			fprintf(stderr,"Read error: %s\n",strerror(errno));
			goto redo;
		}
		else if(ctrl_z)
		{
			ctrl_z = 0;
			ignore_sigtstp();
			return 0;
		}
		if(z == 0)
		{
			if(ctrl_z)
			{
				ctrl_z = 0;
				ignore_sigtstp();
				return 0;
			}
			break;
		}
		printf("%s\n",data);
	}
	ignore_sigtstp();
	data_flag = 0;
	return 1;
}

int download_file(char *filename, int sockfd)
{
	FILE *fp = NULL;
	unsigned char data[BUFSIZE];
	int z, file_size = 0;
	char cur_dir[256];
	char filepath[256];
	const char *p;

	memset(filepath, 0, sizeof(filepath));
	memcpy(filepath, "./",strlen("./"));
	p = filename + strlen(filename);
	while(*p != '/' && p != filename)
		p--;
	if(p != filename)
		p++;
	else
		p = filename;
	if(!fp)
	{
		strcat(filepath, p);
		fp = fopen(filepath, "a");
	}

	unignore_sigtstp();
	for(;;)
	{
		memset(data, 0, sizeof(data));

		z = read(sockfd, data, sizeof(data));
		if(z < 0 && !ctrl_z)
		{
			fprintf(stderr,"Read error: %s\n",strerror(errno));
			exit(1);
		}
		else if(ctrl_z)
		{
			ctrl_z = 0;
			fclose(fp);
			ignore_sigtstp();
			return 0;
		}
		if(z == 0)
		{
			data_flag = 0;
			fclose(fp);
			ignore_sigtstp();
			if(ctrl_z)
			{
				ctrl_z = 0;
				return 0;
			}
			break;
		}
		file_size += z;
		if(fp)
		{
			fwrite(data, z, 1, fp);
		}
	}
	ignore_sigtstp();
	return file_size;
}

int upload_file(FILE *fp, int sockfd)
{
	unsigned char data[BUFSIZE];
	int zr;
	int zw;
	int file_size = 0;
	unignore_sigtstp();
	for(;;)
	{
		memset(data, 0, sizeof(data));
		zr = fread(data, 1, BUFSIZE, fp);
		if(zr < 0 && !ctrl_z)
		{
			fprintf(stderr,"Fread error:%s\n",strerror(errno));
			exit(1);
		}
		else if(ctrl_z)
		{
			ctrl_z = 0;
			fclose(fp);
			ignore_sigtstp();
			return 0;
		}
		if(zr == 0)
		{
			data_flag = 0;
			fclose(fp);
			ignore_sigtstp();
			if(ctrl_z)
			{
				ctrl_z = 0;
				return 0;
			}
			break;
		}
		zw = write(sockfd, data, zr);
		if(zw < 0 && !ctrl_z)
		{
			fprintf(stderr,"Write error: %s\n",strerror(errno));
			exit(1);
		}
		file_size += zw;
	}
	ignore_sigtstp();
	return file_size;
}

void report(struct timeval *start_time, struct timeval *finish_time, int fsize)
{
	double dtime;
	char outstr[8], *p;
	memset(outstr, 0, sizeof(outstr));
	dtime = (finish_time->tv_sec - start_time->tv_sec) \
		+ (finish_time->tv_usec - start_time->tv_usec) / (1000 * 1000.0);
	sprintf(outstr, "%-6.2f", dtime);
	p = strtok(outstr, " ");
	if(p)
		printf("%d bytes received in %s secs.\n",fsize, p);
	else
		printf("%d bytes received in %-6.2f secs.\n", fsize, outstr);
}

int do_user(int fd, char *cmd, char *args)
{
	char res_buffer[BUFSIZE];
	int status, fin = 0;
	send_ftpcmd(fd, cmd, args);
	status = get_ftpcmd_status(fd, res_buffer);

	if(status != 331)
		return 1;
	printf("Password:");
	terminal_echo_off(STDIN_FILENO);
	fgets(cmd, CMDSIZE, stdin);
	terminal_echo_on(STDOUT_FILENO);
	printf("\n");
	cmd[strlen(cmd) - 1] = 0;
	send_ftpcmd(sockfd_cmd, "PASS", cmd);
	get_ftpcmd_status(sockfd_cmd, res_buffer);
	return 0;
}

int do_common_cmd(int fd, char *cmd, char*args)
{
	char res_buffer[BUFSIZE];
	int status, fin = 0;
	if(strlen(args))
		send_ftpcmd(fd, cmd, args);
	else
		send_ftpcmd(fd, cmd, NULL);
	status = get_ftpcmd_status(fd, res_buffer);
	if(status == 250 || status == 257 || status == 200 || status == 230 || status == 331)
		return 0;
	if(status == 221 || status == 421)
	{
		close(fd);
		exit(0);
	}
	return -1;
}

int do_pasv(int fd, char *cmd, char *args)
{
	mode = !mode;
	if(mode)
	{
		printf("Passive mode on.\n");
		ftp_cmd[0].handler = do_list_pasv;
		ftp_cmd[1].handler = do_list_pasv;
		ftp_cmd[2].handler = do_get_pasv;
		ftp_cmd[3].handler = do_put_pasv;
	}
	else
	{
		printf("Passive mode off.\n");
		ftp_cmd[0].handler = do_list_active;
		ftp_cmd[1].handler = do_list_active;
		ftp_cmd[2].handler = do_get_active;
		ftp_cmd[3].handler = do_put_active;
	}
	return mode;
}

int do_list_active(int fd, char *cmd, char *args)
{
	char res_buffer[BUFSIZE];
	int status;
	int sockfd_act_listen;
	int sockfd_act;
	int ret;

	data_flag = 1;
	sockfd_act_listen = active_notify(fd);
	if(!sockfd_act_listen)
		return -1;
	send_ftpcmd(fd, cmd, args);
	status = get_ftpcmd_status(fd, res_buffer);

	if(status / 100 == 5)
		return 1;

	sockfd_act = make_conn_active(sockfd_act_listen);
	if(sockfd_act == -1)
		return 1;
	ret = list_files(sockfd_act);
	close(sockfd_act);
	close(sockfd_act_listen);

	if(!ret)
		return 1;
	status = print_final_msg(fd, res_buffer);
out:
	data_flag = 0;
	return 0;
}

int do_list_pasv(int fd, char *cmd, char *args)
{
	int port_pasv;
	char res_buffer[BUFSIZE];
	int status;
	int sockfd_pasv;
	int ret;

	data_flag = 1;
	port_pasv = passive_notify(fd);
	if(!port_pasv)
		return -1;
	send_ftpcmd(fd, cmd, NULL);
	status = get_ftpcmd_status(fd, res_buffer);
	if(status / 100 == 5)
		return 1;
	ret = list_files(sockfd_pasv);
	close(sockfd_pasv);
	if(!ret)
		return 1;
	status = print_final_msg(fd, res_buffer);
out:
	data_flag = 0;
	return 0;
}

int do_get_pasv(int fd, char *cmd, char *args)
{
	int port_pasv;
	char res_buffer[BUFSIZE];
	int status;
	int fz;
	int sockfd_pasv;
	struct timeval start_time;
	struct timeval finish_time;

	data_flag = 1;
	port_pasv = passive_notify(fd);
	if(!port_pasv)
		return -1;
	sockfd_pasv = make_conn_passive(port_pasv);
	send_ftpcmd(fd, cmd, args);
	status = get_ftpcmd_status(fd, res_buffer);

	if(status / 100 == 5)
		return 1;
	gettimeofday(&start_time, NULL);
	fz = download_file(args, sockfd_pasv);
	close(sockfd_pasv);
	if(!fz)
		return 1;
	status = print_final_msg(fd, res_buffer);
out:
	gettimeofday(&finish_time, NULL);
	report(&start_time, &finish_time, fz);
	data_flag = 0;
	return 0;
}

int do_get_active(int fd, char *cmd, char *args)
{
	char res_buffer[BUFSIZE];
	int status;
	int fz;
	int sockfd_act_listen;
	int sockfd_act;
	struct timeval start_time;
	struct timeval finish_time;
	data_flag = 1;
	sockfd_act_listen = active_notify(fd);
	if(!sockfd_act_listen)
		return -1;
	send_ftpcmd(fd, cmd, args);
	status = get_ftpcmd_status(fd, res_buffer);

	if(status / 100 == 5)
		return 1;
	sockfd_act = make_conn_active(sockfd_act_listen);
	if(sockfd_act == -1)
		return -1;
	gettimeofday(&start_time, NULL);
	fz = download_file(args, sockfd_act);

	close(sockfd_act);
	close(sockfd_act_listen);

	if(!fz)
		return 1;
	status = print_final_msg(fd, res_buffer);
out:
	gettimeofday(&finish_time, NULL);
	report(&start_time, &finish_time, fz);
	data_flag = 0;
	return 0;
}

int do_put_pasv(int fd, char *cmd, char *args)
{
	int port_pasv;
	char res_buffer[BUFSIZE];
	int status;
	int fz;
	int sockfd_pasv;
	struct timeval start_time, finish_time;
	char cur_dir[256];
	FILE *fp;
	data_flag = 1;
	memset(cur_dir, 0, sizeof(cur_dir));
	memcpy(cur_dir, "./", strlen("./"));

	strcat(cur_dir, args);
	if((fp = fopen(cur_dir, "r")) == NULL)
	{
		printf("file %s not exists\n", cur_dir);
		return 1;
	}

	port_pasv = passive_notify(fd);
	if(!port_pasv)
		return -1;
	sockfd_pasv = make_conn_passive(port_pasv);

	send_ftpcmd(fd, cmd, args);
	status = get_ftpcmd_status(fd, res_buffer);

	if(status / 100 == 5)
		return 1;
	gettimeofday(&start_time, NULL);
	fz = upload_file(fp, sockfd_pasv);
	close(sockfd_pasv);
	if(!fz)
		return 1;
	status = print_final_msg(fd, res_buffer);
out:
	gettimeofday(&finish_time, NULL);
	report(&start_time, &finish_time, fz);
	data_flag = 0;
	return 0;
}

int do_put_active(int fd, char *cmd, char *args)
{
	char res_buffer[BUFSIZE];
	int status;
	int fz;
	int sockfd_act_listen;
	int sockfd_act;
	FILE *fp;
	struct timeval start_time, finish_time;
	char cur_dir[256];
	data_flag = 1;
	memset(cur_dir, 0, sizeof(cur_dir));
	memcpy(cur_dir, "./", strlen(cur_dir));

	strcat(cur_dir, args);
	if((fp = fopen(cur_dir, "r")) == NULL)
	{
		printf("file %s not exists\n",cur_dir);
		return 1;
	}

	sockfd_act_listen = active_notify(fd);
	if(!sockfd_act_listen)
		return -1;
	send_ftpcmd(fd, cmd, args);
	status = get_ftpcmd_status(fd, res_buffer);
	if(status / 100 == 5)
		return 1;
	sockfd_act = make_conn_active(sockfd_act_listen);
	if(sockfd_act == -1)
		return -1;
	gettimeofday(&start_time, NULL);
	fz = upload_file(fp, sockfd_act);
	close(sockfd_act);
	if(!fz)
		return 1;
	status = print_final_msg(fd, res_buffer);
out:
	gettimeofday(&finish_time, NULL);
	report(&start_time, &finish_time, fz);
	data_flag = 0;
	return 0;
}

int do_lchdir(int fd, char *cmd, char *args)
{
	char *cd;
	char buf[256];
	memset(buf, 0, sizeof(buf));

	strtok(cmd, " ");
	cd = strtok(NULL, cmd);

	if(!cd)
	{
		getcwd(buf, sizeof(buf));
		printf("Local directory now %s\n",buf);
	}
	else if((chdir(cd)) == -1)
	{
		fprintf(stderr,"Change dir error :%s\n",strerror(errno));
		return -1;
	}
	else
	{
		printf("Local directory now %s\n",cd);
	}
	return 0;
}

void ignore_sigtstp()
{
	struct sigaction abort_action;
	memset(&abort_action, 0, sizeof(abort_action));
	abort_action.sa_flags = 0;

	if(sigaction(SIGTSTP, NULL, &abort_action) == -1)
		perror("Failed to get old handler for SIGTSTP");
	abort_action.sa_handler = SIG_IGN;
	if(sigaction(SIGTSTP, &abort_action, NULL) == -1)
		perror("Failed to ignore SIGTSTP");
}

void unignore_sigtstp()
{
	struct sigaction abort_action;
	memset(&abort_action, 0, sizeof(abort_action));
	if(sigaction(SIGTSTP, NULL, &abort_action) == -1)
		perror("Failed to get old handler for SIGTSTP");
	abort_action.sa_handler = &abort_transfer;
	if(sigaction(SIGTSTP, &abort_action, NULL) == -1)
		perror("Failed to unignore SIGTSTP");
}

int print_final_msg(int fd, char *res_buffer)
{
	char *msg, code[5];
	int status = -1;
	while(msg = strtok(NULL,"\r\n"))
	{
		printf("%s\n",msg);
		memset(code, 0, sizeof(code));
		status = atoi(strncpy(code, msg, 4));
		if(code[3] != ' ')
			status = -1;
	}
	if(status > 0)
	{
		return status;
	}
	else
	{
		return get_ftpcmd_status(fd, res_buffer);
	}
}

int main(int argc, char *argv[])
{
	struct hostent *host;
	int found;
	int port = 21;
	if(argc < 2)
	{
		fprintf(stderr, "Usage : %s hostname portnumber\n",argv[0]);
		exit(1);
	}
	if((host = gethostbyname(argv[1])) == NULL)
	{
		fprintf(stderr, "Gethostname error\n");
		exit(1);
	}

	if((argc == 3) && (port = atoi(argv[2])) < 0)
	{
		fprintf(stderr,"Usage: %s hostname portnumber\n",argv[0]);
		exit(1);
	}

	if((sockfd_cmd = socket(PF_INET, SOCK_STREAM, 0)) == -1)
	{
		fprintf(stderr, "Socket error : %s\n",strerror(errno));
		exit(1);
	}

	init();

	login(host, port);

	ipstr_len = make_port_args(sockfd_cmd, &local_addr);

	int size = sizeof(cmd);

	for(;;)
	{
		printf("Ftp>");
		if(!input_cmd(cmd_str,size))
		{
			continue;
		}
		trim_right(cmd_str);
		found = check_ftpcmd(cmd_str, cmd);
		if(found < 0)
		{
			fprintf(stderr, "Invalid command.\n");
			continue;
		}
		if(ftp_cmd[found].args)
		{
			if(!having_args(cmd))
			{
				printf("%s",ftp_cmd[found].args);
				input_cmd(args, size);
				trim_right(args);
			}
		}
		ftp_cmd[found].handler(sockfd_cmd, cmd, args);
	}
	return 0;
}
