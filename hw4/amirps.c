#include "types.h"
#include "user.h"
#include "processInfo.h"

// Helper enum for printing state strings
enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

const char* getStateName(int state) {
    switch (state) {
        case UNUSED: return "UNUSED";
        case EMBRYO: return "embryo";
        case SLEEPING: return "sleeping";
        case RUNNABLE: return "runnable";
        case RUNNING: return "running";
        case ZOMBIE: return "zombie";
        default: return "???";
    }
}

int main(int argc, char *argv[]) {
    // 1. Print Header Info [cite: 32]
    int total_procs = getNumProc();
    int max_pid = getMaxPid();
    
    printf(1, "Total number of active processes: %d\n", total_procs);
    printf(1, "Maximum PID: %d\n", max_pid);
    
    // 2. Print Table Headers [cite: 33-38]
    printf(1, "PID\tSTATE\tPPID\tSZ\tNFD\tNRSWITCH\n");
    
    // 3. Iterate and Print Rows [cite: 39]
    struct processInfo info;
    
    // Iterate from 1 up to max_pid to print in sorted order
    for (int i = 1; i <= max_pid; i++) {
        if (getProcInfo(i, &info) == 0) {
            // Found a process, print its details
            // Align columns with tabs or spaces [cite: 40]
            printf(1, "%d\t%s\t%d\t%d\t%d\t%d\n", 
                   i, 
                   getStateName(info.state), 
                   info.ppid, 
                   info.sz, 
                   info.nfd, 
                   info.nrswitch);
        }
    }
    
    exit();
}