#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

#define TRUE 1
#define FALSE 0
#define BUFFER_SIZE 512
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

int getHttpVersion(char *input, char *output)
{
  char *filename = malloc(100);
  int start = scan(input, filename, 4, 100);
  if (start > 0) {
    if (scan(input, output, start, 20)) {
      output[strlen(output) + 1] = '\0';
      if (strcmp("HTTP/1.1", output) == 0) {
        return 1;
      }
      else if (strcmp("HTTP/1.0", output) == 0) {
        return 0;
      }
      else {
        return -1;
      }
    }
  }

  return -1;
}

int getExtension(char *input, char *output, int max)
{
  int in_position = 0;
  int appended_position = 0;
  int i = 0;
  int count = 0;

  for (; i < strlen(input); i++) {
    if (in_position == 1)
    {
      if (count < max) {
        output[appended_position] = input[i];
        appended_position += 1;
        count++;
      }
    }

    if (input[i] == '.') {
      in_position = 1;
    }
  }

  output[appended_position + 1] = '\0';

  if (strlen(output) > 0) {
    return 1;
  }

  return -1;
}

int Content_Length(FILE *fp)
{
  int filesize = 0;

  fseek(fp, 0, SEEK_END);
  filesize = ftell(fp);

  return filesize;
}

int handleHttpGET(char *input)
{
  // IF NOT EXISTS
  // RETURN -1
  // IF EXISTS
  // RETURN 1

  char *filename = (char*)malloc(200 * sizeof(char));
  char *path = (char*)malloc(1000 * sizeof(char));
  char *extension = (char*)malloc(10 * sizeof(char));
  char *mime = (char*)malloc(200 * sizeof(char));
  char *httpVersion = (char*)malloc(20 * sizeof(char));

  int contentLength = 0;
  int mimeSupported = 0;
  int fileNameLength = 0;

  memset(path, '\0', 1000);
  memset(filename, '\0', 200);
  memset(extension, '\0', 10);
  memset(mime, '\0', 200);
  memset(httpVersion, '\0', 20);

  fileNameLength = scan(input, filename, 5, 200);

  if (fileNameLength > 0) {
    if (getHttpVersion(input, httpVersion) != -1) {
      FILE *fp;

      if (getExtension(filename, extension, 10) == -1) {
        printf("File extension does not exist");

        sendString("400 Bad Request\n", connecting_socket);

        free(filename);
        free(mime);
        free(path);
        free(extension);

        return -1;
      }

      mimeSupported = checkMime(extension, mime);

      if (mimeSupported != 1) {
        printf("Mime not supported");

        sendString("400 Bad Request\n", connecting_socket);

        free(filename);
        free(mime);
        free(path);
        free(extension);

        return -1;
      }

      // Open the request file as binary

      strcpy(path, wwwroot);
      strcat(path, filename);

      fp = fopen(path, "rb");

      if (fp == NULL)
      {
        printf("Unable to open file");

        sendString("404 Not Found\n", connecting_socket);

        free(filename);
        free(mime);
        free(extension);
        free(path);

        return -1;
      }

      // Calculate Content Length
      contentLength = Content_Length(fp);
      if (contentLength < 0)
      {
        printf("File size is zero");

        free(filename);
        free(mime);
        free(extension);
        free(path);

        fclose(fp);

        return -1;
      }

      // Send File Content
      sendHeader("200 OK", mime, contentLength, connecting_socket);

      sendFile(fp, contentLength);

      free(filename);
      free(mime);
      free(extension);
      free(path);

      fclose(fp);

      return -1;
    }
    else {
      sendString("501 Not Implemented\n", connecting_socket);
    }
  }

  return -1;
}

int getRequestType(char *input)
{
  // IF REQUEST NOT VALID
  // RETURN -1
  // IF REQUEST VALID
  // RETURN 1 IF GET
  // RETURN 2 IF HEAD
  // RETURN 0 IF NOT YET IMPLEMENTED

  int type = -1;

  if (strlen(input) > 0) {
    type = 1;
  }

  char *requestType = malloc(5);
  scan(input, requestType, 0, 5);

  if (type == 1 && strcmp("GET", requestType) == 0) {
    type = 1;
  }
  else if (type == 1 && strcmp("HEAD", requestType) == 0) {
    type = 2;
  }
  else if (strlen(input) > 4 && strcmp("POST", requestType) == 0) {
    type = 0;
  }
  else {
    type = -1;
  }
  return type;
}

int receive(int socket)
{
  int msgLen = 0;
  char buffer[BUFFER_SIZE];

  memset(buffer, '\0', BUFFER_SIZE);

  if ((msgLen = recv(socket, buffer, BUFFER_SIZE, 0)) == -1) {
    printf("Error handling incoming request");
    return -1;
  }

  int request = getRequestType(buffer);

  if (request == 1) { // GET
    handleHttpGET(buffer);
  }
  else if (request == 2) { // HEAD
    /*sendHeader();*/
  }
  else if (request == 0) { // POST
    sendString("501 Not Implemented\n", connecting_socket);
  }
  else {
    sendString("400 Bad Request\n", connecting_socket);
  }

  return 1;
}

/**
 * Create a socket and assign current_socket to the descriptor
 **/
void createSocket()
{
  // AF_INET for ipv4 address family. SOCK_STREAM for full duplex bygte stream
  current_socket = socket(AF_INET, SOCK_STREAM, 0); 
}
