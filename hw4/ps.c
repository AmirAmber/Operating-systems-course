#include "types.h"
#include "stat.h"
#include "user.h"
#include "proc.h"
#include "processInfo.h"

static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleeping", 
  [RUNNABLE]  "runnable",
  [RUNNING]   "running",
  [ZOMBIE]    "zombie"
};

int main(int argc, char *argv[]){
  struct processInfo info;
  int max_pid, num_proc;

  num_proc = getNumProc();
  max_pid = getMaxPid();

  printf(1, "Total number of active processes: %d\n", num_proc);
  printf(1, "Maximum PID: %d\n", max_pid);

  // Using tabs (\t) for alignment as suggested
  printf(1, "PID\tSTATE\tPPID\tSZ\tNFD\tNRSWITCH\n");

  // 3. Iterate and Print Table
  // PDF requires sorting by PID[cite: 39]. Since we iterate 1 to max_pid, 
  // we naturally print in ascending order.
  for(int i = 1; i <= max_pid; i++){
    if(getProcInfo(i, &info) == 0) {
      // Decode state integer to string
      char *state_str = "???";
      if(info.state >= 0 && info.state < 6 && states[info.state])
        state_str = states[info.state];

      // Print row [cite: 40]
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