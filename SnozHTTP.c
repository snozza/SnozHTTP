#include <string.h>
#include <stdio.h>
#include <netinet/in.h>

#define TRUE 1
#define FALSE 0

int port;
int daemon = FALSE;
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
