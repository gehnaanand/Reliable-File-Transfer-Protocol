/* 
 * udpserver.c - A simple UDP echo server 
 * usage: udpserver <port>
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>

#define BUFSIZE 1024
#define MAX_PATH_LEN 512
#define WINDOW_SIZE 5
#define TIMEOUT 2 // Timeout in seconds

//PUT command
void receive_file(int server_socket, const char *filename, struct sockaddr_in *client_addr, int clientlen) {
  char buffer[BUFSIZE];
  ssize_t bytes_received;
  char path[MAX_PATH_LEN];
  const char *folder = "server_folder1/";

  snprintf(path, MAX_PATH_LEN, "%s%s", folder, filename);

  FILE *file = fopen(path, "w");
  if (!file) {
    perror("File open failed");
    return;
  }

  int expected_sequence_number = 0;
  while (1) {
    int sequence_number;
    bytes_received = recvfrom(server_socket, &sequence_number, sizeof(int), 0, (struct sockaddr *)&client_addr, &clientlen);
    if (bytes_received <= 0) {
      break;
    }
    printf("Received sequence number - %d\n", sequence_number);

    if (sequence_number != expected_sequence_number) {
      // Packet loss detected, request retransmission
      printf("Packet loss detected, request retransmission %d - %d\n", sequence_number, expected_sequence_number);
      sendto(server_socket, &expected_sequence_number, sizeof(int), 0,
              (struct sockaddr *)&client_addr, clientlen);
      continue;
    }

    memset(buffer, '\0', sizeof(buffer)); 
    bytes_received = recvfrom(server_socket, buffer, BUFSIZE, 0, (struct sockaddr *)&client_addr, &clientlen);
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
    sendto(server_socket, &sequence_number, sizeof(int), 0, (struct sockaddr *)&client_addr, clientlen);
  }
  printf("Done writing the file \n");
  // fflush(file);
  fclose(file);
}

//GET command
void send_file(int server_socket, const char *filename, struct sockaddr_in *client_addr, int clientlen) {
  char buffer[BUFSIZE];
  ssize_t bytes_read;
  char path[MAX_PATH_LEN];
  const char *folder = "server_folder1/";

  snprintf(path, MAX_PATH_LEN, "%s%s", folder, filename);

  int sequence_number = 0;
  FILE *file = fopen(path, "rb");
  if (!file) {
    snprintf(buffer, BUFSIZE, "Error: File not found");
    sendto(server_socket, &sequence_number, sizeof(int), 0, (struct sockaddr *)client_addr, clientlen);
    sendto(server_socket, buffer, strlen(buffer), 0, (struct sockaddr *)client_addr, clientlen);
    perror("File open failed");
    return;
  }

  struct timeval start, end;
  while ((bytes_read = fread(buffer, 1, BUFSIZE, file)) > 0) {
    // Send packet with sequence number
    printf("Send sequence number - %d \n", sequence_number);
    printf("Send content - %s \n", buffer);
    sendto(server_socket, &sequence_number, sizeof(int), 0, (struct sockaddr *)client_addr, clientlen);
    sendto(server_socket, buffer, bytes_read, 0, (struct sockaddr *)client_addr, clientlen);

    // Start timer
    gettimeofday(&start, NULL);

    // Wait for acknowledgment or timeout
    int ack_received = 0;
    while (!ack_received) {
      gettimeofday(&end, NULL);
      double elapsed_time = (end.tv_sec - start.tv_sec) + ((end.tv_usec - start.tv_usec) / 1000000.0);
      if (elapsed_time >= TIMEOUT) {
        printf("Timeout occurred for packet %d. Retransmitting...\n", sequence_number);
        sendto(server_socket, &sequence_number, sizeof(int), 0, (struct sockaddr *)client_addr, clientlen);
        sendto(server_socket, buffer, bytes_read, 0, (struct sockaddr *)client_addr, clientlen);
        gettimeofday(&start, NULL); // Restart timer
      }

      // Check for acknowledgment
      fd_set readfds;
      FD_ZERO(&readfds);
      FD_SET(server_socket, &readfds);

      struct timeval timeout;
      timeout.tv_sec = TIMEOUT - elapsed_time;
      timeout.tv_usec = 0;

      int ready = select(server_socket + 1, &readfds, NULL, NULL, &timeout);
      if (ready > 0 && FD_ISSET(server_socket, &readfds)) {
        int ack;
        if (recvfrom(server_socket, &ack, sizeof(int), 0, NULL, NULL) > 0 && ack == sequence_number) {
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
  sendto(server_socket, &sequence_number, sizeof(int), 0, (struct sockaddr *)client_addr, clientlen);
  sendto(server_socket, "EOF", 3, 0, (struct sockaddr *)client_addr, clientlen);
  fclose(file);
}
/*
 * error - wrapper for perror
 */
void error(char *msg) {
  perror(msg);
  exit(1);
}

void get(int sockfd, struct sockaddr_in *client_addr, int clientlen, char *filename) {
  FILE *file;
  char buffer[BUFSIZE];
  ssize_t bytes_read;
  char path[MAX_PATH_LEN];
  const char *folder = "server_folder/";
  
  snprintf(path, MAX_PATH_LEN, "%s%s", folder, filename);

  file = fopen(path, "r");
  
  if (file == NULL) {
    perror("File not found");
    snprintf(buffer, BUFSIZE, "Error: File not found");
    sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)client_addr, clientlen);
    return;
  }
  
  while ((bytes_read = fread(buffer, 1, BUFSIZE, file)) > 0) {
    sendto(sockfd, buffer, bytes_read, 0, (struct sockaddr *)client_addr, clientlen);
  }
  
  sendto(sockfd, "EOF", 3, 0, (struct sockaddr *)client_addr, clientlen);
  fclose(file);
  printf("Sent successfully to client\n");
}

void put(int sockfd, struct sockaddr_in *client_addr, int clientlen, char *filename) {
  FILE *file;
  char buffer[BUFSIZE];
  ssize_t bytes_written;
  char path[MAX_PATH_LEN];
  const char *folder = "server_folder/";

  snprintf(path, MAX_PATH_LEN, "%s%s", folder, filename);
  file = fopen(path, "wb");
  if (file == NULL) {
    snprintf(buffer, BUFSIZE, "Error: Could not create file");
    sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)client_addr, clientlen);
    return;
  }

  while (1) {
    printf("Writing \n");
    memset(buffer, '\0', sizeof(buffer)); 
    ssize_t bytes_received = recvfrom(sockfd, buffer, BUFSIZE, 0, NULL, NULL);
    printf("Buffer - %s\n", buffer);
    // if (bytes_received <= 0) {
    //   break;
    // }
    
    // if (bytes_written < bytes_received) {
    //   break;
    // }
    if (strcmp(buffer, "EOF") == 0)
      break;
    
    bytes_written = fwrite(buffer, 1, bytes_received, file);
  }
  printf("Got file from client\n");
  fclose(file);
}

void delete(int sockfd, struct sockaddr_in *client_addr, int clientlen, char *filename) {
  char path[MAX_PATH_LEN];
  const char *folder = "server_folder1/";
  snprintf(path, MAX_PATH_LEN, "%s%s", folder, filename);
  if (remove(path) == 0) {
    printf("Deleted successfully\n");
    sendto(sockfd, "File deleted successfully", 25, 0, (struct sockaddr *)client_addr, clientlen);
  } else {
    printf("Deletion error\n");
    sendto(sockfd, "Error: File not found", 21, 0, (struct sockaddr *)client_addr, clientlen);
  }
}

void ls(int sockfd, struct sockaddr_in *client_addr, int clientlen) {
  DIR *dir;
  struct dirent *entry;
  char buffer[BUFSIZE] = "";

  dir = opendir("server_folder1");
  if (dir == NULL) {
    sendto(sockfd, "Error: Unable to read directory", 31, 0, (struct sockaddr *) client_addr, clientlen);
    return;
  }

  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue; // Skip current directory and parent directory entries
      
    memset(buffer, 0, BUFSIZE); // Reset buffer before each use
    strncpy(buffer, entry->d_name, BUFSIZE - 1); 
    strcat(buffer, "\n"); 
    sendto(sockfd, buffer, strlen(buffer), 0, client_addr, clientlen);
    printf("Sending file - %s", buffer);
  }

  closedir(dir);
  sendto(sockfd, "EOF", strlen("EOF"), 0, (struct sockaddr *) client_addr, clientlen);
  
}

int main(int argc, char **argv) {
  int sockfd; /* socket */
  int portno; /* port to listen on */
  int clientlen; /* byte size of client's address */
  struct sockaddr_in serveraddr; /* server's addr */
  struct sockaddr_in clientaddr; /* client addr */
  struct hostent *hostp; /* client host info */
  char buf[BUFSIZE]; /* message buf */
  char *hostaddrp; /* dotted decimal host addr string */
  int optval; /* flag value for setsockopt */
  int n; /* message byte size */

  /* 
   * check command line arguments 
   */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  portno = atoi(argv[1]);

  /* 
   * socket: create the parent socket 
   */
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) {
    error("ERROR opening socket \n");
    exit(1);
  }

  /* setsockopt: Handy debugging trick that lets 
   * us rerun the server immediately after we kill it; 
   * otherwise we have to wait about 20 secs. 
   * Eliminates "ERROR on binding: Address already in use" error. 
   */
  optval = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
	     (const void *)&optval , sizeof(int));

  /*
   * build the server's Internet address
   */
  bzero((char *) &serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serveraddr.sin_port = htons((unsigned short)portno);

  /* 
   * bind: associate the parent socket with a port 
   */
  if (bind(sockfd, (struct sockaddr *) &serveraddr, 
	   sizeof(serveraddr)) < 0) {
    error("ERROR on binding \n");
    exit(1);
  }

  /* 
   * main loop: wait for a datagram, then echo it
   */
  clientlen = sizeof(clientaddr);
  while (1) {

    /*
     * recvfrom: receive a UDP datagram from a client
     */
    bzero(buf, BUFSIZE);
    n = recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr *) &clientaddr, &clientlen);
    if (n < 0) {
      error("ERROR in recvfrom \n");
      exit(1);
    }

    /* 
     * gethostbyaddr: determine who sent the datagram
     */
    // hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr, 
		// 	  sizeof(clientaddr.sin_addr.s_addr), AF_INET);
    // if (hostp == NULL) {
    //   error("ERROR on gethostbyaddr\n");
    //   exit(1);
    // }
    // hostaddrp = inet_ntoa(clientaddr.sin_addr);
    // if (hostaddrp == NULL) {
    //   error("ERROR on inet_ntoa\n");
    //   exit(1);
    // }
    // printf("Server received datagram from %s (%s)\n", 
	  // hostp->h_name, hostaddrp);
    printf("Server received %d/%d bytes: %s\n", strlen(buf), n, buf);

    /*
    * Get the command 
    */
    // char *command = strtok(buf, " ");
    // char *filename = strtok(NULL, " ");
    // printf("Command: %s \n", command);
    // printf("File name: %s \n", filename);
    char command[50];
    char filename[50];
    
    sscanf(buf, "%s %s", command, filename);
    
    if (strncmp(command, "get", 3) == 0) {
      printf("Getting file - %s\n", filename);
      // get(sockfd, (struct sockaddr *)&clientaddr, clientlen, filename);
      send_file(sockfd, filename, (struct sockaddr *)&clientaddr, clientlen);
    } else if (strncmp(command, "put", 3) == 0) {
      printf("Uploading file - %s\n", filename);
      // put(sockfd, (struct sockaddr *)&clientaddr, clientlen, filename);
      receive_file(sockfd, filename, (struct sockaddr *)&clientaddr, clientlen);
    } else if (strncmp(command, "delete", 6) == 0) {
      printf("Deleting file - %s\n", filename);
      delete(sockfd, (struct sockaddr *)&clientaddr, clientlen, filename);
    } else if (strncmp(command, "ls", 2) == 0) {
      printf("Listing all files\n");
      ls(sockfd, (struct sockaddr *)&clientaddr, clientlen);
    } else if (strncmp(command, "exit", 4) == 0) {
      printf("Client is exiting\n");
      break;
    }
    /* 
     * sendto: echo the input back to the client 
     */
    // n = sendto(sockfd, buf, strlen(buf), 0, 
	  //      (struct sockaddr *) &clientaddr, clientlen);
    // if (n < 0) 
    //   error("ERROR in sendto");
  }

  close(sockfd);
  return 0;
}