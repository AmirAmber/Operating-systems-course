#ifndef PROCESSINFO_H
#define PROCESSINFO_H

struct processInfo {
    int state;      // process state
    int ppid;       // parent PID
    int sz;         // size of process memory, in bytes
    int nfd;        // number of open file descriptors
    int nrswitch;   // number context switches in
};

#endif