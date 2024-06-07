#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <stdio.h>
#include <stdlib.h>

#include <err.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <netinet/in.h>

#define BUFSIZE 512
#define CMDSIZE 4
#define PARSIZE 100
#define CLIENTS 5

#define MSG_220 "220 srvFtp version 1.0\r\n"
#define MSG_331 "331 Password required for %s\r\n"
#define MSG_230 "230 User %s logged in\r\n"
#define MSG_530 "530 Login incorrect\r\n"
#define MSG_221 "221 Goodbye\r\n"
#define MSG_550 "550 %s: no such file or directory\r\n"
#define MSG_299 "299 File %s size %ld bytes\r\n"
#define MSG_226 "226 Transfer complete\r\n"

// global variable to store PORT data connection
struct sockaddr_in client_data_addr;


/**
 * function: receive the commands from the client
 * sd: socket descriptor
 * operation: \0 if you want to know the operation received
 *            OP if you want to check an especific operation
 *            ex: recv_cmd(sd, "USER", param)
 * param: parameters for the operation involve
 * return: only usefull if you want to check an operation
 *         ex: for login you need the seq USER PASS
 *             you can check if you receive first USER
 *             and then check if you receive PASS
 **/
bool recv_cmd(int sd, char *operation, char *param) {
  char buffer[BUFSIZE], *token;
  int recv_s;

  // receive the command in the buffer and check for errors
  recv_s = recv(sd, buffer , BUFSIZE, 0);

  if (recv_s < 0) warn("error receiving data");
  if (recv_s == 0) errx(1, "connection closed by host");

  // expunge the terminator characters from the buffer
  buffer[strcspn(buffer, "\r\n")] = 0;

  // complex parsing of the buffer
  // extract command receive in operation if not set \0
  // extract parameters of the operation in param if it needed
  token = strtok(buffer, " ");
  if (token == NULL || strlen(token) < 4) {
    warn("not valid ftp command");
    return false;
  } else {
    if (operation[0] == '\0')
      strcpy(operation, token);
    if (strcmp(operation, token)) {
      warn("abnormal client flow: did not send %s command", operation);
      return false;
    }
    token = strtok(NULL, " ");
    if (token != NULL)
      strcpy(param, token);
  }
  return true;
}

/**
 * function: send answer to the client
 * sd: file descriptor
 * message: formatting string in printf format
 * ...: variable arguments for economics of formats
 * return: true if not problem arise or else
 * notes: the MSG_x have preformated for these use
 **/
bool send_ans(int sd, char *message, ...) {
  char buffer[BUFSIZE];

  va_list args;
  va_start(args, message);

  vsprintf(buffer, message, args);
  va_end(args);
  // send answer preformated and check errors
  if (send(sd, buffer, sizeof(buffer), 0) < 0) {
    printf("ERROR: failed to send message.\n");
    return false;
  } else {
    return true;
  }
}

/**
 * function: RETR operation
 * sd: socket descriptor
 * file_path: name of the RETR file
 **/

void retr(int sd, char *file_path) {
  FILE *file;
  int bread;
  long fsize;
  char buffer[BUFSIZE];

  // check if file exists if not inform error to client
   file = fopen(file_path, "r");
    if (file == NULL) {
        send_ans(sd, MSG_550, file_path);
        return;
    }
  //get file size
    fseek(file, 0, SEEK_END);
    fsize = ftell(file);
    fseek(file, 0, SEEK_SET);


  // send a success message with the file length
  send_ans(sd, MSG_299, file_path, fsize);

  // important delay for avoid problems with buffer size
  sleep(1);

  // new socket for data connection
    int data_sd = socket(AF_INET, SOCK_STREAM, 0);
    if (data_sd < 0) {
        perror("ERROR: failed to create data socket");
        fclose(file);
        return;
    }

  // connect to the client by data port
    if (connect(data_sd, (struct sockaddr*)&client_data_addr, sizeof(client_data_addr)) < 0) {
        perror("ERROR: failed to connect to client data socket");
        close(data_sd);
        fclose(file);
        return;
    }
  // send the file
    while ((bread = fread(buffer, sizeof(char), BUFSIZE, file)) > 0) {
        send(data_sd, buffer, bread, 0);
        sleep(1);
    }
  // close the file
  fclose(file);

  //close data socket
  close(data_sd);

  // send a completed transfer message
  send_ans(sd, MSG_226);
}

/**
 * funcion: check valid credentials in ftpusers file
 * user: login user name
 * pass: user password
 * return: true if found or false if not
 **/
bool check_credentials(char *user, char *pass) {
  FILE *file;
  char *path = "./ftpusers", *line = NULL, credentials[100];
  size_t line_size = 0;
  bool found = false;

  // make the credential string
  sprintf(credentials, "%s:%s", user, pass);

  // check if ftpusers file it's present
  if ((file = fopen(path, "r")) == NULL) {
    warn("Error opening %s", path);
    return false;
  }

  // search for credential string
  while (getline(&line, &line_size, file) != -1) {
    strtok(line, "\n");
    if (strcmp(line, credentials) == 0) {
      found = true;
      break;
    }
  }

  // close file and release any pointers if necessary
  fclose(file);
  if (line)
    free(line);

  // return search status
  return found;
}

/**
 * function: login process management
 * sd: socket descriptor
 * return: true if login is succesfully, false if not
 **/
bool authenticate(int sd) {
  char user[PARSIZE], pass[PARSIZE];

  // wait to receive USER action
  recv_cmd(sd, "USER", user);

  // ask for password
  send_ans(sd, MSG_331 , user);

  // wait to receive PASS action
  recv_cmd(sd, "PASS", pass);

  // if credentials don't check denied login
  if(!check_credentials(user, pass)){
    send_ans(sd, MSG_530);
    printf("Connection closed. \n");
    return false;
  };

  // confirm login
  send_ans(sd, MSG_230,user);
  return true;
}

/**
 *  function: execute all commands (RETR|QUIT)
 *  sd: socket descriptor
 **/

void operate(int sd) {
  char op[CMDSIZE], param[PARSIZE];

  while (true) {
    op[0] = param[0] = '\0';
    // check for commands send by the client if not inform and exit
     if (!recv_cmd(sd, op, param)) {
            printf("Error: failed to receive command\n");
            break;
        }

    if (strcmp(op, "RETR") == 0) {
      retr(sd, param);
    } else if (strcmp(op, "QUIT") == 0) {
      // send goodbye and close connection
      send_ans(sd,MSG_221);
      close(sd);
      break;
    }else if(strcmp(op, "PORT") == 0){
       // Parse and store the client's data port address
            struct sockaddr_in data_addr;
            unsigned int ip[4], p1, p2;
            sscanf(param, "%u,%u,%u,%u,%u,%u", &ip[0], &ip[1], &ip[2], &ip[3], &p1, &p2);
            char ip_str[16];
            sprintf(ip_str, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
            data_addr.sin_family = AF_INET;
            inet_pton(AF_INET, ip_str, &data_addr.sin_addr);
            data_addr.sin_port = htons(p1 * 256 + p2);

            // Store the data address in a global or context variable
            memcpy(&client_data_addr, &data_addr, sizeof(data_addr));
      
    }else {
      // invalid command
      // furute use
    }
  }
}

/**
 * Run with
 *         ./mysrv <SERVER_PORT>
 **/
int main(int argc, char *argv[]) {

  // arguments checking
  if (argc < 2) {
    errx(1, "Port expected as argument");
  } else if (argc > 2) {
    errx(1, "Too many arguments");
  }

  // reserve sockets and variables space
  int master_sd, slave_sd,addr_len;
  int opt = 1;
  struct sockaddr_in master_addr, slave_addr;
  


  // memset(&master_addr, 0, sizeof(master_addr));
  master_addr.sin_family = AF_INET;
  master_addr.sin_port = htons(atoi(argv[1]));
  master_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  // create server socket and check errors
  if ((master_sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("ERROR: failed to create the socket.\n");
    return -1;
  };
    //Function to reuse quicky port and address after a run
    if(setsockopt(master_sd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))){
        perror("setsockop");
        exit(EXIT_FAILURE);
    }
    
  // bind master socket and check errors

  if (bind(master_sd, (struct sockaddr *)&master_addr,
           sizeof(struct sockaddr_in)) < 0) {
    printf("ERROR: failed to associate socket with local address.\n");
    return -1;
  }

  // make it listen
  if (listen(master_sd, CLIENTS) < 0) {
    printf("ERROR: socket failed to listen.\n");
    return -1;
  }
  // main loop
  while (true) {
    // accept connectiones sequentially and check errors
    addr_len = sizeof(struct sockaddr_in);
    if ((slave_sd = accept(master_sd, (struct sockaddr *)&slave_addr, (socklen_t *)&addr_len)) < 0) {
      printf("ERROR: socket failed to accept connection.\n");
      return -1;
    }

    // send hello

     send_ans(slave_sd, MSG_220);

    // operate only if authenticate is true
    if(authenticate(slave_sd)){
      operate(slave_sd);
    };
  }

  // close server socket

  return 0;
}
