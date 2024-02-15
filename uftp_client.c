/* 
 * udpclient.c - A simple UDP client
 * usage: udpclient <host> <port>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 

#define BUFSIZE 1024
#define MAX_PATH_LEN 512
#define WINDOW_SIZE 5
#define TIMEOUT 2 // Timeout in seconds

//PUT command
void send_file(int client_socket, const char *filename, struct sockaddr_in server_addr, int serverlen) {
  char buffer[BUFSIZE];
  ssize_t bytes_read;
  FILE *file = fopen(filename, "rb");

  if (!file) {
    perror("File open failed");
    return;
  }

  int sequence_number = 0;
  struct timeval start, end;
  while ((bytes_read = fread(buffer, 1, BUFSIZE, file)) > 0) {
    // Send packet with sequence number
    printf("Send sequence number - %d \n", sequence_number);
    // printf("Send content - %s \n", buffer);
    sendto(client_socket, &sequence_number, sizeof(int), 0, (struct sockaddr *)&server_addr, serverlen);
    sendto(client_socket, buffer, bytes_read, 0, (struct sockaddr *)&server_addr, serverlen);

    // Start timer
    gettimeofday(&start, NULL);

    // Wait for acknowledgment or timeout
    int ack_received = 0;
    while (!ack_received) {
      gettimeofday(&end, NULL);
      double elapsed_time = (end.tv_sec - start.tv_sec) + ((end.tv_usec - start.tv_usec) / 1000000.0);
      if (elapsed_time >= TIMEOUT) {
        printf("Timeout occurred for packet %d. Retransmitting...\n", sequence_number);
        sendto(client_socket, &sequence_number, sizeof(int), 0, (struct sockaddr *)&server_addr, serverlen);
        sendto(client_socket, buffer, bytes_read, 0, (struct sockaddr *)&server_addr, serverlen);
        gettimeofday(&start, NULL); // Restart timer
      }

      // Check for acknowledgment
      fd_set readfds;
      FD_ZERO(&readfds);
      FD_SET(client_socket, &readfds);

      struct timeval timeout;
      timeout.tv_sec = TIMEOUT - elapsed_time;
      timeout.tv_usec = 0;

      int ready = select(client_socket + 1, &readfds, NULL, NULL, &timeout);
      if (ready > 0 && FD_ISSET(client_socket, &readfds)) {
        int ack;
        if (recvfrom(client_socket, &ack, sizeof(int), 0, NULL, NULL) > 0 && ack == sequence_number) {
          printf("Ack received - %d \n", sequence_number);
          ack_received = 1;
        }
      }
    }

    // Move to the next sequence number
    sequence_number = (sequence_number + 1) % WINDOW_SIZE;
    printf("Advance to next sequence number %d \n", sequence_number);
  }
  printf("EOF\n");
  sendto(client_socket, &sequence_number, sizeof(int), 0, (struct sockaddr *)&server_addr, serverlen);
  sendto(client_socket, "EOF", 3, 0, &server_addr, sizeof(server_addr));
  fclose(file);
}

//GET command
void receive_file(int client_socket, const char *filename, struct sockaddr_in server_addr, int serverlen) {
  char buffer[BUFSIZE];
  ssize_t bytes_received;
  char path[MAX_PATH_LEN];
  const char *folder = "client_folder1/";

  snprintf(path, MAX_PATH_LEN, "%s%s", folder, filename);
  FILE *file = fopen(path, "wb");
  printf("Receive_file\n");
  if (file == NULL) {
    error("File open failed");
    return; 
  }

  // perror("File error\n");
  printf("File opened\n");
  int expected_sequence_number = 0;
  while (1) {
    printf("Hello\n");
    int sequence_number;
    printf("Hello1\n");
    bytes_received = recvfrom(client_socket, &sequence_number, sizeof(int), 0, NULL, NULL);
    printf("Bytes received - %d\n", bytes_received);
    if (bytes_received <= 0) {
      break;
    }
    printf("Received sequence number - %d\n", sequence_number);

    if (sequence_number != expected_sequence_number) {
      // Packet loss detected, request retransmission
      printf("Packet loss detected, request retransmission %d - %d\n", sequence_number, expected_sequence_number);
      sendto(client_socket, &expected_sequence_number, sizeof(int), 0,
              &server_addr, serverlen);
      continue;
    }

    memset(buffer, '\0', sizeof(buffer)); 
    bytes_received = recvfrom(client_socket, buffer, BUFSIZE, 0, NULL, NULL);
    // printf("Received buffer - %s\n", buffer);
    if (bytes_received <= 0) {
      break;
    }

    if (strcmp(buffer, "EOF") == 0)
      break;

    fwrite(buffer, 1, bytes_received, file);
    expected_sequence_number = (expected_sequence_number + 1) % WINDOW_SIZE;

    // Send acknowledgment
    printf("Send Ack for %d\n", sequence_number);
    printf("Expected sequence number %d\n", expected_sequence_number);
    sendto(client_socket, &sequence_number, sizeof(int), 0, &server_addr, serverlen);
  }
  printf("Done writing the file \n");
  // fflush(file);
  fclose(file);
}

/* 
 * error - wrapper for perror
 */
void error(char *msg) {
  perror(msg);
  exit(0);
}

void get_file(int client_socket, const char *filename) {
  char path[MAX_PATH_LEN];
  const char *folder = "client_folder/";

  snprintf(path, MAX_PATH_LEN, "%s%s", folder, filename);
  FILE *file = fopen(path, "wb");
  if (!file) {
    error("File open failed");
    return; 
  }

  char buffer[BUFSIZE];
  ssize_t bytes_received;
  
  while (1) {
    memset(buffer, '\0', sizeof(buffer)); 
    bytes_received = recvfrom(client_socket, buffer, BUFSIZE, 0, NULL, NULL);
    if (strcmp(buffer, "Error: File not found") == 0) {
      perror("File not found in server");
      return;
    }
    if (bytes_received < 0)
      error("ERROR in recvfrom");

    if (strcmp(buffer, "EOF") == 0)
      break;
    
    fwrite(buffer, 1, bytes_received, file);
  }
  fclose(file);
  printf("Got file %s successfully from server\n", filename);
}

void put_file(int client_socket, const char *filename, struct sockaddr_in serveraddr, int serverlen) {
  char buffer[BUFSIZE];
  ssize_t bytes_read;
  // char path[MAX_PATH_LEN];
  // const char *folder = "/home/gehnaanand/PA1_udp_example/udp/client_folder/";

  // snprintf(path, MAX_PATH_LEN, "%s%s", folder, filename);
  FILE *file = fopen(filename, "r");
  if (file == NULL) {
    error("File open failed");
    return;
  }

  while ((bytes_read = fread(buffer, 1, BUFSIZE, file)) > 0) {
    if (sendto(client_socket, buffer, bytes_read, 0, &serveraddr, serverlen) == -1) {
      error("Send failed");
      fclose(file);
      return;
    }
    memset(buffer, '\0', sizeof(buffer));
  }
  sendto(client_socket, "EOF", 3, 0, &serveraddr, serverlen);
  printf("Sent successfully to server\n");
  fclose(file);
}

void ls(int client_socket) {
  char buffer[BUFSIZE];
  ssize_t bytes_received;

  while(1) {
    memset(buffer, '\0', sizeof(buffer)); 
    bytes_received = recvfrom(client_socket, buffer, BUFSIZE, 0, NULL, NULL);
    if (bytes_received < 0)
      error("ERROR in recvfrom");

    if (strcmp(buffer, "EOF") == 0)
      break;
    
    printf("%s", buffer);
  }
}

int main(int argc, char **argv) {
  int sockfd, portno, n;
  int serverlen;
  struct sockaddr_in serveraddr;
  struct hostent *server;
  char *hostname;
  

  /* check command line arguments */
  if (argc != 3) {
      fprintf(stderr,"usage: %s <hostname> <port>\n", argv[0]);
      exit(0);
  }
  hostname = argv[1];
  portno = atoi(argv[2]);

  /* socket: create the socket */
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) {
    error("ERROR opening socket");
  }

  /* gethostbyname: get the server's DNS entry */
  server = gethostbyname(hostname);
  if (server == NULL) {
    fprintf(stderr,"ERROR, no such host as %s\n", hostname);
    exit(0);
  }

  /* build the server's Internet address */
  bzero((char *) &serveraddr, sizeof(serveraddr)); 
  serveraddr.sin_family = AF_INET;
  bcopy((char *)server->h_addr, (char *)&serveraddr.sin_addr.s_addr, server->h_length);
  serveraddr.sin_port = htons(portno);

  while(1) {
    printf("\nEnter one of the following FTP commands: \n 1: get [filename] \n 2: put [filename] \n 3: delete [filename] \n 4: ls \n 5: exit \n\n");
    char buf[BUFSIZE];
    fgets(buf, BUFSIZE, stdin);
    
    serverlen = sizeof(serveraddr);
    n = sendto(sockfd, buf, strlen(buf), 0, &serveraddr, serverlen);
    if (n < 0) 
      error("ERROR in sendto \n");

    char command[50];
    char filename[50];
    
    sscanf(buf, "%s %s", command, filename);
    
    if (sscanf(buf, "%s %s", command, filename) == 2) {
      if (strncmp(command, "get", 3) == 0) {
        printf("Getting file - %s from server \n", filename);
        // get_file(sockfd, filename);
        receive_file(sockfd, filename, serveraddr, serverlen);
      } else if (strncmp(command, "put", 3) == 0) {
        printf("Putting file - %s into server \n", filename);
        // put_file(sockfd, filename, serveraddr, serverlen);
        send_file(sockfd, filename, serveraddr, serverlen);
      } else if (strncmp(command, "delete", 6) == 0) {
        n = recvfrom(sockfd, buf, strlen(buf), 0, NULL, NULL);
        if (n < 0) 
          error("ERROR in recvfrom \n");
        printf("Delete status: %s \n", buf);
      }
    } else if (sscanf(buf, "%s", command) == 1) {
      if (strncmp(command, "exit", 4) == 0) {
        break;
      } else if (strncmp(command, "ls", 2) == 0) {
        ls(sockfd);
      } 
    } else {
      printf("Unkown command, please enter a valid command\n");
    }
    printf("---------------------------------------------------------------------------");
  }
  close(sockfd);
  return 0;
}
