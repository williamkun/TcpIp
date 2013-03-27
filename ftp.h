#define BUFSIZE 512
#define CMDSIZE 64
#define ARGSIZE 64
#define PASSIVE_ON 0x1

struct ftpcmd
{
	char *alias;
	char *name;
	char *args;
	int (*handler)(int fd, char *cmd, char *args);
};

typedef struct ftpcmd FTPCMD;
static void bail(const char *);
int send_ftpcmd(int, const char *, const char *);
int active_listen();
char *get_localip(int, struct sockaddr_in *);
int make_port_args(int, struct sockaddr_in *);
int get_active_port(int);
int list_files(int);
int input_cmd(char *, int);
int check_ftpcmd(char *, char *);
int having_args(char *);
char *trim_right(char *);
int download_file(char *, int);
int upload_file(FILE *, int);
void report(struct timeval *, struct timeval *, int);
int get_ftpcmd_status(int, char *);
int make_conn_active(int);
int make_conn_passive(int);
int passive_notify(int);
int active_notiry(int);
void replace_delim(char *, char, char);
int parse_port(char *, int);
void init();
char *get_username();
void terminal_echo_off(int);
void terminal_echo_on(int);
void ignore_sigtstp();
void unignore_sigtstp();
int do_common_cmd(int, char *, char *);
int do_user(int, char *, char *);
int do_pasv(int, char *, char *);
int do_list_pasv(int, char *, char *);
int do_list_active(int, char *, char *);
int do_get_pasv(int, char *, char *);
int do_get_active(int, char *, char *);
int do_put_pasv(int, char *, char *);
int do_put_active(int, char *, char*);
int do_lchdir(int, char *, char *);
void abort_transfer(int);
void login(struct hostent *, int);
int print_final_msg(int, char *);
