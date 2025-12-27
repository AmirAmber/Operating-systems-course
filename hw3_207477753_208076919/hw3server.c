#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024
#define MAX_FD 1024 

// Helper function to handle  errors and exit
void error(const char *msg) {
    perror(msg);
    exit(1);
}

// Global array to map Socket File Descriptors to Client Names
// Index = Socket FD, Value = Name String
char *client_names[MAX_FD];

int main(int argc, char *argv[]) {
    // Check command line arguments: hw3server port
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);// Convert the port argument from string to integer
    int server_sock, new_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    
    // Initialize names array to NULL
    for (int i = 0; i < MAX_FD; i++) client_names[i] = NULL;

    // Create the server socket (TCP)
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) error("Error opening socket");

    // Allow immediate reuse of the port
    int opt = 1;// Set socket option SO_REUSEADDR. This allows to restart the server immediately after it crashes or is closed
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind the socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; 
    server_addr.sin_port = htons(port);

    if (bind(server_sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) 
        error("Error on binding");

    // Listen for incoming connections
    listen(server_sock, 5);
    printf("Server listening on port %d...\n", port);

    // Setup for select()
    fd_set readfds;         // The master set containing all file descriptors to monitor
    fd_set temp_fds;        // A temporary set because select() modifies the set passed to it
    int max_fd;             // A temporary set because select() modifies the set passed to it

    FD_ZERO(&readfds);      // Initialize the master set to empty
    FD_SET(server_sock, &readfds); // Add the main listener socket to the set
    max_fd = server_sock;

    // Main Server Loop
    while (1) {
        temp_fds = readfds; 

        if (select(max_fd + 1, &temp_fds, NULL, NULL, NULL) < 0) 
            error("Error in select");

        // Iterate through all FDs
        for (int i = 0; i <= max_fd; i++) {
            if (FD_ISSET(i, &temp_fds)) {
                
                // --- CASE 1: NEW CONNECTION ---
                if (i == server_sock) {
                    client_len = sizeof(client_addr);
                    new_sock = accept(server_sock, (struct sockaddr *) &client_addr, &client_len);
                    
                    if (new_sock < 0) {
                        perror("Accept error");
                    } else if (new_sock >= MAX_FD) {
                        close(new_sock);
                    } else {
                        FD_SET(new_sock, &readfds);
                        if (new_sock > max_fd) max_fd = new_sock;

                        // Handshake
                        char name_buf[BUFFER_SIZE];
                        memset(name_buf, 0, BUFFER_SIZE);
                        int len = read(new_sock, name_buf, BUFFER_SIZE - 1);
                        
                        if (len > 0) {
                            name_buf[strcspn(name_buf, "\r\n")] = 0; 
                            client_names[new_sock] = strdup(name_buf); 
                            
                            printf("client %s connected from %s\n", 
                                   client_names[new_sock], inet_ntoa(client_addr.sin_addr));
                        } else {
                            close(new_sock);
                            FD_CLR(new_sock, &readfds);
                        }
                    }
                } 
                // --- CASE 2: INCOMING DATA FROM CLIENT ---
                else {
                    char buffer[BUFFER_SIZE];
                    memset(buffer, 0, BUFFER_SIZE);
                    int n = read(i, buffer, BUFFER_SIZE - 1);
                    
                    // --- DISCONNECTION HANDLING ---
                    if (n <= 0) {
                        if (client_names[i] != NULL) {
                            printf("client %s disconnected\n", client_names[i]);
                            free(client_names[i]); 
                            client_names[i] = NULL;
                        }
                        close(i);            
                        FD_CLR(i, &readfds); 
                    } 
                    // --- MESSAGE HANDLING ---
                    else {
                        char formatted_msg[BUFFER_SIZE + 50];
                        memset(formatted_msg, 0, sizeof(formatted_msg));
                        sprintf(formatted_msg, "%s: %s", client_names[i], buffer);

                        // Check for Whisper Message
                        char *space_ptr = strchr(buffer, ' ');
                        
                        if (buffer[0] == '@' && space_ptr != NULL) {
                            int name_len = space_ptr - (buffer + 1);
                            char target_name[100];
                            memset(target_name, 0, sizeof(target_name));
                            strncpy(target_name, buffer + 1, name_len);

                            // Find target and send ONLY to them
                            for (int j = 0; j <= max_fd; j++) {
                                if (client_names[j] != NULL && strcmp(client_names[j], target_name) == 0) {
                                    write(j, formatted_msg, strlen(formatted_msg));
                                    break; 
                                }
                            }
                        } 
                        else {
                            // Normal Message: Broadcast to ALL
                            for (int j = 0; j <= max_fd; j++) {
                                if (FD_ISSET(j, &readfds) && j != server_sock) {
                                    write(j, formatted_msg, strlen(formatted_msg));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    close(server_sock);
    return 0;
}