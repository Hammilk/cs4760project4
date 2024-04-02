/*
 *
 *Project Title: Project 4 - Multilevel Feedback Queues
 *Author: David Pham
 * 3/7/2024
 *
 */

#define _POSIX_C_SOURCE 199309L


#include<stdio.h>
#include<sys/types.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/wait.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<math.h>
#include<signal.h>
#include<sys/time.h>
#include<getopt.h>
#include<string.h>
#include<sys/msg.h>
#include<errno.h>

#define SHMKEY1 2031535
#define SHMKEY2 2031536
#define SHMKEY3 2031537
#define BUFF_SZ sizeof (int)
#define MAXDIGITS 3
#define PERMS 0644

struct QNode{
    int key;
    struct QNode* next;
};

struct Queue{
    struct QNode *front, *rear;
};

//Create new node function
struct QNode* newNode(int k){
    struct QNode* temp = (struct QNode*)malloc(sizeof(struct QNode));
    temp->key = k;
    temp->next = NULL;
    return temp;
}

//Create Queue function
struct Queue* createQueue(){
    struct Queue* q = (struct Queue*)malloc(sizeof(struct Queue));
    q->front = q->rear = NULL;
    return q;
}

//The function to add a key k to q
void enQueue(struct Queue* q, int k){
    struct QNode* temp = newNode(k);

    if(q->rear == NULL){
        q->front = q->rear = temp;
        return;
    }

    q->rear->next = temp;
    q->rear = temp;
}

//Function to remove a key from given queue q
void deQueue(struct Queue* q)
{
    if(q->front == NULL){
        return;
    }

    struct QNode* temp = q->front;

    q->front = q->front->next;

    if(q->front == NULL){
        q->rear = NULL;
    }
    free(temp);
}

struct PCB{
    int occupied; //Either true or false
    pid_t pid; //process id of child
    int startSeconds; //time when it was forked
    int startNano; //time when it was forked
    int blocked;
    int eventBlockedUntilSec;
    int eventBlockedUntilNano;
};

int *sharedSeconds;
int *sharedNano;
int shmidSeconds;
int shmidNano;


struct PCB processTable[20];

int msqid;

static void myhandler(int s){
    printf("Got signal, terminated\n");
    for(int i = 0; i < 20; i++){
        if(processTable[i].occupied == 1){
            kill(processTable[i].pid, SIGTERM);
        }
    }
    
    if(msgctl(msqid, IPC_RMID, NULL) == -1){
        perror("msgctl to get rid of queue in parent failed");
        exit(1);
    }

    shmdt(sharedSeconds);
    shmdt(sharedNano);
    shmctl(shmidSeconds, IPC_RMID, NULL); 
    shmctl(shmidNano, IPC_RMID, NULL);
    exit(1);
}

static int setupinterrupt(void){
    struct sigaction act;
    act.sa_handler = myhandler;
    act.sa_flags = 0;
    return(sigemptyset(&act.sa_mask) || sigaction(SIGINT, &act, NULL) || sigaction(SIGPROF, &act, NULL));
}

static int setupitimer(void){
    struct itimerval value;
    value.it_interval.tv_sec = 5;
    value.it_interval.tv_usec = 0;
    value.it_value = value.it_interval;
    return (setitimer(ITIMER_PROF, &value, NULL));
}

typedef struct msgbuffer {
    long mtype;
    int intData;
    int quanta;
} msgbuffer;

typedef struct{
    int proc;
    int simul;
    int timelimit;
    int interval;
    char logfile[20];
} options_t;


void print_usage(const char * app){
    fprintf(stderr, "usage: %s [-h] [-n proc] [-s simul] [-t timeLimitForChildren] [-i intervalInMsToLaunchChildren] [-f logfile]\n", app);
    fprintf(stderr, "   proc is the total amount of children.\n");
    fprintf(stderr, "   simul is how many children can run simultaneously.\n");
    fprintf(stderr, "   timeLimitForChildren is the bound of time that a child process should be launched for.\n");
    fprintf(stderr, "   intervalInMsToLaunchChildren specifies how often you should launch a child.\n");
    fprintf(stderr, "   logfile is the input for the name of the logfile for oss to write into.\n");
}

void printProcessTable(int PID, int SysClockS, int SysClockNano, struct PCB processTable[20]){
    printf("OSS PID %d SysClockS: %d SysClockNano: %d\n", PID, SysClockS, SysClockNano);
    printf("Process Table:\n");
    printf("Entry     Occupied  PID       StartS    Startn\n"); 
    for(int i = 0; i<20; i++){
        if((processTable[i].occupied) == 1){
            printf("%d         %d         %d         %d         %d\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano);
        }
        
    } 
}

void fprintProcessTable(int PID, int SysClockS, int SysClockNano, struct PCB processTable[20], FILE *fptr){
    fprintf(fptr, "OSS PID %d SysClockS: %d SysClockNano: %d\n", PID, SysClockS, SysClockNano);
    fprintf(fptr, "Process Table:\n");
    fprintf(fptr, "Entry     Occupied  PID       StartS    Startn\n"); 
    for(int i = 0; i<20; i++){
        if((processTable[i].occupied) == 1){
            fprintf(fptr, "%d         %d         %d         %d         %d\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano);
        }
        
    } 
}

void incrementClock(int *seconds, int *nano, int increment){
    (*nano) += increment;
    if((*nano) >= (pow(10, 9))){
         (*nano) -= (pow(10, 9));
         (*seconds)++;
    }
}

int nextChild(struct Queue* q0, struct Queue* q1, struct Queue* q2){
    if((q0->front != NULL)){
        return 0;
    }    
    else if((q1->front != NULL)){
        return 1;
    }

    else if((q2->front != NULL)){
        return 2;
    }
    else return -1;
}

int nextChildTest(struct Queue* q0){
    int returnValue;
    if((q0->front != NULL)){
        returnValue = (q0->front)->key;
        return returnValue;
    }
    else return -1;
}

static int randomize_helper(FILE *in){
    unsigned int seed;
    if(!in) return -1;
    if(fread(&seed, sizeof seed, 1, in) == 1){
        fclose(in);
        srand(seed);
        return 0;
    }
    fclose(in);
    return -1;
}

static int randomize(void){
    if(!randomize_helper(fopen("/dev/urandom", "r"))) return 0;
    if(!randomize_helper(fopen("/dev/arandom", "r"))) return 0;
    if(!randomize_helper(fopen("/dev/random", "r"))) return 0;
    return -1;
}

    



int main(int argc, char* argv[]){

    //Seed random
    if(randomize()){
        fprintf(stderr, "Warning: No source for randomness.\n");
    }

    //Set up shared memory
    shmidSeconds = shmget(SHMKEY1, BUFF_SZ, 0666 | IPC_CREAT);
    if(shmidSeconds == -1){
        fprintf(stderr, "error in shmget 1.0\n");
        exit(1);
    }
    sharedSeconds = shmat(shmidSeconds, 0, 0);
    
    //Attach shared memory to nano
    shmidNano = shmget(SHMKEY2, BUFF_SZ, 0777 | IPC_CREAT);
    if(shmidNano == -1){
        fprintf(stderr, "error in shmget 2.0\n");
        exit(1);
    }
    sharedNano=shmat(shmidNano, 0, 0);

    //Set up structs defaults
   
    for(int i = 0; i < 20; i++){
            processTable[i].occupied = 0;
            processTable[i].pid = 0;
            processTable[i].startSeconds = 0;
            processTable[i].startNano = 0;
            processTable[i].blocked = 0;
            processTable[i].eventBlockedUntilSec = 0;
            processTable[i].eventBlockedUntilNano = 0;
    }
    
    options_t options;
    options.proc = 2; //n
    options.simul = 2; //s
    options.timelimit = 50000000; //t
    options.interval = 1; //i
    strcpy(options.logfile, "msgq.txt"); //f

    //Set up user input

    const char optstr[] = "hn:s:t:i:f:";

    char opt;
    while((opt = getopt(argc, argv, optstr))!= -1){
        switch(opt){
            case 'h':
                print_usage(argv[0]);
                return(EXIT_SUCCESS);
            case 'n':
                options.proc = atoi(optarg);
                break;
            case 's':
                options.simul = atoi(optarg);
                break;
            case 't':
                options.timelimit = atoi(optarg);
                break;
            case 'i':
                options.interval = atoi(optarg);
                break;
            case 'f':
                strcpy(options.logfile, optarg);
                break;
            default:
                printf("Invalid options %c\n", optopt);
                print_usage(argv[0]);
                return(EXIT_FAILURE);
        }
    }
   
    //Set up variables;
    pid_t pid;
     
    int seconds = 0;
    int nano = 0;
    *sharedSeconds = seconds;
    *sharedNano = nano;

    //Variables for message queue
    key_t key;
    msgbuffer buff;
    buff.mtype = 1;
    buff.quanta = 0;


    //Set up timers
    if(setupinterrupt() == -1){
        perror("Failed to set up handler for SIGPROF");
        return 1;
    }
    if(setupitimer() == -1){
        perror("Failed to set up the ITIMER_PROF interval timer");
        return 1;
    }

    //Set up file
    char commandString[20];
    strcpy(commandString, "touch "); 
    strcat(commandString, options.logfile);
    system(commandString);

    FILE *fptr;
    fptr = fopen(options.logfile, "w");
   

    if(fptr == NULL){
        fprintf(stderr, "Error: file has not opened.\n");
        exit(0);
    }


    //get a key for message queue
    if((key = ftok("oss.c", 1)) == -1){
        perror("ftok");
        exit(1);
    }

    //create our message queue
    if((msqid = msgget(key, PERMS | IPC_CREAT)) == -1){
        perror("msgget in parent");
        exit(1);
    }

    //Variables
    int childrenLaunched = 0; 
    int simulCount = 0;
    int childrenFinishedCount = 0;
    int currentChild = 0;
    int currentQueue = 0;
    int nextIntervalSeconds;
    int nextIntervalNano;
    int launchFlag = 0;
    int terminationPercent = 2;
    int blockPercent = 2;
    int timeSlice = 0;

    //Statistic Variable
    double idle = 0;
    double waitTime = 0;
    double blockTime = 0;
    
    struct Queue* q0 = createQueue(); //Highest priority queue (10ms)
    struct Queue* q1 = createQueue(); // (20 ms)
    struct Queue* q2 = createQueue(); //Lowest priority queue (40 ms)
    struct Queue* blockQueue = createQueue(); //Handles blocked processes


    while(childrenFinishedCount < options.proc){
        
        //Calculate Next Child
        if(simulCount > 0){ //Skips running if no child has been launched
            
            //Check for block queue
            if((blockQueue -> front) != NULL && simulCount < options.simul){ //Will not add if simultaneous limit is reached
                int key = (blockQueue -> front) -> key;
                if(((*sharedSeconds > processTable[key].eventBlockedUntilSec) || (*sharedSeconds == processTable[key].eventBlockedUntilSec)) && 
                        (*sharedNano > processTable[key].eventBlockedUntilNano)){
                deQueue(blockQueue);
                enQueue(q0, key);
                incrementClock(sharedSeconds, sharedNano, 10000); //Increment for moving from block queue to ready queue
                fprintf(fptr, "OSS: Process with PID %d was unblocked and placed into the ready queue at time %d:%d\n", processTable[key].pid, *sharedSeconds, *sharedNano);
                simulCount++;

                //STATISTICS: Block time
                blockTime += ((*sharedSeconds * pow(10, 9)) + (*sharedNano)) - 
                    ((processTable[key].eventBlockedUntilSec * pow(10, 9) + processTable[key].eventBlockedUntilNano) - pow(10,9));     

                }

            } 
            
            //Increment for calculating next child
            incrementClock(sharedSeconds, sharedNano, 5000); //Increment for scheduling decision
            currentQueue = nextChild(q0, q1, q2);
            
            if(currentQueue == 0){
                currentChild = (q0->front)->key;
            }
            else if(currentQueue == 1){
                currentChild = (q1->front)->key;
            }
            else if(currentQueue == 2){
                currentChild = (q2->front)->key;
            }
            else{
                printf("queue code %d\n", currentQueue);
                perror("queue failed");
                exit(1);
            }

                                    
        }
        //Increments clock by 1000 ns if no children are launched
        else{
            incrementClock(sharedSeconds, sharedNano, 5*pow(10, 8));
            //STATISTICS: Idle Time
            idle += 5*pow(10,8);
        }

        if(currentChild >= 0){
            printf("Sending message to child %d\n", currentChild);
        }
        printf("Current Child: %d\n", currentChild);
        //Message Handling
        if(simulCount > 0){
            buff.mtype = processTable[currentChild].pid;
            buff.intData = processTable[currentChild].pid;
            if(currentQueue == 0) timeSlice = pow(10, 6);
            else if(currentQueue == 1) timeSlice = 2 * pow(10, 6);
            else if(currentQueue == 2) timeSlice = 4 * pow(10, 6);
            buff.quanta = timeSlice;
            
            fprintf(fptr, "OSS: Dispatching process with PID %d from queue %d at time %d:%d\n", processTable[currentChild].pid, currentQueue, *sharedSeconds, *sharedNano); 
            //Message Sent
            if(msgsnd(msqid, &buff, sizeof(msgbuffer)-sizeof(long), 0) == -1){
                perror("msgsnd to child failed\n");
                exit(1);
            }
            
            if(currentQueue == 0){
                deQueue(q0);
            }               

            
            else if(currentQueue == 1){
                deQueue(q1);
            }


            else if(currentQueue == 2){
               deQueue(q2);
            }

            

            
            //Message Received (Blocking)  
            if(msgrcv(msqid, &buff, sizeof(msgbuffer), getpid(), 0) == -1){
                perror("failed to receive message in parent\n");
                exit(1);
            }
            fprintf(fptr, "OSS: Receiving that process with PID %d ran for %d nanoseconds\n", processTable[currentChild].pid, abs(buff.quanta));
            //STATISTICS: Wait Time
            if(simulCount > 1){
                waitTime += abs(buff.quanta);
            }

            //Increment Clock by Amount Used by Child
            incrementClock(sharedSeconds, sharedNano, abs(buff.quanta));

            printf("Received message from child %d\n", currentChild);
        }
        
        //If child terminates
        if(buff.quanta < 0){
            printf("Child %d has decided to terminate\n", currentChild);
            fprintf(fptr, "OSS: Received that process with PID %d terminated at time %d:%d\n", processTable[currentChild].pid, *sharedSeconds, *sharedNano);
            wait(0);
            processTable[currentChild].pid = 0;
            processTable[currentChild].occupied = 0;
            processTable[currentChild].startSeconds = 0;
            processTable[currentChild].startNano = 0;
            processTable[currentChild].eventBlockedUntilNano = 0;
            processTable[currentChild].eventBlockedUntilSec = 0;
            
            buff.mtype = 0;
            buff.intData = 0; 
            buff.quanta = 0;
            simulCount--;
            childrenFinishedCount++;
        }
        //If child is blocked
        else if(buff.quanta > 0 && buff.quanta < timeSlice){
            simulCount--;
            enQueue(blockQueue, currentChild);
            processTable[currentChild].blocked = 1;
            processTable[currentChild].eventBlockedUntilNano = (*sharedNano); //Block runs for 1000 ms
            processTable[currentChild].eventBlockedUntilSec++;
            fprintf(fptr, "OSS: Recieved that process with PID %d blocked until time %d:%d at time %d:%d\n", processTable[currentChild].pid,
                   processTable[currentChild].eventBlockedUntilSec, processTable[currentChild].eventBlockedUntilNano, *sharedSeconds, *sharedNano);            
            buff.mtype = 0;
            buff.intData = 0; 
            buff.quanta = 0;
           
            
        }

        else if(simulCount > 0){
            if(currentQueue == 0){
                enQueue(q1, currentChild);
                fprintf(fptr, "OSS: Process with PID %d moved from Queue 0 to Queue 1 at time %d:%d\n", processTable[currentChild].pid, *sharedSeconds, *sharedNano);
            }

            
           else if(currentQueue == 1){
               enQueue(q2, currentChild);
               fprintf(fptr, "OSS: Process with PID %d moved from Queue 1 to Queue 2 at time %d:%d\n", processTable[currentChild].pid, *sharedSeconds, *sharedNano);          
           }

           else if(currentQueue == 2){
               enQueue(q2, currentChild);
               fprintf(fptr, "OSS: Process with PID %d requeued to Queue 2 at time %d:%d\n", processTable[currentChild].pid, *sharedSeconds, *sharedNano); 
           }

        }
        
       

        //Launch Children
        if(launchFlag == 0 && childrenLaunched < options.proc && simulCount < options.simul && (((*sharedSeconds) > nextIntervalSeconds || 
                ((*sharedSeconds) == nextIntervalSeconds && (*sharedNano) > nextIntervalNano)))){
            simulCount++;
            launchFlag++;
            nextIntervalSeconds = nextIntervalSeconds + (rand() % (options.interval + 1));
            nextIntervalNano = rand() % (int) pow(10, 9);
            childrenLaunched++;
            pid = fork();
        }

        //Launch Executables
        if(pid == 0){

            //Generates a random bounded time limit for child

           
            char childTimeLimit[MAXDIGITS];
            sprintf(childTimeLimit, "%d", options.timelimit);
            char terminateChance[MAXDIGITS];
            sprintf(terminateChance, "%d", terminationPercent);
            char blockChance[MAXDIGITS];
            sprintf(blockChance, "%d", blockPercent);



            char * args[] = {"./worker", terminateChance, blockChance, childTimeLimit};

            //Run Executable
            execlp(args[0], args[0], args[1], args[2], args[3], NULL);
            printf("Exec failed\n");
            exit(1);
        }

        else if (pid > 0 && launchFlag > 0){
            printf("OSS: Generating process with PID %d and putting it in queue 0 at time %d:%d\n", pid, *sharedSeconds, *sharedNano);
            
            fprintf(fptr, "OSS: Generating process with PID %d and putting it in queue 0 at time %d:%d\n", pid, *sharedSeconds, *sharedNano);
            
            launchFlag = 0;       
            //Insert child into PCB
            int index = 0;
            int arrayInserted = 0;
            while(!arrayInserted){
                
                if(processTable[index].occupied == 1){
                    index++;
                }
                else if(processTable[index].occupied == 0){
                    arrayInserted = 1;
                    processTable[index].occupied = 1;
                    processTable[index].pid = pid;
                    processTable[index].startSeconds = *sharedSeconds;
                    processTable[index].startNano = *sharedNano;
                }
                else{
                    printf("ERROR PCB Fail\n");
                    exit(1);
                }
            }

            //Queue child
            enQueue(q0, index);

            printProcessTable(getpid(), *sharedSeconds, *sharedNano, processTable);
            
        }
    }
    //STATISTICS: Final Report
    //Average wait time
    double averageWaitTime = waitTime / options.proc;

    //Average CPU Utilization
    double cpuUtilization  = idle / ((*sharedSeconds) * pow(10,9) + (*sharedNano));

    //Average block time 
    double averageBlockTime = blockTime / options.proc;   

    printf("Average Wait Time for %d processes: %f nanoseconds\n", options.proc, averageWaitTime);
    printf("CPU Utilization: %f\n", cpuUtilization);
    printf("Average Block Time for %d processes: %f nanoseconds\n", options.proc, averageBlockTime);

    //Remove message queues 
    if(msgctl(msqid, IPC_RMID, NULL) == -1){
        perror("msgctl to get rid of queue in parent failed");
        exit(1);
    }
    
    
    //Remove shared memory
    shmdt(sharedSeconds);
    shmdt(sharedNano);
    shmctl(shmidSeconds, IPC_RMID, NULL);
    shmctl(shmidNano, IPC_RMID, NULL);
    

    //Close file
    fclose(fptr);
    return 0;
    
}




