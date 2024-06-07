#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <err.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>


#define BUFSIZE 512

/**
 * function: receive and analize the answer from the server
 * sd: socket descriptor
 * code: three leter numerical code to check if received
 * text: normally NULL but if a pointer if received as parameter
 *       then a copy of the optional message from the response
 *       is copied
 * return: result of code checking
 **/
bool recv_msg(int sd, int code, char *text) {
    char buffer[BUFSIZE], message[BUFSIZE];
    int recv_s, recv_code;

    // receive the answer
    recv_s = recv(sd, buffer, BUFSIZE, 0);

    // error checking
    if (recv_s < 0) warn("error receiving data");
    if (recv_s == 0) errx(1, "connection closed by host");

    // parsing the code and message receive from the answer
    sscanf(buffer, "%d %[^\r\n]\r\n", &recv_code, message);
    printf("%d %s\n", recv_code, message);
    // optional copy of parameters
    if(text) strcpy(text, message);
    // boolean test for the code
    return (code == recv_code) ? true : false;
}

/**
 * function: send command formated to the server
 * sd: socket descriptor
 * operation: four letters command
 * param: command parameters
 **/
void send_msg(int sd, char *operation, char *param) {
    char buffer[BUFSIZE] = "";

    // command formating
    if (param != NULL)
        sprintf(buffer, "%s %s\r\n", operation, param);
    else
        sprintf(buffer, "%s\r\n", operation);

    // send command and check for errors
     if(send(sd, buffer, sizeof(buffer), 0) < 0) {
        printf("ERROR: failed to send command.\n");
    }
}

/**
 * function: simple input from keyboard
 * return: input without ENTER key
 **/
char * read_input() {
    char *input = malloc(BUFSIZE);
    if (fgets(input, BUFSIZE, stdin)) {
        return strtok(input, "\n");
    }
    return NULL;
}

/**
 * function: login process from the client side
 * sd: socket descriptor
 **/
void authenticate(int sd) {
    char *input, desc[100];
    int code;

    // ask for user
    printf("username: ");
    input = read_input();

    // send the command to the server
    send_msg(sd, "USER", input);
    
    // relese memory
    free(input);

    // wait to receive password requirement and check for errors
    code = 331;
    if(recv_msg(sd, code, desc) != true){
        printf("Error code");
    };

    // ask for password
    printf("passwd: ");
    input = read_input();

    // send the command to the server
    send_msg(sd, "PASS", input);

    // release memory
    free(input);

    // wait for answer and process it and check for errors
    code = 230;
   if(recv_msg(sd, code, desc) != true){
    exit(EXIT_FAILURE);
   } ;
}


int send_port_command(int sd) {
   
    //Create a new socket for data connection
    int data_sd = socket(AF_INET, SOCK_STREAM, 0);
    if (data_sd < 0) {
        perror("ERROR: failed to create data socket");
        exit(EXIT_FAILURE);
    }

    //assign a random port
    struct sockaddr_in data_addr;
    socklen_t addr_len = sizeof(data_addr);
    data_addr.sin_family = AF_INET;
    data_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    data_addr.sin_port = htons(0); 


    if (bind(data_sd, (struct sockaddr*)&data_addr, sizeof(data_addr)) < 0) {
        perror("ERROR: failed to bind data socket");
        close(data_sd);
        exit(EXIT_FAILURE);
    }

    // get assigned port
    if (getsockname(data_sd, (struct sockaddr*)&data_addr, &addr_len) < 0) {
        perror("ERROR: failed to get data socket name");
        close(data_sd);
        exit(EXIT_FAILURE);
    }

    unsigned char* ip = (unsigned char*)&data_addr.sin_addr.s_addr;
    unsigned char* port = (unsigned char*)&data_addr.sin_port;

    char port_param[BUFSIZE];
    sprintf(port_param, "%d,%d,%d,%d,%d,%d",
            ip[0], ip[1], ip[2], ip[3], ntohs(data_addr.sin_port) / 256, ntohs(data_addr.sin_port) % 256);

    // send port command
    send_msg(sd, "PORT", port_param);

    // listenig socket
   if (listen(data_sd, 1) < 0) {
        perror("ERROR: failed to listen on data socket");
        close(data_sd);
        exit(EXIT_FAILURE);
    }
    
   return data_sd;
}



/**
 * function: operation get
 * sd: socket descriptor
 * file_name: file name to get from the server
 **/
void get(int sd, char *file_name) {
    char desc[BUFSIZE], buffer[BUFSIZE];
    int f_size, recv_s, r_size = BUFSIZE;
    FILE *file;

    // send the PORT command to the server
    int data_socket = send_port_command(sd);

    // send the RETR command to the server
    send_msg(sd, "RETR", file_name);

    // check for the response
     if (!recv_msg(sd, 299, desc)) {
        printf("Error: no se pudo iniciar la transferencia del archivo.\n");
        return;
    }
    // parsing the file size from the answer received
    // "File %s size %ld bytes"
    sscanf(buffer, "File %*s size %d bytes", &f_size);

    // open the file to write
    file = fopen(file_name, "w");
     if (file == NULL) {
        perror("ERROR: failed to open file");
        return;
    }
    //accepting connection from the server
    struct sockaddr_in data_addr;
    socklen_t data_addr_len = sizeof(data_addr);
    int data_sd = accept(data_socket, (struct sockaddr*)&data_addr, &data_addr_len);
    if (data_sd < 0) {
        perror("ERROR: failed to accept data connection");
        fclose(file);
        return;
    }
    //receive the file
    while ((recv_s = recv(data_sd, buffer, r_size, 0)) > 0) {
        fwrite(buffer, sizeof(char), recv_s, file);
    }

    // close the file
    fclose(file);

    //close data socket
    close(data_sd);

    // receive the OK from the server
    recv_msg(sd, 226, NULL);
}

/**
 * function: operation quit
 * sd: socket descriptor
 **/
void quit(int sd) {
    // send command QUIT to the client
    send_msg(sd, "QUIT", NULL);
    // receive the answer from the server
    recv_msg(sd, 221, NULL);
}

/**
 * function: make all operations (get|quit)
 * sd: socket descriptor
 **/
void operate(int sd) {
    char *input, *op, *param;

    while (true) {
        printf("Operation: ");
        input = read_input();
        if (input == NULL)
            continue; // avoid empty input
        op = strtok(input, " ");
        // free(input);
        if (strcmp(op, "get") == 0) {
            param = strtok(NULL, " ");
            get(sd, param);
        }
        else if (strcmp(op, "quit") == 0) {
            quit(sd);
            break;
        }
        else {
            // new operations in the future
            printf("TODO: unexpected command\n");
        }
        free(input);
    }
    free(input);
}

// Function to validate IP address
bool ip_validation(const char *ip) {
    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip, &(sa.sin_addr)) != 0;
}

// Function to validate port number
bool port_validation(const char *puerto) {
    int num_puerto = atoi(puerto);
    return num_puerto > 0 && num_puerto <= 65535;
}

/**
 * Run with
 *         ./myftp <SERVER_IP> <SERVER_PORT>
 **/
int main (int argc, char *argv[]) {
    int socket_fd,data_socket;
    const char *server_ip,*server_port;
    struct sockaddr_in addr;

    // arguments checking
     if(argc != 3) {
    	printf("ERROR: enter ip address and port.\n");
    	return -1;
    }

    server_ip = argv[1];
    server_port = argv[2];
 
     if (!ip_validation(server_ip)) {
        printf("The provided IP address is not valid.\n");
        return 1;
    }

    if (!port_validation(server_port)) {
        printf("The provided port number is not valid.\n");
        return 1;
    }
    // create socket and check for errors
    if((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
    	printf("ERROR: failed to create socket.\n");
    	return -1;
    };
    

    // set socket data    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &(addr.sin_addr));
    //memset(&addr, 0, sizeof(addr));

    // connect and check for errors
    if(connect(socket_fd, (struct sockaddr *) &addr, sizeof(struct sockaddr)) < 0) {
    	printf("ERROR: failed to connect.\n");
    	return -1;
    }

    // if receive hello proceed with authenticate and operate if not warning
    
    if(!recv_msg(socket_fd, 220, NULL)){
        warn("ERROR: hello message was not received");
    }else{
       authenticate(socket_fd);
       operate(socket_fd);
    }
    // close socket

    return 0;
}
