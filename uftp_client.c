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

#define BUFSIZE 1024
#define MAX_PATH_LEN 512
#define WINDOW_SIZE 5
#define TIMEOUT 2 // Timeout in seconds

typedef struct {
    int sequence_number;
    char data[BUFSIZE];
    struct timeval sent_time;
    // int is_ack_received;
} Packet;

// Packet window[WINDOW_SIZE];
// int base = 0, nextseqnum = 0;
// int sockfd;
// struct sockaddr_in server_addr1;
// int server_addr_len = sizeof(server_addr1);

// void handle_timeout(int signum) {
//   // Retransmit packets from base to nextseqnum
//   printf("Timeout occurred, retransmitting packets from sequence number %d to %d\n", base, nextseqnum - 1);
//   for (int i = base; i < nextseqnum; i++) {
//     Packet *packet = &window[i % WINDOW_SIZE];
//     if (!packet->is_ack_received) {
//       sendto(sockfd, packet, sizeof(Packet), 0, (const struct sockaddr *)&server_addr1, server_addr_len);
//       gettimeofday(&packet->sent_time, NULL);
//     }
//   }
// }

long timeval_diff(struct timeval *start_time, struct timeval *end_time) {
    return (end_time->tv_sec - start_time->tv_sec) * 1000 +
           (end_time->tv_usec - start_time->tv_usec) / 1000;
}

void go_back_n_send_file2(int client_socket, const char *filename, struct sockaddr_in server_addr, int serverlen) {
  FILE *file = fopen(filename, "rb");

  if (file == NULL) {
    perror("File not found \n");
    exit(0);
  }
  
  Packet window[WINDOW_SIZE];
  int base = 0, nextseqnum = 0;
  int a = 0;

  struct timeval timeout;
  timeout.tv_sec = TIMEOUT;
  timeout.tv_usec = 0;

  while (1) {
      // Send packets within the window
    while (nextseqnum < base + WINDOW_SIZE && a == 0) {
      Packet packet;
      packet.sequence_number = nextseqnum;

      memset(packet.data, '\0', BUFSIZE); 
      ssize_t bytes_read = fread(packet.data, 1, BUFSIZE, file);
      if (bytes_read <= 0) {
        printf("EOF\n");
        memset(packet.data, '\0', BUFSIZE); 
        memcpy(packet.data, "EOF", 3); 
        // Set transmission time for the packet
        gettimeofday(&packet.sent_time, NULL);
        printf("Sending sequence number - %d\n", packet.sequence_number);
        printf("Sending content - %s\n", packet.data);
        sendto(client_socket, &packet, bytes_read + sizeof(int) + sizeof(packet.sent_time), 0, (const struct sockaddr *)&server_addr, serverlen);
        nextseqnum++;
        a = 1;
        break; // End of file
      }

      // Set transmission time for the packet
      gettimeofday(&packet.sent_time, NULL);
      printf("Sending sequence number - %d\n", packet.sequence_number);
      printf("Sending content - %s\n", packet.data);
      sendto(client_socket, &packet, bytes_read + sizeof(int) + sizeof(packet.sent_time), 0, (const struct sockaddr *)&server_addr, serverlen);
      nextseqnum++;

      // Store the packet in the window for retransmission if needed
      window[packet.sequence_number % WINDOW_SIZE] = packet;
    }

    // Check for timeout on each packet in the window
    struct timeval current_time;
    gettimeofday(&current_time, NULL);

    for (int i = base; i < nextseqnum; i++) {
        Packet *packet = &window[i % WINDOW_SIZE];
        long elapsed_time = timeval_diff(&packet->sent_time, &current_time);
        if (elapsed_time >= TIMEOUT * 1000) {
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
  fclose(file);
}

// void go_back_n_send_file1(int client_socket, const char *filename, struct sockaddr_in server_addr, int serverlen) {
//   memset(window, 0, sizeof(window));
//   base = 0;
//   nextseqnum = 0;
//   sockfd = client_socket;
//   server_addr1 = server_addr;
//   server_addr_len = serverlen;

//   FILE *file = fopen(filename, "rb");

//   if (file == NULL) {
//     perror("File not found \n");
//     exit(0);
//   }

//   // signal(SIGALRM, handle_timeout);

//   int a = -1, b = -1;

//   while (1) {
//     // Send packets within the window
//     while (nextseqnum < base + WINDOW_SIZE) {
//       // if (a != -1)
//       //   break;
//       Packet *packet = &window[nextseqnum % WINDOW_SIZE];
//       packet->sequence_number = nextseqnum;

//       ssize_t bytes_read = fread(packet->data, 1, sizeof(packet->data), file);
      
//       if (bytes_read <= 0) {
//         printf("EOF\n");
//         // packet->is_ack_received = 0;
//         memset(packet->data, '\0', sizeof(packet->data)); 
//         memcpy(packet->data, "EOF", 3); 
//         sendto(sockfd, packet, sizeof(packet), 0, (const struct sockaddr *)&server_addr, server_addr_len);
//         // a = packet->sequence_number;
//         // a = 1;
//         gettimeofday(&packet->sent_time, NULL);

//         // Start timer for the packet
//         // alarm(TIMEOUT);
//         nextseqnum++;
//         break; // End of file
//       }

//       packet->is_ack_received = 0;
//       printf("Sending packet %d\n", packet->sequence_number);
//       // printf("Sending content - %s\n", packet->data);
//       sendto(sockfd, packet, sizeof(packet), 0, (const struct sockaddr *)&server_addr, server_addr_len);
//       gettimeofday(&packet->sent_time, NULL);

//       // Start timer for the packet
//       // alarm(TIMEOUT);
//       nextseqnum++;
//     }
      
//     // Wait for acknowledgment or timeout
//     while (1) {
//       fd_set readfds;
//       FD_ZERO(&readfds);
//       FD_SET(sockfd, &readfds);

//       // Set timeout for select
//       struct timeval end, start;
//       start = window[base % WINDOW_SIZE].sent_time;
//       gettimeofday(&end, NULL);
//       double elapsed_time = (end.tv_sec - start.tv_sec) + ((end.tv_usec - start.tv_usec) / 1000000.0);
      
//       struct timeval tv;
//       tv.tv_sec = elapsed_time - TIMEOUT;
//       tv.tv_usec = 0;

//       if (select(sockfd + 1, &readfds, NULL, NULL, &tv) == 0) {
//         // Timeout occurred
//         handle_timeout(SIGALRM);
//       }

//       if (FD_ISSET(sockfd, &readfds)) {
//         // Receive acknowledgment
//         int ack_number;
//         if (recvfrom(sockfd, &ack_number, sizeof(int), 0, (struct sockaddr *)&server_addr, &server_addr_len) < 0) {
//           perror("Recvfrom failed");
//           exit(0);
//         }

//         printf("Received acknowledgment for sequence number = %d\n", ack_number);

//         // Mark the packet as acknowledged
//         for (int i = base; i <= ack_number; i++) {
//           window[i % WINDOW_SIZE].is_ack_received = 1;
//         }

//         if (base == ack_number)
//           alarm(0);

//         // Move base forward
//         if (ack_number >= base)
//           base = ack_number + 1;

//         printf("base = %d == %d == %d\n", base, ack_number, nextseqnum);
//         // Stop timer if all packets are acknowledged
//         if (base == nextseqnum) {
//           alarm(0);
//         }
//       }

//       if (a == 1) {
//         alarm(0);
//         break;
//       }
//       // Break the loop if all packets are acknowledged
//       if (base == nextseqnum) {
//         break;
//       }
//     }
//   }

//   fclose(file);
// }

// void shiftWindow(struct packet arr[]) {
//   for (int i = 0; i < (WINDOW_SIZE - 1); i++) {
//     arr[i] = arr[i + 1];
//   }
// }

//PUT command
// void go_back_n_send_file(int client_socket, const char *filename, struct sockaddr_in server_addr, int serverlen) {
//   char buffer[BUFSIZE];
//   ssize_t bytes_read;
//   FILE *file = fopen(filename, "rb");
//   int i = 0, j = 0;
//   int count = 0;
//   struct packet packets[WINDOW_SIZE];
  
//   struct timeval start, end;

//   for (j = 0; j < WINDOW_SIZE; j++) {
//     bytes_read = fread(buffer, 1, BUFSIZE, file);
//     if (bytes_read > 0) {
//       // sequence_number_arr[j] = count;
//       // sendto(client_socket, &sequence_number_arr[j], sizeof(int), 0, (struct sockaddr *)&server_addr, serverlen);
//       // sendto(client_socket, buffer, bytes_read, 0, (struct sockaddr *)&server_addr, serverlen);
//       // gettimeofday(&start, NULL);
//       // timeout[j] = start;
//       packets[j].sequence_number = count;
//       memcpy(packets[j].buffer, buffer, sizeof(buffer)); 
//       gettimeofday(&start, NULL);
//       packets[j].timeout = start;
//       packets[j].bytes_read = bytes_read;

//       printf("Sending sequence number - %d", packets[j].sequence_number);
//       printf("Sending content - %s", packets[j].buffer);
//       sendto(client_socket, &packets[j].sequence_number, sizeof(int), 0, (struct sockaddr *)&server_addr, serverlen);
//       sendto(client_socket, packets[j].buffer, packets[j].bytes_read, 0, (struct sockaddr *)&server_addr, serverlen);
//       count++;
//     }
//   }

//   while (1) {
//     //Check if the first packed in the window has timed out
//     // struct timeval begin = timeout[i];
//     struct timeval begin = packets[i].timeout;
//     gettimeofday(&end, NULL);
//     double elapsed_time = (end.tv_sec - begin.tv_sec) + ((end.tv_usec - begin.tv_usec) / 1000000.0);
//     if (elapsed_time >= TIMEOUT) {
//       for (int k = i; k < WINDOW_SIZE; k++) {
//         printf("Timeout occurred for packet %d. Retransmitting...\n", packets[k].sequence_number);
//         sendto(client_socket, &packets[k].sequence_number, sizeof(int), 0, (struct sockaddr *)&server_addr, serverlen);
//         sendto(client_socket, packets[k].buffer, packets[k].bytes_read, 0, (struct sockaddr *)&server_addr, serverlen);
//         gettimeofday(&start, NULL); // Restart timer
//         packets[k].timeout = start;
//       }
//     } 

//     // int ack_received = 0;
//     // while (!ack_received) {
//       // Check for acknowledgment
//     fd_set readfds;
//     FD_ZERO(&readfds);
//     FD_SET(client_socket, &readfds);

//     struct timeval timeout;
//     timeout.tv_sec = TIMEOUT - elapsed_time;
//     timeout.tv_usec = 0;

//     int ready = select(client_socket + 1, &readfds, NULL, NULL, &timeout);
//     if (ready > 0 && FD_ISSET(client_socket, &readfds)) {
//       int ack;
//       if (recvfrom(client_socket, &ack, sizeof(int), 0, NULL, NULL) > 0 && ack == packets[i].sequence_number) {
//         printf("Ack received - %d \n", packets[i].sequence_number);
        
//         // Move to the next sequence number
//         bytes_read = fread(buffer, 1, BUFSIZE, file);
        
//         if (bytes_read > 0) {
//           printf("Advance to next sequence number %d \n", count);
//           int index = WINDOW_SIZE - 1;
//           shiftWindow(packets);
//           packets[index].sequence_number = count;
//           count++;
//           memcpy(packets[index].buffer, buffer, sizeof(buffer)); 
//           gettimeofday(&start, NULL);
//           packets[index].bytes_read = bytes_read;
          
//           printf("Sending sequence number - %d", packets[index].sequence_number);
//           printf("Sending content - %s", packets[index].buffer);
//           sendto(client_socket, &packets[index].sequence_number, sizeof(int), 0, (struct sockaddr *)&server_addr, serverlen);
//           sendto(client_socket, packets[index].buffer, packets[index].bytes_read, 0, (struct sockaddr *)&server_addr, serverlen);
//           gettimeofday(&start, NULL);
//           packets[index].timeout = start;
//         } else {
//           if (i == (WINDOW_SIZE - 1)) {
//             printf("End of file\n");
//             break;
//           }
//           printf("End of content - Decreasing window size i = %d\n", i);
//           i++;
//         }
//       }
//     }
//   }
//   sendto(client_socket, &count, sizeof(int), 0, (struct sockaddr *)&server_addr, serverlen);
//   sendto(client_socket, "EOF", 3, 0, &server_addr, sizeof(server_addr));
//   fclose(file);
// }

// void update_time_out() {
//   for (int k = 0; k < WINDOW_SIZE-1; k++) {
//     timeout[k] = timeout[k+1];
//     sequence_number_arr[k] = sequence_number_arr[k+1];
//   }
// }

// void update_sequence_number() {
//   for (int k = 0; k < WINDOW_SIZE-1; k++) {
//     timeout[k] = timeout[k+1];
//     sequence_number_arr[k] = sequence_number_arr[k+1];
//   }
// }

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
  if (file == NULL) {
    error("File open failed");
    return; 
  }

  int expected_sequence_number = 0;
  while (1) {
    int sequence_number;
    bytes_received = recvfrom(client_socket, &sequence_number, sizeof(int), 0, NULL, NULL);
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
    if (strcmp(buffer, "Error: File not found") == 0) {
      perror("File not found in server");
      return;
    }
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
        // send_file(sockfd, filename, serveraddr, serverlen);
        // go_back_n_send_file1(sockfd, filename, serveraddr, serverlen);
        go_back_n_send_file2(sockfd, filename, serveraddr, serverlen);
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