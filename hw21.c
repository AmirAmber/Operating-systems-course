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

long long getCurrentTimeMs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// structs // 
typedef struct job_t{
    char command[MAX_LINE_LENGTH];
    long long read_time_ms
    struct job* next;
}job;

typedef struct job_queue_t{
    job* head;
    job* tail;
    int size;
} job_queue;


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
    // creating counter files
    for (int i=0; i<num_counters; i++) {
        char filename[counter_FILE_NAME];
        if(i >= 0 && i<=9){    
            snprintf(filename, sizeof(filename), "count%02d.txt", i);
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
    //creating worker threads
    createWorkerThreads(num_threads);


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
    char* token
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
    new_job = (job*)malloc(sizeof(job));
    if(!new_job){
        fprintf(stderr, "Error: Could not allocate memory for new job\n");
        return -1;
    }
    new_job->next = NULL;
    strcpy(new_job->command, cleanLine);
    new_job->read_time_ms = getCurrentTimeMs(); 

    token = strtok(cleanLine, "  ;"); //tokenize by space and semicolon
    if(strcomp(token, "worker")!= 0){ //dispatcher commands
        //executing dispatcher commands
        if(strcomp(token,"dispatcher_msleep") == 0){
            token = strtok(NULL, " ;");
            if(token == NULL){
                fprintf(stderr, "Error: Missing argument for dispatcher_msleep command\n");
                free(new_job);
                continue;
            }
            int sleep_time = atoi(token);
            usleep(sleep_time * 1000); //convert to microseconds
            free(new_job);
            continue;
        }
        else if(strcomp(token,"dispather_wait") == 0){
            //wait until all jobs are finished
            pthread_mutex_lock(&queue_mutex);
            while(pending_jobs > 0 || active_workers > 0){
                pthread_cond_wait(&all_jobs_finished, &queue_mutex);
            }
            pthread_mutex_unlock(&queue_mutex);
            free(new_job);
            continue;
        } else {
            fprintf(stderr, "Error: Unknown dispatcher command: %s\n", token);
            free(new_job);
            continue;
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
    if (argc != 4) {
        printf("ERROR: command line aruments need to be exactly 4\n");
        return EXIT_FAILURE;
    }
    // parsing command line arguments
    FILE* cmdfile = fopen(argv[1], "r");
    if (cmdfile == NULL) {
        fprintf(stderr, "Error: Could not open command file %s: %s\n", argv[1], strerror(errno));
        return EXIT_FAILURE;
    }
    int num_threads = atoi(argv[2]);
    int num_counters = atoi(argv[3]);
    int log_mode = atoi(argv[4]);

    // control variables
    int pending_jobs = 0;
    int active_workers = 0;
    int shutdown_flag = 0;

    //queue synchronization
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_not_empty;

    // dispatcher operation
    pthread_cond_t all_jobs_finished;

    pthread_mutex_init(&queue_mutex, NULL);
    pthread_cond_init(&queue_not_empty, NULL);
    pthread_cond_init(&all_jobs_finished, NULL);

    dispatcher(cmdfile, num_threads, num_counters, log_mode);


    return 0;
}
    