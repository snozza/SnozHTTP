#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#define TRUE 1
#define FALSE 0
#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0

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

  if (pid > 0) {
    exit(EXIT_SUCCESS);
  }

  /* now executing as the child process */

  /* change the file mode mask */
  umask(0);

  /* create a new SID for the child process */
  sid = setsid();
  if (sid < 0) {
    exit(EXIT_FAILURE);
  }

  /* change current working dir to prevent dir from being locked -
     hence not being able to remove it */
  if ((chdir("/")) < 0) {
    exit(EXIT_FAILURE);
  }
}

int sendString(char *message, int socket)
{
  int length = strlen(message);
  int bytes_sent = send(socket, message, length, 0);

  return bytes_sent;
}

int sendBinary(int *byte, int length)
{
  int bytes_sent = send(connecting_socket, byte, length, 0);
  return bytes_sent;
}

void sendHeader(char *Status_code, char *Content_Type, int TotalSize, int socket)
{
  char *head = "\r\nHTTP/1.1 "; /* use \r\n for windows dos etc. */
  char *content_head = "\r\nContent-Type: ";
  char *length_head = "\r\nContent-Length: ";
  char *date_head = "\r\nDate: ";
  char *newline = "\r\n";
  char contentLength[100]; /* TODO as pointer */

  time_t rawtime;

  time(&rawtime);

  sprintf(contentLength, "%i", TotalSize);

  char *message = malloc((
        strlen(head) +
        strlen(content_head) +
        strlen(length_head) +
        strlen(date_head) +
        strlen(newline) +
        strlen(Status_code) +
        strlen(Content_Type) +
        strlen(contentLength) +
        28 +
        sizeof(char)) * 2);

  if (message != NULL) {
    strcpy(message, head);
    strcat(message, Status_code);
    strcat(message, content_head);
    strcat(message, Content_Type);
    strcat(message, length_head);
    strcat(message, contentLength);
    strcat(message, date_head);
    strcat(message, (char*)ctime(&rawtime));
  }
}

void sendHTML(char *statusCode, char *contentType, char *content, int size, int socket)
{
  sendHeader(statusCode, contentType, size, socket);
  sendString(content, socket);
}

void sendFile(FILE *fp, int file_size)
{
  int current_char = 0;

  do {
    current_char = fgetc(fp);
    sendBinary(&current_char, sizeof(char));
  } while(current_char != EOF);
}

int scan(char *input, char *output, int start, int max)
{
  if (start >= strlen(input) )
    return -1;

  int appending_char_count = 0;
  int i = start;
  int count = 0;

  for (; i < strlen(input); i++) {
    if (*(input + i) != '\t' && *(input + i) != ' ' && *(input + i) != '\n' && *(input + i) !='\r') {
      if (count < (max - 1)) {
        *(output + appending_char_count) = *(input + i);
        appending_char_count += 1;
        count++;
      }
    }
    else {
      break;
    }
  }
  *(output + appending_char_count) = '\0';

  // find next word start
  i += 1;

  for (; i < strlen(input); i++) {
    if (*(input + i) != '\t' && *(input + i) != ' ' && *(input + i) != '\n' && *(input + i) != '\r') {
      break;
    }
  }

  return i;
}

int checkMime(char *extension, char *mime_type)
{
  char *current_word = malloc(600);
  char *word_holder = malloc(600);
  char *line = malloc(200);
  int startline = 0;

  FILE *mimeFile = fopen(mime_file, "r");

  free(mime_type);

  mime_type = (char*)malloc(200);

  memset(mime_type, '\0', 200);

  while(fgets(line, 200, mimeFile) != NULL) {
    if (line[0] != '#') {
      startline = scan(line, current_word, 0, 600);
      while(1) {
        if (startline != -1) {
          if (strcmp(word_holder, extension) == 0) {
            memcpy(mime_type, current_word, strlen(current_word));
            free(current_word);
            free(word_holder);
            free(line);
            return 1;
          }
        }
        else {
          break;
        }
      }
    }
    memset(line, '\0', 200);
  }

  free(current_word);
  free(word_holder);
  free(line);

  return 0;
}
