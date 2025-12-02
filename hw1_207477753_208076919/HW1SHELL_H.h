#ifndef HW1SHELL_H
#define HW1SHELL_H

#include <sys/types.h>  // pid_t

#define MAX_LINE 1024
#define MAX_ARGS 64
#define MAX_JOBS 4

typedef struct {
    pid_t pid;
    char cmd[MAX_LINE];
} Job;

extern Job background_jobs[MAX_JOBS];
extern int active_jobs;
extern int is_background;

void init_jobs(void);
int  add_job(pid_t pid, const char* cmd);
void remove_job(pid_t pid);
void print_syscall_error(const char* syscall_name);
void reap_zombies(void);

#endif // HW1SHELL_H