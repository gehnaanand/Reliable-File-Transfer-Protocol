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
#include <sys/stat.h>

#define BUFSIZE 1024
#define MAX_PATH_LEN 512
#define WINDOW_SIZE 5
#define TIMEOUT 1 // Timeout in seconds

typedef struct
{
  int sequence_number;
  char data[BUFSIZE];
  struct timeval sent_time;
} Packet;

/* 
 * error - wrapper for perror
 */
void error(char *msg) {
  perror(msg);
  exit(0);
}

long timeval_diff(struct timeval *start_time, struct timeval *end_time) {
  return (end_time->tv_sec - start_time->tv_sec) * 1000 + (end_time->tv_usec - start_time->tv_usec) / 1000;
}

//PUT command
void go_back_n_receive_file(int server_socket, const char *filename, struct sockaddr_in *client_addr, int clientlen) {
  const char *dir_name = "server_folder";

  // Check if the directory exists
  struct stat st;
  if (stat(dir_name, &st) == -1) {
    // Directory does not exist, create it
    if (mkdir(dir_name, 0777) == -1) {
      perror("mkdir");
      return; // Error creating directory
    }
    printf("Directory created successfully: %s\n", dir_name);
  } else {
    printf("Directory already exists: %s\n", dir_name);
  }

  char path[MAX_PATH_LEN];
  const char *folder = "server_folder/";

  snprintf(path, MAX_PATH_LEN, "%s%s", folder, filename);

  FILE *file = fopen(path, "wb");
  if (!file) {
    perror("File open failed");
    return;
  }

  Packet window[WINDOW_SIZE];
  for (int i = 0; i < WINDOW_SIZE; i++) {
    window[i].sequence_number = -1;
    memset(window[i].data, '\0', sizeof(window[i].data)); 
  }

  int base = 0;
  int eof = 0;
  int error = 0;

  while (1) {
    Packet packet;
    memset(packet.data, '\0', sizeof(packet.data)); 
    ssize_t packet_len;

    //Wait for sometime (timeout) to check if client asks for acknowledgement again
    if (eof == 1 || error == 1) {
      fd_set readfds;
      FD_ZERO(&readfds);
      FD_SET(server_socket, &readfds);

      struct timeval timeout;
      timeout.tv_sec = TIMEOUT;
      timeout.tv_usec = 0;

      int ready = select(server_socket + 1, &readfds, NULL, NULL, &timeout);
      printf("Waiting to receive\n");
      if (ready > 0 && FD_ISSET(server_socket, &readfds)) {
        packet_len = recvfrom(server_socket, &packet, sizeof(Packet), 0, (struct sockaddr *)&client_addr, &clientlen);
      } else {
        printf("Will close\n");
        break;
      }
    } else {
      packet_len = recvfrom(server_socket, &packet, sizeof(Packet), 0, (struct sockaddr *)&client_addr, &clientlen);
    }

    if (packet_len < 0) {
      perror("Recvfrom failed");
      exit(0);
    }

    printf("Receiving sequence number %d \n", packet.sequence_number);
   
    if (packet.sequence_number == -10) {
      printf("Received error\n");
      error = 1;
    }

    if (strcmp(packet.data, "EOF") == 0) {
      printf("EOF\n");
      eof = 1;
    }

    if (packet.sequence_number >= base && packet.sequence_number < base + WINDOW_SIZE) {
      // If within the window, store the packet
      window[packet.sequence_number % WINDOW_SIZE] = packet;
      
      // Check if the packet is the next in sequence to be written to the file
      while (window[base % WINDOW_SIZE].sequence_number == base) {
        if (strcmp(window[base % WINDOW_SIZE].data, "EOF") != 0)
          fwrite(window[base % WINDOW_SIZE].data, 1, packet_len - sizeof(int) - sizeof(packet.sent_time), file);
        // Send acknowledgment for the received packet
        printf("Sending ack for sequence number - %d\n", window[base % WINDOW_SIZE].sequence_number);
        sendto(server_socket, &window[base % WINDOW_SIZE].sequence_number, sizeof(int), 0, (const struct sockaddr *)&client_addr, clientlen);
        base++;
      }
    } else if (packet.sequence_number < base) {
      printf("Sending ack for already received sequence number - %d\n", packet.sequence_number);
      sendto(server_socket, &packet.sequence_number, sizeof(int), 0, (const struct sockaddr *)&client_addr, clientlen);
    }
  }
  
  if (error == 1) {
    printf("Deleting file due to file error\n");
    remove(path);
  } else {
    printf("Closing\n");
    fclose(file);
  }
}

//GET command
void go_back_n_send_file(int server_socket, const char *filename, struct sockaddr_in client_addr, int clientlen) {
  char path[MAX_PATH_LEN];
  const char *folder = "server_folder/";

  snprintf(path, MAX_PATH_LEN, "%s%s", folder, filename);
  FILE *file = fopen(filename, "rb");
  int file_error = 0;
  if (file == NULL) {
    file_error = 1;
    perror("File not found \n");
    // exit(0);
  }
  
  Packet window[WINDOW_SIZE];
  int base = 0, nextseqnum = 0;
  int eof = 0;
  int error_sent = 0;

  while (1) {
    // Send packets within the window
    while (nextseqnum < base + WINDOW_SIZE && eof == 0 && error_sent == 0) {
      Packet packet;
      if (file_error == 1) {
        packet.sequence_number = -10;
        memcpy(packet.data, "File not found", strlen("File not found"));
        gettimeofday(&packet.sent_time, NULL);
        sendto(server_socket, &packet, strlen("File not found") + sizeof(int) + sizeof(packet.sent_time), 0, (const struct sockaddr *)&client_addr, clientlen);
        nextseqnum++;
        window[0] = packet;
        error_sent = 1;
        break;
      }

      packet.sequence_number = nextseqnum;

      memset(packet.data, '\0', BUFSIZE); 
      ssize_t bytes_read = fread(packet.data, 1, BUFSIZE, file);

      // End of file
      if (bytes_read <= 0) {
        printf("EOF\n");
        memset(packet.data, '\0', BUFSIZE); 
        memcpy(packet.data, "EOF", 3); 
        // Set transmission time for the packet
        gettimeofday(&packet.sent_time, NULL);
        printf("Sending sequence number - %d\n", packet.sequence_number);
        // printf("Sending content - %s\n", packet.data);
        sendto(server_socket, &packet, bytes_read + sizeof(int) + sizeof(packet.sent_time), 0, (const struct sockaddr *)&client_addr, clientlen);
        nextseqnum++;
        window[packet.sequence_number % WINDOW_SIZE] = packet;
        eof = 1;
        break; 
      }

      // Set transmission time for the packet
      gettimeofday(&packet.sent_time, NULL);
      printf("Sending sequence number - %d\n", packet.sequence_number);
      // printf("Sending content - %s\n", packet.data);
      sendto(server_socket, &packet, bytes_read + sizeof(int) + sizeof(packet.sent_time), 0, (const struct sockaddr *)&client_addr, clientlen);
      nextseqnum++;

      // Store the packet in the window for retransmission if needed
      window[packet.sequence_number % WINDOW_SIZE] = packet;
    }

    // Check for timeout on each packet in the window
    struct timeval current_time;
    gettimeofday(&current_time, NULL);

    // printf("Checking timeout for sequence number - %d\n", window[base % WINDOW_SIZE].sequence_number);
    long elapsed_time = timeval_diff(&window[base % WINDOW_SIZE].sent_time, &current_time);
    // printf("Sent time - %ld, Current time - %ld\n", window[base % WINDOW_SIZE].sent_time.tv_sec, current_time.tv_sec);
    // printf("Checking timeout for sequence number - %d ==> %ld\n", window[base % WINDOW_SIZE].sequence_number, elapsed_time);
    if (elapsed_time >= TIMEOUT * 1000) {
      for (int i = base; i < nextseqnum; i++) {
        Packet *packet = &window[i % WINDOW_SIZE];
        
        // Timeout occurred, retransmit the packet
        printf("Timeout occurred for sequence number %d, retransmitting...\n", packet->sequence_number);
        sendto(server_socket, packet, sizeof(packet->data) + sizeof(int) + sizeof(packet->sent_time), 0, (const struct sockaddr *)&client_addr, clientlen);
        gettimeofday(&packet->sent_time, NULL); // Update transmission time
      }
    }

    // Receive acknowledgment
    int ack_number;
    ssize_t ack_len = recvfrom(server_socket, &ack_number, sizeof(int), MSG_DONTWAIT, (struct sockaddr *)&client_addr, &clientlen);
    if (ack_len > 0) {
      printf("Received acknowledgment for sequence number = %d\n", ack_number);

      if (ack_number == -10)
        break;

      // Move base forward
      while (base <= ack_number) {
        base++;
      }
    }

    // Check for completion
    if (base >= nextseqnum) {
      break; // All packets have been acknowledged
    }
  }

  if (file_error == 1)
    return;

  fclose(file);
}

void ls(int server_socket, struct sockaddr_in client_addr, int clientlen) {
  DIR *dir;
  struct dirent *entry;

  dir = opendir("server_folder");
  if (dir == NULL) {
    Packet packet;
    packet.sequence_number = -10;
    memcpy(packet.data, "Error: Unable to read directory", strlen("Error: Unable to read directory"));
    gettimeofday(&packet.sent_time, NULL);
    sendto(server_socket, &packet, 31, 0, (struct sockaddr *)&client_addr, clientlen);
    return;
  }

  Packet window[WINDOW_SIZE];
  int base = 0, nextseqnum = 0;
  int eof = 0;

  while (1) {
    // Send packets within the window
    while (nextseqnum < base + WINDOW_SIZE && eof == 0) {
      Packet packet;
      packet.sequence_number = nextseqnum;

      memset(packet.data, '\0', BUFSIZE); 
      entry = readdir(dir);

      // End of file
      if (entry == NULL) {
        printf("EOF\n");
        memset(packet.data, '\0', BUFSIZE); 
        memcpy(packet.data, "EOF", 3); 
        // Set transmission time for the packet
        gettimeofday(&packet.sent_time, NULL);
        printf("Sending sequence number - %d\n", packet.sequence_number);
        // printf("Sending content - %s\n", packet.data);
        sendto(server_socket, &packet, strlen(packet.data) + sizeof(int) + sizeof(packet.sent_time), 0, (const struct sockaddr *)&client_addr, clientlen);
        nextseqnum++;
        window[packet.sequence_number % WINDOW_SIZE] = packet;
        eof = 1;
        break; 
      }

      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;  // Skip current directory and parent directory entries

      strncpy(&packet.data, entry->d_name, BUFSIZE - 1); 
      strcat(&packet.data, "\n"); 

      // Set transmission time for the packet
      gettimeofday(&packet.sent_time, NULL);
      printf("Sending sequence number - %d\n", packet.sequence_number);
      printf("Sending content - %s\n", packet.data);
      sendto(server_socket, &packet, strlen(packet.data) + sizeof(int) + sizeof(packet.sent_time), 0, (const struct sockaddr *)&client_addr, clientlen);
      nextseqnum++;

      // Store the packet in the window for retransmission if needed
      window[packet.sequence_number % WINDOW_SIZE] = packet;
    }

    // Check for timeout on each packet in the window
    struct timeval current_time;
    gettimeofday(&current_time, NULL);

    long elapsed_time = timeval_diff(&window[base % WINDOW_SIZE].sent_time, &current_time);
    if (elapsed_time >= TIMEOUT * 1000) {
      for (int i = base; i < nextseqnum; i++) {
        Packet *packet = &window[i % WINDOW_SIZE];
        
        // Timeout occurred, retransmit the packet
        printf("Timeout occurred for sequence number %d, retransmitting...\n", packet->sequence_number);
        sendto(server_socket, packet, strlen(packet->data) + sizeof(int) + sizeof(packet->sent_time), 0, (const struct sockaddr *)&client_addr, clientlen);
        gettimeofday(&packet->sent_time, NULL); // Update transmission time
      }
    }

    // Receive acknowledgment
    int ack_number;
    ssize_t ack_len = recvfrom(server_socket, &ack_number, sizeof(int), MSG_DONTWAIT, (struct sockaddr *)&client_addr, &clientlen);
    if (ack_len > 0) {
      printf("Received acknowledgment for sequence number = %d\n", ack_number);

      // Move base forward
      while (base <= ack_number) {
        base++;
      }
    }

    // Check for completion
    if (base >= nextseqnum) {
      break; // All packets have been acknowledged
    }
  }

  closedir(dir);
}

//DELETE command
void delete(int sockfd, struct sockaddr_in client_addr, int clientlen, char *filename) {
  char path[MAX_PATH_LEN];
  const char *folder = "server_folder/";
  snprintf(path, MAX_PATH_LEN, "%s%s", folder, filename);
  char buffer[BUFSIZE];
  memset(buffer, '\0', BUFSIZE); 
  if (remove(path) == 0) {
    memcpy(buffer, "File deleted successfully", strlen("File deleted successfully")); 
    printf("File deleted successfully\n");
  } else {
    memcpy(buffer, "Error: File not found", strlen("Error: File not found")); 
    printf("Deletion error\n");
    // sendto(sockfd, "Error: File not found", 21, 0, (struct sockaddr *)client_addr, clientlen);
  }

  sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr, clientlen);
  
  int ack = 0;

  while (1) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    struct timeval timeout;
    timeout.tv_sec = TIMEOUT;
    timeout.tv_usec = 0;

    int n;
    int ready = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
    printf("Waiting to receive ack\n");
    if (ready > 0 && FD_ISSET(sockfd, &readfds)) {
      n = recvfrom(sockfd, &ack, sizeof(int), 0, (struct sockaddr *)&client_addr, &clientlen);
      if (n < 0)
        error("Error in recvfrom\n");
      
      if (ack == 1) {
        printf("Received ack\n");
        break;
      }
    } else {
      //Retransmit the status
      printf("Retransmitting status\n");
      sendto(sockfd, buffer, strlen(buffer), 0, (struct sockaddr *)&client_addr, clientlen);
    } 
  }
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

    printf("Server received %d/%d bytes: %s\n", strlen(buf), n, buf);
    int ack = 1;
    int n1;
    n1 = sendto(sockfd, &ack, sizeof(int), 0, (const struct sockaddr *)&clientaddr, clientlen);
    if (n1 < 0)
      error("ERROR in sendto\n");

    /*
    * Get the command 
    */
    char command[50];
    char filename[50];
    
    sscanf(buf, "%s %s", command, filename);
    
    if (strncmp(command, "get", 3) == 0) {
      printf("Getting file - %s\n", filename);
      go_back_n_send_file(sockfd, filename, clientaddr, clientlen);
    } else if (strncmp(command, "put", 3) == 0) {
      printf("Uploading file - %s\n", filename);
      go_back_n_receive_file(sockfd, filename, (struct sockaddr *)&clientaddr, clientlen);
    } else if (strncmp(command, "delete", 6) == 0) {
      printf("Deleting file - %s\n", filename);
      delete(sockfd, clientaddr, clientlen, filename);
    } else if (strncmp(command, "ls", 2) == 0) {
      printf("Listing all files\n");
      ls(sockfd, clientaddr, clientlen);
    } else if (strncmp(command, "exit", 4) == 0) {
      printf("Client is exiting\n");
      break;
    }
  }

  close(sockfd);
  return 0;
}