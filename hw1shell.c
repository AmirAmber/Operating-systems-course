#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_LINE 1024
#define MAX_ARGS 64
#define MAX_JOBS 4

// flags for clarity
int is_background = 0;
int active_jobs = 0;

// Data structure to track background jobs
typedef struct {
    pid_t pid;
    char cmd[MAX_LINE];
} Job;

Job background_jobs[MAX_JOBS];

// Helper to initialize jobs array
void init_jobs() {
    for (int i = 0; i < MAX_JOBS; i++) {
        background_jobs[i].pid = 0;
    }
}

// Helper to add a job
// Returns index on success, -1 if full (though we check count before calling)
int add_job(pid_t pid, const char* cmd) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (background_jobs[i].pid == 0) {
            background_jobs[i].pid = pid;
            strncpy(background_jobs[i].cmd, cmd, MAX_LINE - 1);
            background_jobs[i].cmd[MAX_LINE - 1] = '\0'; // Ensure null termination
            active_jobs++;
            return i;
        }
    }
    return -1;
}

// Helper to remove a job by PID
void remove_job(pid_t pid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (background_jobs[i].pid == pid) {
            background_jobs[i].pid = 0;
            active_jobs--;
            break;
        }
    }
}

// Helper to print system call errors
void print_syscall_error(const char* syscall_name) {
    printf("hw1shell: %s failed, errno is %d\n", syscall_name, errno);
}

// Helper to reap zombies (Guideline 12)
void reap_zombies() {
    int status;
    pid_t pid;
    
    // WNOHANG returns immediately if no child has exited
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // Check if this pid was in our background list
        for (int i = 0; i < MAX_JOBS; i++) {
            if (background_jobs[i].pid == pid) {
                printf("hw1shell: pid %d finished\n", pid);
                remove_job(pid);
                break;
            }
        }
        // Note: If waitpid returns a PID not in our list, it might have been 
        // a foreground process caught here, but foregrounds are usually handled 
        // by the blocking waitpid in the main loop.
    }
}

int main() {
    char line[MAX_LINE];
    char line_copy[MAX_LINE]; // To preserve original command for jobs list
    char *args[MAX_ARGS];
    
    init_jobs();

    while (1) {
        // 1. Print Prompt
        printf("hw1shell$ ");
        fflush(stdout);

        // 2. Read Input
        if (fgets(line, MAX_LINE, stdin) == NULL) {
            break; // Exit on Ctrl+D
        }

        // Remove newline
        line[strcspn(line, "\n")] = 0;

        // 10. Empty command check
        if (strlen(line) == 0) {
            continue;
        }

        // Save a copy of the line before strtok destroys it
        strncpy(line_copy, line, MAX_LINE);
        
        // 3. Tokenize
        int i = 0;
        args[i] = strtok(line, " ");
        while (args[i] != NULL) {
            i++;
            args[i] = strtok(NULL, " ");
        }

        if (args[0] == NULL) continue; // Should be covered by empty check, but safety first

        // 5. Check for Background '&'
        if (i > 0 && strcmp(args[i-1], "&") == 0) {
            is_background = 1;
            args[i-1] = NULL; // Remove '&' from arguments passed to exec
            
            // Clean up the line_copy for display in 'jobs' (remove the &)
            char *ampersand_ptr = strrchr(line_copy, '&');
            if (ampersand_ptr) *ampersand_ptr = '\0';
        }

        // --- Internal Commands ---

        // 2. exit
        if (strcmp(args[0], "exit") == 0) {
            // Wait for all children before exiting
            // Not strictly required to print "finished" for these according to PDF, 
            // but required to "wait and reap".
            while(wait(NULL) > 0); 
            break;
        } 
        
        // 3. cd
        else if (strcmp(args[0], "cd") == 0) {
            // "other variants should display hw1shell: invalid command"
            // If no arg provided, or chdir fails
            if (args[1] == NULL || chdir(args[1]) != 0) {
                printf("hw1shell: invalid command\n");
            }
        } 
        
        // 4. jobs
        else if (strcmp(args[0], "jobs") == 0) {
            for (int j = 0; j < MAX_JOBS; j++) {
                if (background_jobs[j].pid != 0) {
                    printf("%d\t%s\n", background_jobs[j].pid, background_jobs[j].cmd);
                }
            }
        } 
        
        // --- External Commands ---
        else {
            // 6. Check background limit
            if (is_background && active_jobs >= MAX_JOBS) {
                printf("hw1shell: too many background commands running\n");
            } 
            else {
                // 7. Fork
                pid_t pid = fork();

                if (pid < 0) {
                    // 13. System call fail
                    print_syscall_error("fork");
                } 
                else if (pid == 0) {
                    // --- CHILD ---
                    // Execute command
                    execvp(args[0], args);
                    
                    // 11. Exec failed
                    // If we are here, execvp returned (failed)
                    printf("hw1shell: invalid command\n");
                    exit(1);
                } 
                else {
                    // --- PARENT ---
                    if (is_background) {
                        // 9. Background handling
                        printf("hw1shell: pid %d started\n", pid);
                        add_job(pid, line_copy);
                    } else {
                        // 8. Foreground handling
                        // Wait specifically for this child
                        waitpid(pid, NULL, 0);
                    }
                }
            }
        }

        // 12. Reap zombies at the end of iteration
        reap_zombies();
    }

    return 0;
}