#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <sys/select.h>

#define BUFFER_SIZE 1024

void error(const char *msg) {
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[]) {
    // Check syntax: hw3client addr port name
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <host> <port> <name>\n", argv[0]);
        exit(1);
    }

    char *hostname = argv[1];
    int port = atoi(argv[2]);
    char *name = argv[3];

    int sockfd;
    struct sockaddr_in server_addr;
    struct hostent *server;

    //Create Socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);// TCP socket
    if (sockfd < 0) error("ERROR opening socket");

    // Resolve Hostname  "localhost" to 127.0.0.1
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr, "ERROR, no such host\n");
        exit(0);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&server_addr.sin_addr.s_addr, server->h_length);
    server_addr.sin_port = htons(port);

    //Connect to Server
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) 
        error("ERROR connecting");

    // Handshake: Notify server of our name
    //  send the name immediately after connecting.
    write(sockfd, name, strlen(name));

    // Main Select Loop
    fd_set readfds;
    printf("Connected to server as '%s'. You can start typing.\n", name);

    while(1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds); // Watch user input (fd 0)
        FD_SET(sockfd, &readfds);       // Watch server socket

        // Monitor for activity
        if (select(sockfd + 1, &readfds, NULL, NULL, NULL) < 0)
            error("ERROR in select");

        // Case A: Message received from Server
        if (FD_ISSET(sockfd, &readfds)) {
            char buffer[BUFFER_SIZE];
            memset(buffer, 0, BUFFER_SIZE);
            int n = read(sockfd, buffer, BUFFER_SIZE - 1);
            
            if (n <= 0) {
                printf("Server disconnected.\n");
                close(sockfd);
                exit(0);
            }
            // Display message as is
            printf("%s", buffer);
            // Flush stdout to ensure the user sees it immediately
            fflush(stdout); 
        }

        // Case B: User typed something
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char buffer[BUFFER_SIZE];
            memset(buffer, 0, BUFFER_SIZE);
            
            // Read line from stdin
            if (fgets(buffer, BUFFER_SIZE, stdin) != NULL) {
                
                // Handle !exit command
                if (strncmp(buffer, "!exit", 5) == 0) {
                    // Send to server first (so others know we left)
                    write(sockfd, buffer, strlen(buffer));
                    printf("client exiting\n");
                    close(sockfd);
                    exit(0);
                }

                // Send normal/whisper message to server
                write(sockfd, buffer, strlen(buffer));
            }
        }
    }

    return 0;
}