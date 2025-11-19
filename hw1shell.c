#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_LINE 1024
#define MAX_ARGS 64

int main() {
    char line[MAX_LINE];
    char *args[MAX_ARGS];

    while (1) {
        // 1. Print the prompt (Guideline 1)
        // We use fflush because printf does not automatically flush without a \n
        printf("hw1shell$ ");
        fflush(stdout);

        // 2. Read user input
        if (fgets(line, MAX_LINE, stdin) == NULL) {
            break; // Exit on Ctrl+D or error
        }
        
        // Remove the trailing newline character from fgets
        line[strcspn(line, "\n")] = 0;

        // 3. Parse the input (Tokenize)
        int i = 0;
        args[i] = strtok(line, " ");
        while (args[i] != NULL) {
            i++;
            args[i] = strtok(NULL, " ");
        }
        
        // If the user pressed enter without typing a command, skip
        if (args[0] == NULL) {
            continue;
        }

        // 4. Check for Internal Commands
        if (strcmp(args[0], "exit") == 0) {
            break; // Break the loop to exit the shell
        } 
        else if (strcmp(args[0], "cd") == 0) {
            if (args[1] == NULL) {
                fprintf(stderr, "hw1shell: expected argument to \"cd\"\n");
            } else {
                if (chdir(args[1]) != 0) {
                    perror("hw1shell");
                }
            }
        } 
        else if (strcmp(args[0], "jobs") == 0) {
            // TODO: Implement job logic here based on further instructions
            printf("This is where the jobs command runs.\n");
        } 
        else {
            // 5. External Commands (Fork & Exec)
            pid_t pid = fork();

            if (pid < 0) {
                // Error forking
                perror("Fork failed");
            } else if (pid == 0) {
                // --- CHILD PROCESS ---
                // Execute the command
                // execvp searches the PATH for the command automatically
                if (execvp(args[0], args) == -1) {
                    printf("Error: Command not found: %s\n", args[0]);
                }
                exit(1); // Make sure child exits if exec fails
            } else {
                // --- PARENT PROCESS ---
                // Wait for the child to finish
                wait(NULL); 
            }
        }
    }

    return 0;
}