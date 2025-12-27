#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <errno.h>
#include <semaphore.h>
#include <stdint.h>
#include <ctype.h>
#include <assert.h>

#define MAX_THREADS 4096
#define MAX_COUNTERS 100
#define MAX_LINE_LENGTH 1024
#define LOG_ENABLE 1
#define LOG_DISABLE 0
#define COUNTER_FILE_NAME 64  // Increased size to prevent compiler warnings

// --- 1. STRUCTS MOVED TO TOP (Fixes "unknown type name" error) ---
typedef struct job_t {
    char command[MAX_LINE_LENGTH];
    long long read_time_ms;
    struct job_t* next;
} job;

typedef struct job_queue_t {
    job* head;
    job* tail;
    int size;
} job_queue;

// --- GLOBAL SYNCHRONIZATION & STATS ---
pthread_mutex_t queue_mutex;
pthread_cond_t queue_not_empty;
pthread_cond_t all_jobs_finished;
// Added: Mutexes for file locking (Required for correct counting)
pthread_mutex_t file_mutexes[MAX_COUNTERS]; 

// Statistics variables
long long start_time_global;
long long sum_turnaround = 0;
long long min_turnaround = -1;
long long max_turnaround = 0;
long long total_jobs_done = 0;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

int global_log_mode = 0;
int pending_jobs = 0;
int active_workers = 0;
int shutdown_flag = 0;

// Global Queue
job_queue* work_queue;
// mem
pthread_t* worker_thread_pool;
int* tid;
// --- FUNCTION DECLARATIONS --- 

long long getCurrentTimeMs();
void* worker_thread(void* arg);
void parsingCommandFile(FILE* cmdfile);
void enqueueJob(job_queue* queue, job* new_job);
job* dequeueJob(job_queue* queue);
int createWorkerThreads(int num_threads); // FIXED: Returns int, not void
job_queue* queue_init();                  // FIXED: Returns pointer, not void

// --- HELPER FUNCTIONS ---
long long getCurrentTimeMs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void write_log(const char* filename, const char* format, long long time, const char* str_arg) {
    if (!global_log_mode) return;
    FILE* fp = fopen(filename, "a");
    if (fp) {
        fprintf(fp, format, time - start_time_global, str_arg);
        fclose(fp);
    }
}

void update_stats(long long turnaround) {
    pthread_mutex_lock(&stats_mutex);
    sum_turnaround += turnaround;
    if (min_turnaround == -1 || turnaround < min_turnaround) min_turnaround = turnaround;
    if (turnaround > max_turnaround) max_turnaround = turnaround;
    total_jobs_done++;
    pthread_mutex_unlock(&stats_mutex);
}

// --- FILE OPERATIONS ---
void modify_counter(int counter_id, int val) {
    char filename[COUNTER_FILE_NAME];
    snprintf(filename, sizeof(filename), "count%02d.txt", counter_id);
    
    // Lock specific counter mutex to prevent race conditions
    if(counter_id < MAX_COUNTERS) pthread_mutex_lock(&file_mutexes[counter_id]);

    FILE* f = fopen(filename, "r+");
    if (!f) {
        if(counter_id < MAX_COUNTERS) pthread_mutex_unlock(&file_mutexes[counter_id]);
        return;
    }

    long long current_val = 0;
    if (fscanf(f, "%lld", &current_val) != 1) current_val = 0;
    
    current_val += val;

    rewind(f);
    fprintf(f, "%lld\n", current_val);
    ftruncate(fileno(f), ftell(f)); 
    fclose(f);

    if(counter_id < MAX_COUNTERS) pthread_mutex_unlock(&file_mutexes[counter_id]);
}

// --- WORKER LOGIC ---
void execute_worker_line(char* line) {
    char* context_save = NULL;
    char* cmd_token = strtok_r(line, ";", &context_save);
    
    while (cmd_token != NULL) {
        while(isspace((unsigned char)*cmd_token)) cmd_token++;
        
        if (strncmp(cmd_token, "msleep", 6) == 0) {
            int ms = atoi(cmd_token + 6);
            usleep(ms * 1000);
        } else if (strncmp(cmd_token, "increment", 9) == 0) {
            int id = atoi(cmd_token + 9);
            modify_counter(id, 1);
        } else if (strncmp(cmd_token, "decrement", 9) == 0) {
            int id = atoi(cmd_token + 9);
            modify_counter(id, -1);
        } else if (strncmp(cmd_token, "repeat", 6) == 0) {
            int reps = atoi(cmd_token + 6);
            char* rest = NULL;
             if (context_save && *context_save != '\0') {
                 rest = context_save;
            }
            if (rest) {
                char* sequence = strdup(rest);
                for(int i=0; i < reps; i++) {
                    char* seq_copy = strdup(sequence);
                    execute_worker_line(seq_copy);
                    free(seq_copy);
                }
                free(sequence);
            }
             break;
        }
        cmd_token = strtok_r(NULL, ";", &context_save);
    }
}

void* worker_thread(void* arg) {
    int id = *(int*)arg;
    char log_file[32];
    snprintf(log_file, sizeof(log_file), "thread%02d.txt", id);
    
    if (global_log_mode) {
        FILE* f = fopen(log_file, "w");
        if(f) fclose(f);
    }

    while (1) {
        pthread_mutex_lock(&queue_mutex);
        
        while (work_queue->size == 0 && !shutdown_flag) {
            pthread_cond_wait(&queue_not_empty, &queue_mutex);
        }

        if (shutdown_flag && work_queue->size == 0) {
            pthread_mutex_unlock(&queue_mutex);
            break;
        }

        job* j = dequeueJob(work_queue);
        active_workers++; 
        pthread_mutex_unlock(&queue_mutex);

        if (j) {
            long long start_t = getCurrentTimeMs();
            write_log(log_file, "TIME %lld: START job %s\n", start_t, j->command);
            
            char* line_copy = strdup(j->command);
            char* commands_start = strstr(line_copy, "worker");
            if (commands_start) commands_start += 6; 
            else commands_start = line_copy;

            execute_worker_line(commands_start);
            free(line_copy);

            long long end_t = getCurrentTimeMs();
            write_log(log_file, "TIME %lld: END job %s\n", end_t, j->command);
            update_stats(end_t - j->read_time_ms);
            free(j);
        }

        pthread_mutex_lock(&queue_mutex);
        active_workers--;
        if (work_queue->size == 0 && active_workers == 0) {
            pthread_cond_signal(&all_jobs_finished);
        }
        pthread_mutex_unlock(&queue_mutex);
    }
    return NULL;
}

// --- QUEUE & THREAD CREATION ---

job_queue* queue_init(){ // FIXED: return type
    job_queue* queue = (job_queue*)malloc(sizeof(job_queue));
    if(!queue){
        fprintf(stderr, "Error: Could not allocate memory for job queue\n");
        exit(EXIT_FAILURE);
    }
    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
    return queue;
}

void enqueueJob(job_queue* queue, job* new_job){
    if(queue->size == 0){
        queue->head = new_job;
        queue->tail = new_job;
    } else {
        queue->tail->next = new_job;
        queue->tail = new_job;
    }
    queue->size++;
}

job* dequeueJob(job_queue* queue){
    job* dequeued_job;
    if(queue->size==0){
        return NULL;
    }
    dequeued_job = queue->head;
    queue->head = queue->head->next;
    queue->size--;
    return dequeued_job;
}

int createWorkerThreads(int num_threads){ // FIXED: return type
    // FIXED: Casting malloc to (pthread_t*) instead of (int)
    worker_thread_pool = (pthread_t*)malloc(num_threads * sizeof(pthread_t));
    if(!worker_thread_pool){
        fprintf(stderr, "Error: Could not allocate memory for worker thread pool\n");
        return -1;
    }
    // FIXED: Casting malloc to (int*)
    tid = (int*)malloc(num_threads * sizeof(int));
    if(!tid){
        fprintf(stderr, "Error: Could not allocate memory for thread IDs\n");
        return -1;
    }
    for(int i= 0; i< num_threads; i++){
        tid[i] = i;
        if(pthread_create(&worker_thread_pool[i], NULL, worker_thread, &tid[i]) != 0){
            fprintf(stderr, "Error: Could not create worker thread %d: %s\n", i, strerror(errno));
            return -1;
        }
    } 
    return 0;   
}

void parsingCommandFile(FILE* cmdfile){
    char* token;
    char line_buffer[MAX_LINE_LENGTH];
    job* new_job=NULL;
    
    while(fgets(line_buffer, sizeof(line_buffer), cmdfile) != NULL){
        // Remove whitespace logic...
        char* start = line_buffer;
        while(isspace((unsigned char)*start)) start++;
        if(*start == '\0') continue; // Empty line
        
        char* end = start + strlen(start) - 1;
        while(end > start && isspace((unsigned char)*end)) end--;
        *(end + 1) = '\0';
        char* cleanLine = start;

        write_log("dispatcher.txt", "TIME %lld: read cmd line: %s\n", getCurrentTimeMs(), cleanLine);

        char parsing_copy[MAX_LINE_LENGTH];
        strcpy(parsing_copy, cleanLine);

        token = strtok(parsing_copy, " ;"); 
        
        if(token == NULL) continue;

        if (strcmp(token, "worker") == 0) {
            new_job = (job*)malloc(sizeof(job));
            new_job->next = NULL;
            strcpy(new_job->command, cleanLine);
            new_job->read_time_ms = getCurrentTimeMs();

            pthread_mutex_lock(&queue_mutex);
            enqueueJob(work_queue, new_job);
            pending_jobs++;
            pthread_cond_signal(&queue_not_empty);
            pthread_mutex_unlock(&queue_mutex);

        } else if (strcmp(token, "dispatcher_msleep") == 0) {
            token = strtok(NULL, " ;");
            if (token) {
                usleep(atoi(token) * 1000);
            }
        } else if (strcmp(token, "dispatcher_wait") == 0) {
            pthread_mutex_lock(&queue_mutex);
            while (work_queue->size > 0 || active_workers > 0) {
                pthread_cond_wait(&all_jobs_finished, &queue_mutex);
            }
            pthread_mutex_unlock(&queue_mutex);
        }
    }
}

// --- DISPATCHER & MAIN ---

int dispatcher(FILE* cmdfile, int num_threads, int num_counters, int log_mode){
    if (log_mode) {
        FILE* f = fopen("dispatcher.txt", "w");
        if(f) fclose(f);
    }
    
    // Create counter files and Init Mutexes
    for (int i=0; i<num_counters; i++) {
        char filename[COUNTER_FILE_NAME];
        snprintf(filename, sizeof(filename), "count%02d.txt", i);
        
        // Init the mutex for this counter
        pthread_mutex_init(&file_mutexes[i], NULL);

        // FIXED: Renamed 'filename' to 'fptr' to avoid conflict
        FILE* fptr = fopen(filename, "w"); 
        if (fptr == NULL) {
            return -1;
        }
        fprintf(fptr,"%lld\n", 0LL);
        fclose(fptr);        
    }

    work_queue = queue_init(); 
    createWorkerThreads(num_threads);
    parsingCommandFile(cmdfile);

    // Shutdown
    pthread_mutex_lock(&queue_mutex);
    while (work_queue->size > 0 || active_workers > 0) {
        pthread_cond_wait(&all_jobs_finished, &queue_mutex);
    }
    shutdown_flag = 1;
    pthread_cond_broadcast(&queue_not_empty); 
    pthread_mutex_unlock(&queue_mutex);

    // Write stats
    FILE* statf = fopen("stats.txt", "w");
    if(statf) {
        long long total_run = getCurrentTimeMs() - start_time_global;
        double avg = (total_jobs_done > 0) ? (double)sum_turnaround / total_jobs_done : 0.0;
        
        fprintf(statf, "total running time: %lld milliseconds\n", total_run);
        fprintf(statf, "sum of jobs turnaround time: %lld milliseconds\n", sum_turnaround);
        fprintf(statf, "min job turnaround time: %lld milliseconds\n", (min_turnaround == -1 ? 0 : min_turnaround));
        fprintf(statf, "average job turnaround time: %f milliseconds\n", avg);
        fprintf(statf, "max job turnaround time: %lld milliseconds\n", max_turnaround);
        fclose(statf);
    }
    //added: Free allocated memory and destroy mutexes/conds
    // 1. Wait for all threads to actually finish (Join)
    for (int i = 0; i < num_threads; i++) {
        pthread_join(worker_thread_pool[i], NULL);
    }

    // 2. Free the arrays we allocated
    free(worker_thread_pool);
    free(tid);
    
    // 3. Free the queue struct itself
    free(work_queue);

    // 4. Destroy synchronization objects (Good practice)
    pthread_mutex_destroy(&queue_mutex);
    pthread_cond_destroy(&queue_not_empty);
    pthread_cond_destroy(&all_jobs_finished);
    for(int i = 0; i < num_counters; i++) {
        pthread_mutex_destroy(&file_mutexes[i]);
    }

    return 0;
}


int main(int argc, char* argv[]) {
    if (argc != 5) {
        printf("Usage: %s cmdfile num_threads num_counters log_enabled\n", argv[0]);
        return EXIT_FAILURE;
    }
    start_time_global = getCurrentTimeMs();
    
    FILE* cmdfile = fopen(argv[1], "r");
    if (cmdfile == NULL) {
        perror("Error opening cmdfile");
        return EXIT_FAILURE;
    }
    int num_threads = atoi(argv[2]);
    int num_counters = atoi(argv[3]);
    int log_mode = atoi(argv[4]);
    global_log_mode = log_mode;
    
    pthread_mutex_init(&queue_mutex, NULL);
    pthread_cond_init(&queue_not_empty, NULL);
    pthread_cond_init(&all_jobs_finished, NULL);
    
    dispatcher(cmdfile, num_threads, num_counters, log_mode);
    
    fclose(cmdfile);
    return 0;
}