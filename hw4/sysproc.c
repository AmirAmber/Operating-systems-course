#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "processInfo.h"

extern int getNumProc(void);
extern int getMaxPid(void);
extern int getProcInfo(int, struct processInfo*);

int sys_getNumProc(void) {
  return getNumProc();
}

int sys_getMaxPid(void){
  return getMaxPid();
}

int sys_getProcInfo(void){
  int pid;
  struct processInfo *pinfo; // Pointer to the struct in user space

  // Fetch the 1st argument (int pid) and check if the argument is valid;
  if(argint(0, &pid) < 0)
    return -1;
  // Fetch the 2nd argument (struct processInfo* pinfo) and check if the argument is valid
  if(argptr(1, (void*)&pinfo, sizeof(*pinfo)) < 0)
    return -1;

  // Now call the real function in proc.c(in user space). protect the kernel.
  return getProcInfo(pid, pinfo);
}

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}