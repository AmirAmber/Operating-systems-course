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

void error(const char *msg) {
    perror(msg);
    exit(1);
}

// Global array to map Socket File Descriptors to Client Names
char *client_names[MAX_FD];

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    int server_sock, new_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    
    // Initialize names array
    for (int i = 0; i < MAX_FD; i++) client_names[i] = NULL;

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) error("Error opening socket");

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) 
        error("Error on binding");

    listen(server_sock, 5);
    printf("Server listening on port %d...\n", port);

    fd_set readfds, temp_fds;
    int max_fd;

    FD_ZERO(&readfds);
    FD_SET(server_sock, &readfds);
    max_fd = server_sock;

    while (1) {
        temp_fds = readfds;

        if (select(max_fd + 1, &temp_fds, NULL, NULL, NULL) < 0) 
            error("Error in select");

        for (int i = 0; i <= max_fd; i++) {
            if (FD_ISSET(i, &temp_fds)) {
                
                // --- NEW CONNECTION ---
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

                        // Handshake: Read Name
                        char name_buf[BUFFER_SIZE];
                        memset(name_buf, 0, BUFFER_SIZE);
                        int len = read(new_sock, name_buf, BUFFER_SIZE - 1);
                        
                        if (len > 0) {
                            name_buf[strcspn(name_buf, "\r\n")] = 0; // Strip newlines
                            client_names[new_sock] = strdup(name_buf);
                            printf("client %s connected from %s\n", 
                                   client_names[new_sock], inet_ntoa(client_addr.sin_addr));
                        } else {
                            close(new_sock);
                            FD_CLR(new_sock, &readfds);
                        }
                    }
                } 
                // --- INCOMING MESSAGE ---
                else {
                    char buffer[BUFFER_SIZE];
                    memset(buffer, 0, BUFFER_SIZE);
                    int n = read(i, buffer, BUFFER_SIZE - 1);
                    
                    if (n <= 0) {
                        // Disconnect
                        if (client_names[i] != NULL) {
                            printf("client %s disconnected\n", client_names[i]);
                            free(client_names[i]);
                            client_names[i] = NULL;
                        }
                        close(i);
                        FD_CLR(i, &readfds);
                    } 
                    else {
                        // Prepare the message prefix: "SenderName: "
                        char formatted_msg[BUFFER_SIZE + 50];
                        memset(formatted_msg, 0, sizeof(formatted_msg));
                        sprintf(formatted_msg, "%s: %s", client_names[i], buffer);

                        // --- LOGIC SPLIT: WHISPER VS NORMAL ---
                        
                        // Check for Whisper: Starts with '@' and contains a space
                        char *space_ptr = strchr(buffer, ' ');
                        
                        if (buffer[0] == '@' && space_ptr != NULL) {
                            // 1. Extract the target name
                            // The name is between buffer[1] and space_ptr
                            int name_len = space_ptr - (buffer + 1);
                            char target_name[100];
                            memset(target_name, 0, sizeof(target_name));
                            strncpy(target_name, buffer + 1, name_len);

                            // 2. Find the target client
                            int target_found = 0;
                            for (int j = 0; j <= max_fd; j++) {
                                if (client_names[j] != NULL && strcmp(client_names[j], target_name) == 0) {
                                    // 3. Send ONLY to target
                                    // Spec: "sent just to the destination client"
                                    write(j, formatted_msg, strlen(formatted_msg));
                                    target_found = 1;
                                    break; 
                                }
                            }
                            // Optional: If target not found, we do nothing (or could print error to server log)
                            if (!target_found) {
                                printf("Debug: Whisper target '%s' not found.\n", target_name);
                            }
                        } 
                        else {
                            // --- NORMAL MESSAGE (Broadcast) ---
                            // Spec: "sent to all connected clients (including back to the client)"
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