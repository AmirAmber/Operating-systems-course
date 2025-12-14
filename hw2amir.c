#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <errno.h>
#include <semaphore.h>
#include <stdint.h>   // uint64_t instead of long long
#include <ctype.h>    // isspace(), parsing helpers
#include <assert.h>   // debugging sanity checks

#define MAX_THREADS 4096
#define MAX_COUNTERS 100
#define MAX_LINE_LENGTH 1024
#define LOG_ENABLE 1
#define LOG_DISABLE 0
#define COUNTER_FILE_NAME 16
// --- GLOBAL SYNCHRONIZATION & STATS (Moved from main/Added) ---
pthread_mutex_t queue_mutex;
pthread_cond_t queue_not_empty;
pthread_cond_t all_jobs_finished;

// Statistics variables
long long start_time_global;
long long sum_turnaround = 0;
long long min_turnaround = -1; // -1 indicates not set
long long max_turnaround = 0;
long long total_jobs_done = 0;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

int global_log_mode = 0;
int pending_jobs = 0;       // Jobs in queue
int active_workers = 0;     // Workers currently processing
int shutdown_flag = 0;      // To tell workers to exit
// --- PROTOTYPES ---
long long getCurrentTimeMs();
void* worker_thread(void* arg);
void parsingCommandFile(FILE* cmdfile); // Fixed return type and signature
void enqueueJob(job_queue* queue, job* new_job);
job* dequeueJob(job_queue* queue);
void createWorkerThreads(int num_threads);
void queue_init();


// structs // 
typedef struct job_t{
    char command[MAX_LINE_LENGTH];
    long long read_time_ms;
    struct job_t* next;
}job;

typedef struct job_queue_t{
    job* head;
    job* tail;
    int size;
} job_queue;
// Global Queue
job_queue* work_queue;

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
    
    // must protect file access if multiple threads hit the same file, 
    // use a mutex per file
    
    FILE* f = fopen(filename, "r+");
    if (!f) return;

    long long current_val = 0;
    if (fscanf(f, "%lld", &current_val) != 1) current_val = 0;
    
    current_val += val;

    rewind(f); // Go back to start
    fprintf(f, "%lld\n", current_val);
    
    // Truncate rest of file to ensure no artifacts remain if number gets shorter
    ftruncate(fileno(f), ftell(f)); 
    
    fclose(f);
}
// --- WORKER LOGIC  ---

// Helper to execute the specific semicolon separated commands
void execute_worker_line(char* line) {
    char* context_save = NULL;
    char* cmd_token = strtok_r(line, ";", &context_save); // thread-safe strtok
    
    while (cmd_token != NULL) {
        // Trim spaces
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
            // The logic requires repeating the *rest* of the line.
            // Because strtok modifies the string, we need the original pointer logic or recursion.
            // Simplified: The spec says repeat is the only one, and it repeats everything AFTER it.
            // Since we are tokenizing, the 'next' calls to strtok_r give us the rest.
            // We need to store the remaining tokens and run them loop times.
            
            // This is tricky with strtok. Strategy: Stop tokenizing, take the rest of string.
            char* rest = NULL;
             // Recover the rest of the string from where strtok left off
             if (context_save && *context_save != '\0') {
                 rest = context_save; // This points to the start of the next token
            }
            
            if (rest) {
                // Make a copy because we will parse it multiple times
                char* sequence = strdup(rest);
                for(int i=0; i < reps; i++) {
                    char* seq_copy = strdup(sequence);
                    execute_worker_line(seq_copy);
                    free(seq_copy);
                }
                free(sequence);
            }
             break; // Repeat handles the rest, we stop this loop
        }
        
        cmd_token = strtok_r(NULL, ";", &context_save);
    }
}
void* worker_thread(void* arg) {
    int id = *(int*)arg;
    char log_file[32];
    snprintf(log_file, sizeof(log_file), "thread%02d.txt", id);
    
    // Create log file if enabled
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
            
            // Parse and Execute
            // We need a copy because strtok destroys it
            char* line_copy = strdup(j->command);
            
            // Skip the word "worker"
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
        // If queue is empty and no one is working, signal dispatcher
        if (work_queue->size == 0 && active_workers == 0) {
            pthread_cond_signal(&all_jobs_finished);
        }
        pthread_mutex_unlock(&queue_mutex);
    }
    return NULL;
}

int dispatcher(FILE* cmdfile, int num_threads, int num_counters, int log_mode){
    // check for argument validity
    if (num_threads < 0 || num_threads > MAX_THREADS) {
        fprintf(stderr, "Error: Number of threads must be between 0 and %d\n", MAX_THREADS);
        return -1;
    }
    if (num_counters < 0 || num_counters > MAX_COUNTERS) {
        fprintf(stderr, "Error: Number of counters must be between 0 and %d\n", MAX_COUNTERS);
        return -1;
    }
    if (log_mode != LOG_ENABLE && log_mode != LOG_DISABLE) {
        fprintf(stderr, "Error: Log mode must be either %d (enable) or %d (disable)\n", LOG_ENABLE, LOG_DISABLE);
        return -1;
    }
    if (cmdfile == NULL) {
        fprintf(stderr, "Error: Command file is NULL\n");
        return -1;
    }
    // Initialize logging
    if (log_mode) {
        FILE* f = fopen("dispatcher.txt", "w");
        if(f) fclose(f);
    }
    // creating counter files
    for (int i=0; i<num_counters; i++) {
        char filename[counter_FILE_NAME];
        if(i >= 0 && i<=9){    
            snprintf(filename, sizeof(filename), "count%02d.txt", i);// for padding leading zero
        } else {
            snprintf(filename, sizeof(filename), "count%d.txt", i);
        }
        FILE* filename = fopen(filename, "w");
        if (filename == NULL) {
            fprintf(stderr, "Error: Could not create counter file %s: %s\n", filename, strerror(errno));
            return -1;
        }
        fprintf(filename,"%lld\n", 0);
        fclose(filename);        
    }
    // Initialize Queue
    queue_init();
    //creating worker threads
    createWorkerThreads(num_threads);
    // Parse the file and run dispatcher loop
    parsingCommandFile(cmdfile);

    // Shutdown sequence
    pthread_mutex_lock(&queue_mutex);
    shutdown_flag = 1;
    pthread_cond_broadcast(&queue_not_empty); // Wake up everyone to die
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

    return 0;
}



void createWorkerThreads(int num_threads){
    pthread_t* worker_thread_pool= (pthread_t)malloc(num_threads*sizeof(pthread_t));
    if(!worker_thread_pool){
        fprintf(stderr, "Error: Could not allocate memory for worker thread pool\n");
        return -1;
    }
    int* tid = (int)malloc(num_threads*sizeof(int));
    if(!tid){
        fprintf(stderr, "Error: Could not allocate memory for thread IDs\n");
        return -1;
    }
    for(int i= 0; i< num_threads; i++){
        tid[i] = i;
        if(pthread_create(&worker_thread_pool[i],NULL, worker_thread, &tid[i]) !=0){
            fprintf(stderr, "Error: Could not create worker thread %d: %s\n", i, strerror(errno));
            return -1;
        }
    }    
}

int parsingCommandFile(FILE* cmdfile){
    char* token;
    char line_buffer[MAX_LINE_LENGTH];
    job* new_job=NULL;
    while(!feof(cmdfile)){
        if(fgets(line_buffer, sizeof(line_buffer), cmdfile)==NULL){
            printf("End of file reached/n");
            break; //EOF reached
        }
    //remove leading and trailing whitespace
    char* start = line_buffer;
    char* cleanLine;
    while(isspace((unsigned char)*start)) start++;
    char* end = start + strlen(start) - 1;
    while(end > start && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    cleanLine = start;
    // Logging
    write_log("dispatcher.txt", "TIME %lld: read cmd line: %s\n", getCurrentTimeMs(), cleanLine);
    // We need a copy for tokenizing because strtok destroys the string
        char parsing_copy[MAX_LINE_LENGTH];
        strcpy(parsing_copy, cleanLine);

        token = strtok(parsing_copy, " ;"); // tokenize by space and semicolon
        
        if(token == NULL) continue;

        if (strcmp(token, "worker") == 0) {
            // WORKER COMMAND
            new_job = (job*)malloc(sizeof(job));
            if (!new_job) {
                fprintf(stderr, "Error: Could not allocate memory for new job\n");
                return;
            }
            new_job->next = NULL;
            strcpy(new_job->command, cleanLine); // Keep full original command
            new_job->read_time_ms = getCurrentTimeMs();

            pthread_mutex_lock(&queue_mutex);
            enqueueJob(work_queue, new_job);
            pending_jobs++;
            pthread_cond_signal(&queue_not_empty);
            pthread_mutex_unlock(&queue_mutex);

        } else {
            // DISPATCHER COMMANDS
            if (strcmp(token, "dispatcher_msleep") == 0) {
                token = strtok(NULL, " ;");
                if (token == NULL) {
                    fprintf(stderr, "Error: Missing argument for dispatcher_msleep command\n");
                    continue;
                }
                int sleep_time = atoi(token);
                usleep(sleep_time * 1000); // convert to microseconds
                continue;
            }
            else if (strcmp(token, "dispatcher_wait") == 0) {
                // wait until all jobs are finished
                pthread_mutex_lock(&queue_mutex);
                while (work_queue->size > 0 || active_workers > 0) {
                    pthread_cond_wait(&all_jobs_finished, &queue_mutex);
                }
                pthread_mutex_unlock(&queue_mutex);
                continue;
            }
            else {
               // Unknown command, ignore or print error
            }
        }
    }
}


void queue_init(){
    job_queue* queue =(job_queue*)malloc (sizeof (job_queue));
    if(!queue){
        fprintf (stderr, "Error: Could not allocate memory for job queue\n");
        exit (EXIT_FAILURE);
    }
    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
    return queue;
}

void enqueueJob(job_queue* queue, job* new_job){
    //used by dispatcher to add new jobs to the queue
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
    //used by worker thread to get job from queue
    job* dequeued_job;
    if(queue->size==0){
        printf("Queue is empty, thread is going to sleep/n");
        return NULL;
    }
    dequeued_job= queue->head;
    queue->head = queue->head->next;
    queue->size--;
    return dequeued_job;
}

//** MAIN  **//
int main(int argc, char* argv[]) {
    // This is 5 items total (argv[0] to argv[4])
    if (argc != 5) {
        printf("ERROR: command line aruments need to be exactly 4\n");
        return EXIT_FAILURE;
    }
    start_time_global = getCurrentTimeMs();
    // parsing command line arguments
    FILE* cmdfile = fopen(argv[1], "r");
    if (cmdfile == NULL) {
        fprintf(stderr, "Error: Could not open command file %s: %s\n", argv[1], strerror(errno));
        return EXIT_FAILURE;
    }
    int num_threads = atoi(argv[2]);
    int num_counters = atoi(argv[3]);
    int log_mode = atoi(argv[4]);
    global_log_mode = log_mode;
    
    // Initialization
    pthread_mutex_init(&queue_mutex, NULL);
    pthread_cond_init(&queue_not_empty, NULL);
    pthread_cond_init(&all_jobs_finished, NULL);
    
    // dispatcher operation
    

    dispatcher(cmdfile, num_threads, num_counters, log_mode);
    // Cleanup (Optional for HW but good practice)
    fclose(cmdfile);

    return 0;
}
    