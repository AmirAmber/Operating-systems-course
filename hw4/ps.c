#include "types.h"
#include "stat.h"
#include "user.h"
#include "processInfo.h"

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// FIX 1: Pad these strings with spaces so they are all same length (e.g. 8 chars)
static char *states[] = {
  [UNUSED]    "unused  ",
  [EMBRYO]    "embryo  ",
  [SLEEPING]  "sleeping", 
  [RUNNABLE]  "runnable",
  [RUNNING]   "running ", // Added space
  [ZOMBIE]    "zombie  "  // Added spaces
};

int main(int argc, char *argv[]){
  struct processInfo info;
  int max_pid, num_proc;

  num_proc = getNumProc();
  max_pid = getMaxPid();

  printf(1, "Total number of active processes: %d\n", num_proc);
  printf(1, "Maximum PID: %d\n", max_pid);

  // FIX 2: Add an extra \t after STATE to align with the long data strings
  printf(1, "PID\tSTATE\t\tPPID\tSZ\tNFD\tNRSWITCH\n");

  for(int i = 1; i <= max_pid; i++){
    if(getProcInfo(i, &info) == 0) {
      char *state_str = "???     "; // Pad default too
      if(info.state >= 0 && info.state < 6 && states[info.state])
        state_str = states[info.state];

      printf(1, "%d\t%s\t%d\t%d\t%d\t%d\n", 
             i, 
             state_str, 
             info.ppid, 
             info.sz, 
             info.nfd, 
             info.nrswitch);
    }
  }

  exit();
}