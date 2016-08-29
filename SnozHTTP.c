#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <unistd.h>

#define TRUE 1
#define FALSE 0
#define EXIT_FAILURE 1

int port;
int deamon = FALSE;
char *wwwroot;
char *conf_file;
char *log_file;
char *mime_file;

FILE *filePointer = NULL;

struct sockaddr_in address;
struct sockaddr_storage connector;
int current_socket;
int connecting_socket;
socklen_t addr_size;

static void daemonize(void) 
{
  pid_t pid, sid;
  
  /* already a daemon */
  if (getppid() == 1) return;

  /* fork off the parent process */
  pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE);
  }
}

