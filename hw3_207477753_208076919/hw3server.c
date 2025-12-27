#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>


#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

// Helper function to see errors
void error(const char *msg) {
  perror(msg);
  exit(1);
}

int main(int argc, char *argv[]) {
  // Check command line arguments: hw3server port 
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    exit(1);
  }

  int port = atoi(argv[1]);
  int server_sock, new_sock;
  struct sockaddr_in server_addr, client_addr;
  socklen_t client_len;

  //  Create the server socket TCP
  server_sock = socket(AF_INET, SOCK_STREAM, 0); //  Who/Where are we connecting to IPv4 , How is the data transported TCP
  if (server_sock < 0)
    error("Error opening socket");

  // Option to allow immediate reuse of the port 
  int opt = 1;
  setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  // 2. Bind the socket to the address and port
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
  server_addr.sin_port = htons(port);

  if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0)
    error("Error on binding");

  // 3. Listen for incoming connections
  listen(server_sock, 5);
  printf("Server listening on port %d...\n", port);

  // 4. Setup for select()
  fd_set readfds;  // Set of file descriptors for reading
  fd_set temp_fds; // Temporary set for select()
  int max_fd;      // Highest file descriptor number

  FD_ZERO(&readfds);
  FD_SET(server_sock, &readfds); // Add the main listener socket to the set
  max_fd = server_sock;

  // Main Server Loop
  while (1) {
    temp_fds = readfds; // Copy because select() modifies the set

    // Wait for activity on any of the sockets
    if (select(max_fd + 1, &temp_fds, NULL, NULL, NULL) < 0)
      error("Error in select");

    // Loop through all possible file descriptors to see which one is active
    for (int i = 0; i <= max_fd; i++) {
      if (FD_ISSET(i, &temp_fds)) {

        // Case 1: Activity on the listener socket -> New Connection
        if (i == server_sock) {
          client_len = sizeof(client_addr);
          new_sock =
              accept(server_sock, (struct sockaddr *)&client_addr, &client_len);

          if (new_sock < 0) {
            perror("Accept error");
          } else {
            // Add new socket to the set
            FD_SET(new_sock, &readfds);
            if (new_sock > max_fd)
              max_fd = new_sock;

            // We will add the "client name connected" logic later
            // For now, just print the IP
            printf("New connection accepted from %s (Socket FD: %d)\n",
                   inet_ntoa(client_addr.sin_addr), new_sock);
          }
        }
        // Case 2: Activity on a client socket -> Incoming Data
        else {
          char buffer[BUFFER_SIZE];
          memset(buffer, 0, BUFFER_SIZE);
          int n = read(i, buffer, BUFFER_SIZE - 1);

          if (n <= 0) {
            // Connection closed or error
            printf("Socket %d disconnected\n", i);
            close(i);
            FD_CLR(i, &readfds); // Remove from set
          } else {
            // Data received (we will handle routing later)
            printf("Received data from socket %d: %s\n", i, buffer);
          }
        }
      }
    }
  }

  close(server_sock);
  return 0;
}