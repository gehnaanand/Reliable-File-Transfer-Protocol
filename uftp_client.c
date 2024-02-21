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
#include <signal.h>
#include <sys/stat.h>

#define BUFSIZE 1024
#define MAX_PATH_LEN 512
#define WINDOW_SIZE 5
#define TIMEOUT 1 // Timeout in seconds

// long global_timeout;

typedef struct {
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
void go_back_n_send_file(int client_socket, const char *filename, struct sockaddr_in server_addr, int serverlen) {
  FILE *file = fopen(filename, "rb");
  int file_error = 0;
  if (file == NULL) {
    file_error = 1;
    perror("File not found \n");
    // return;
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
        sendto(client_socket, &packet, strlen("File not found") + sizeof(int) + sizeof(packet.sent_time), 0, (const struct sockaddr *)&server_addr, serverlen);
        nextseqnum++;
        window[0] = packet;
        error_sent = 1;
        break;
      }

      packet.sequence_number = nextseqnum;

      memset(packet.data, '\0', BUFSIZE); 
      ssize_t bytes_read = fread(packet.data, 1, BUFSIZE, file);
      if (bytes_read <= 0) {
        printf("EOF\n");
        memset(packet.data, '\0', BUFSIZE); 
        memcpy(packet.data, "EOF", 3); 
        gettimeofday(&packet.sent_time, NULL);
        printf("Sending sequence number - %d\n", packet.sequence_number);
        sendto(client_socket, &packet, bytes_read + sizeof(int) + sizeof(packet.sent_time), 0, (const struct sockaddr *)&server_addr, serverlen);
        nextseqnum++;
        window[packet.sequence_number % WINDOW_SIZE] = packet;
        eof = 1;
        break; // End of file
      }

      // Set transmission time for the packet
      gettimeofday(&packet.sent_time, NULL);
      printf("Sending sequence number - %d\n", packet.sequence_number);
      // printf("Sending content - %s\n", packet.data);
      sendto(client_socket, &packet, bytes_read + sizeof(int) + sizeof(packet.sent_time), 0, (const struct sockaddr *)&server_addr, serverlen);
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
        sendto(client_socket, packet, sizeof(packet->data) + sizeof(int) + sizeof(packet->sent_time), 0, (const struct sockaddr *)&server_addr, serverlen);
        gettimeofday(&packet->sent_time, NULL); // Update transmission time
      }
    }

    // Receive acknowledgment
    int ack_number;
    ssize_t ack_len = recvfrom(client_socket, &ack_number, sizeof(int), MSG_DONTWAIT, (struct sockaddr *)&server_addr, &serverlen);
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

//GET command
void go_back_n_receive_file(int client_socket, const char *filename, struct sockaddr_in *server_addr, int serverlen) {
  const char *dir_name = "client_folder";

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
  const char *folder = "client_folder/";

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
      FD_SET(client_socket, &readfds);

      struct timeval timeout;
      timeout.tv_sec = TIMEOUT;
      timeout.tv_usec = 0;

      int ready = select(client_socket + 1, &readfds, NULL, NULL, &timeout);
      printf("Waiting to receive\n");
      if (ready > 0 && FD_ISSET(client_socket, &readfds)) {
        packet_len = recvfrom(client_socket, &packet, sizeof(Packet), 0, (struct sockaddr *)&server_addr, &serverlen);
      } else {
        printf("Will close\n");
        break;
      }
    } else {
      packet_len = recvfrom(client_socket, &packet, sizeof(Packet), 0, (struct sockaddr *)&server_addr, &serverlen);
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
        sendto(client_socket, &window[base % WINDOW_SIZE].sequence_number, sizeof(int), 0, (const struct sockaddr *)&server_addr, serverlen);
        base++;
      }
    } else if (packet.sequence_number < base) {
      printf("Sending ack for already received sequence number - %d\n", packet.sequence_number);
      sendto(client_socket, &packet.sequence_number, sizeof(int), 0, (const struct sockaddr *)&server_addr, serverlen);
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

//LS Command
void ls(int client_socket, struct sockaddr_in *server_addr, socklen_t serverlen) {
  Packet window[WINDOW_SIZE];
  for (int i = 0; i < WINDOW_SIZE; i++) {
    window[i].sequence_number = -1;
    memset(window[i].data, '\0', sizeof(window[i].data)); 
  }

  int base = 0;
  int eof = 0;

  while (1) {
    Packet packet;
    memset(packet.data, '\0', sizeof(packet.data)); 
    ssize_t packet_len;

    //Wait for sometime (timeout) to check if client asks for acknowledgement again
    if (eof == 1) {
      fd_set readfds;
      FD_ZERO(&readfds);
      FD_SET(client_socket, &readfds);

      struct timeval timeout;
      timeout.tv_sec = TIMEOUT;
      timeout.tv_usec = 0;

      int ready = select(client_socket + 1, &readfds, NULL, NULL, &timeout);
      // printf("Waiting to receive\n");
      if (ready > 0 && FD_ISSET(client_socket, &readfds)) {
        packet_len = recvfrom(client_socket, &packet, sizeof(Packet), 0, (struct sockaddr *)server_addr, &serverlen); 
      } else {
        // printf("Will close\n");
        break;
      }
    } else {
      packet_len = recvfrom(client_socket, &packet, sizeof(Packet), 0, (struct sockaddr *)server_addr, &serverlen); 
      // printf("Packet data - %s - %d\n", packet.data, packet_len);
    }

    if (packet_len < 0) {
      perror("Recvfrom failed");
      exit(0);
    }

    if (packet.sequence_number == -10) {
      printf("Error from server: %s\n", packet.data);
      break;
    }

    // printf("Receiving sequence number %d \n", packet.sequence_number);
    // printf("Received content - %s\n", packet.data);

    if (strcmp(packet.data, "EOF") == 0) {
      // printf("EOF\n");
      eof = 1;
    }

    if (packet.sequence_number >= base && packet.sequence_number < base + WINDOW_SIZE) {
      // If within the window, store the packet
      window[packet.sequence_number % WINDOW_SIZE] = packet;
      // Check if the packet is the next in sequence to be written to the file
      while (window[base % WINDOW_SIZE].sequence_number == base) {
        if (strcmp(window[base % WINDOW_SIZE].data, "EOF") != 0)
            printf("%s", window[base % WINDOW_SIZE].data);
        // Send acknowledgment for the received packet
        sendto(client_socket, &window[base % WINDOW_SIZE].sequence_number, sizeof(int), 0, (const struct sockaddr *)server_addr, serverlen); 
        base++;
      }
    } else if (packet.sequence_number < base) {
      // printf("Sending ack for already received sequence number - %d\n", packet.sequence_number);
      sendto(client_socket, &packet.sequence_number, sizeof(int), 0, (const struct sockaddr *)server_addr, serverlen);
    }
  }
}

//DELETE Command
void delete(int client_socket, struct sockaddr_in *server_addr, int serverlen) {
  int n;
  char buffer[BUFSIZE];
  int ack_number = 1;
  n = recvfrom(client_socket, buffer, BUFSIZE, 0, (struct sockaddr *)server_addr, &serverlen);
  if (n < 0) 
    error("ERROR in recvfrom \n");
  
  printf("Delete status: %s \n", buffer);
  printf("Sending acknowledgement\n");
  sendto(client_socket, &ack_number, sizeof(int), 0, (const struct sockaddr *)server_addr, serverlen); 

  //Wait for sometime (timeout) to check if client asks for acknowledgement again
  while (1) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(client_socket, &readfds);

    struct timeval timeout;
    timeout.tv_sec = TIMEOUT;
    timeout.tv_usec = 0;

    int ready = select(client_socket + 1, &readfds, NULL, NULL, &timeout);
    // printf("Waiting to receive\n");
    if (ready > 0 && FD_ISSET(client_socket, &readfds)) {
      n = recvfrom(client_socket, buffer, BUFSIZE, 0, (struct sockaddr *)server_addr, &serverlen); 
      if (n < 0) 
        error("ERROR in recvfrom \n");
      // printf("Delete status: %s \n", buffer);
      // printf("Sending acknowledgement again\n");
      sendto(client_socket, &ack_number, sizeof(int), 0, (const struct sockaddr *)server_addr, serverlen); 
    } else {
      // printf("Will close\n");
      break;
    }
  }
}

void send_command(int sockfd, char buf[BUFSIZE], struct sockaddr_in *serveraddr, int serverlen) {
  int n;
  struct timeval start, end;
  gettimeofday(&start, NULL);
  n = sendto(sockfd, buf, strlen(buf), 0, (const struct sockaddr *)serveraddr, serverlen);
  if (n < 0) 
    error("ERROR in sendto \n");

  int ack = 0;
  while (1) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;

    int ready = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
    // printf("Waiting to receive\n");
    if (ready > 0 && FD_ISSET(sockfd, &readfds)) {
      // Setting timeout
      // gettimeofday(&end, NULL);
      // global_timeout = timeval_diff(&start, &end); //In seconds
      // printf("Setting timeout - %ld\n", global_timeout);
      // if (global_timeout == 0)
      //   global_timeout = 500;
      // else
      //   global_timeout = global_timeout*2;
      // printf("Setting timeout - %ld\n", global_timeout);

      n = recvfrom(sockfd, &ack, sizeof(int), 0, NULL, NULL);
      if (n < 0) 
        error("ERROR in recvfrom \n");
      
      if (ack == 1) {
        // printf("Received ack for command \n");
        break;
      }
        
      
      // printf("Sending command again\n");
      sendto(sockfd, buf, strlen(buf), 0, (const struct sockaddr *)serveraddr, serverlen);
    } else {
      // printf("Will close\n");
      break;
    }
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
    
    char command[50];
    char filename[50];
    
    sscanf(buf, "%s %s", command, filename);
    
    if (sscanf(buf, "%s %s", command, filename) == 2) {
      if (strncmp(command, "get", 3) == 0) {
        send_command(sockfd, buf, &serveraddr, serverlen);
        printf("Getting file - %s from server \n", filename);
        go_back_n_receive_file(sockfd, filename, &serveraddr, serverlen);
      } else if (strncmp(command, "put", 3) == 0) {
        send_command(sockfd, buf, &serveraddr, serverlen);
        printf("Putting file - %s into server \n", filename);
        go_back_n_send_file(sockfd, filename, serveraddr, serverlen);
      } else if (strncmp(command, "delete", 6) == 0) {
        send_command(sockfd, buf, &serveraddr, serverlen);
        delete(sockfd, &serveraddr, serverlen);
      } else {
        printf("Unkown command, please enter a valid command\n");
      }
    } else if (sscanf(buf, "%s", command) == 1) {
      if (strncmp(command, "exit", 4) == 0) {
        send_command(sockfd, buf, &serveraddr, serverlen);
        break;
      } else if (strncmp(command, "ls", 2) == 0) {
        send_command(sockfd, buf, &serveraddr, serverlen);
        ls(sockfd, &serveraddr, serverlen);
      } else {
        printf("Unkown command, please enter a valid command\n");
      }
    } else {
      printf("Unkown command, please enter a valid command\n");
    }

    printf("---------------------------------------------------------------------------");
  }
  close(sockfd);
  return 0;
}