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
// FD_SETSIZE is usually 1024, plenty for our array mapping
#define MAX_FD 1024 

void error(const char *msg) {
    perror(msg);
    exit(1);
}

// Global array to map Socket File Descriptors to Client Names
// Index = Socket FD, Value = Name String
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
    
    // Initialize names array to NULL
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
                        fprintf(stderr, "Too many clients. Connection rejected.\n");
                        close(new_sock);
                    } else {
                        FD_SET(new_sock, &readfds);
                        if (new_sock > max_fd) max_fd = new_sock;

                        // HANDSHAKE: Read the name immediately
                        char name_buf[BUFFER_SIZE];
                        memset(name_buf, 0, BUFFER_SIZE);
                        // We assume the client sends the name immediately upon connect
                        int len = read(new_sock, name_buf, BUFFER_SIZE - 1);
                        
                        if (len > 0) {
                            // Strip newline if present (though client usually sends pure string)
                            name_buf[strcspn(name_buf, "\n")] = 0;
                            client_names[new_sock] = strdup(name_buf);
                            
                            // REQUIRED OUTPUT: client <name> connected from <address>
                            printf("client %s connected from %s\n", 
                                   client_names[new_sock], inet_ntoa(client_addr.sin_addr));
                        } else {
                            // If read fails immediately, close
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
                    
                    // Client Disconnected
                    if (n <= 0) {
                        // REQUIRED OUTPUT: client <name> disconnected
                        if (client_names[i] != NULL) {
                            printf("client %s disconnected\n", client_names[i]);
                            free(client_names[i]);
                            client_names[i] = NULL;
                        }
                        close(i);
                        FD_CLR(i, &readfds);
                    } 
                    // Normal Message handling
                    else {
                        // Construct message: "name: original_msg"
                        char formatted_msg[BUFFER_SIZE + 50];
                        memset(formatted_msg, 0, sizeof(formatted_msg));
                        
                        // Note: buffer might already contain newline from fgets in client
                        sprintf(formatted_msg, "%s: %s", client_names[i], buffer);
                        
                        // BROADCAST to ALL clients (including sender)
                        for (int j = 0; j <= max_fd; j++) {
                            // Send to everyone who is in the set AND is not the listener socket
                            if (FD_ISSET(j, &readfds) && j != server_sock) {
                                write(j, formatted_msg, strlen(formatted_msg));
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