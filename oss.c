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
};

int *sharedSeconds;
int *sharedNano;
int shmidSeconds;
int shmidNano;


struct PCB processTable[20];

static void myhandler(int s){
    printf("Got signal, terminated\n");
    for(int i = 0; i < 20; i++){
        if(processTable[i].occupied == 1){
            kill(processTable[i].pid, SIGTERM);
        }
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
    char strData[10];
    int intData;
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


void incrementClock(int *seconds, int *nano){
    (*nano) += 10000;
    if((*nano) >= (pow(10, 9))){
         (*nano) -= (pow(10, 9));
         (*seconds)++;
    }
}

int nextChild(int currentChild, struct PCB processTable[20]){
    currentChild++;
    while(processTable[currentChild].occupied == 0){
        currentChild++;
        if(currentChild > 19){ //Resets count to 0 when upper bound is reached
            currentChild = 0;
        }
    }
    return currentChild;
}

int main(int argc, char* argv[]){
 
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
    }


    options_t options;
    options.proc = 1; //n
    options.simul = 1; //s
    options.timelimit = 1; //t
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
    int msqid;
    key_t key;
    msgbuffer buff;
    buff.mtype = 1;


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
    int childFinished = 0;
    int simulCount = 0;
    int launchFlag = 0;
    int childrenFinishedCount = 0;
    int currentChild = 0;
    int randSecondLimit;
    int randNanoLimit;

    while(childrenFinishedCount < options.proc){
       
        incrementClock(sharedSeconds, sharedNano);

        //Print Table
        
        if(*sharedNano % (int)(pow(10,9)/2) == 0 || *sharedNano == 0){ //WILL BREAK IF YOU CHANGE INCREMENTS
            printProcessTable(getpid(), *sharedSeconds, *sharedNano, processTable);
            fprintProcessTable(getpid(), *sharedSeconds, *sharedNano, processTable, fptr);
        }

        //Calculate Next child
        if(simulCount > 0){
            currentChild = nextChild(currentChild, processTable);
            
            //Send message to child
            
            buff.mtype = processTable[currentChild].pid;
            buff.intData = processTable[currentChild].pid;
            strcpy(buff.strData, "Sent");
            if(msgsnd(msqid, &buff, sizeof(msgbuffer)-sizeof(long), 0) == -1){
                perror("msgsnd to child failed\n");
                exit(1);
            }
           
            if(msgrcv(msqid, &buff, sizeof(msgbuffer), getpid(), 0) == -1){
                perror("failed to receive message in parent\n");
                exit(1);
            }
        }

        //Launch Children
        if(launchFlag == 0 && childrenLaunched < options.proc && simulCount < options.simul && (*sharedSeconds)%options.interval == 0){
            launchFlag = 1;
            simulCount++;
            childrenLaunched++;
            pid = fork();
        }

        //Launch Executables
        if(pid == 0){

            //Generates a random bounded time limit for child
            randSecondLimit = rand() % (options.timelimit);
            randNanoLimit = rand() % 1000000000;          

            char terminatedSeconds[MAXDIGITS];
            char terminatedNano[MAXDIGITS];
            sprintf(terminatedSeconds, "%d", randSecondLimit);
            sprintf(terminatedNano, "%d", randNanoLimit);
            char * args[] = {"./worker", terminatedSeconds, terminatedNano};

            //Run Executable
            execlp(args[0], args[0], args[1], args[2], NULL);
            printf("Exec failed\n");
            exit(1);
        }

        else if (pid > 0 && launchFlag>0){
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
            
        }
        else if (pid > 0){ 
            
            if(atoi(buff.strData) == 0){
                strcpy(buff.strData, "1"); //Clears buffer so loop doesn't go back into this section until a new message is sent
                childFinished = wait(0);
                simulCount--;
                
                //Delete child from PCB
                for(int i = 0; i < 20; i++){
                    if(processTable[i].pid == childFinished){
                        processTable[i].occupied = 0;
                        processTable[i].pid = 0;
                        processTable[i].startSeconds = 0;
                        processTable[i].startNano = 0;
                    }
                }
                childFinished = 0;
                childrenFinishedCount++;
                printf("Children Finished Count: %d\n", childrenFinishedCount);
            }
        }
    }
    
   
    //Close file
    fclose(fptr);

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

    return 0;
    
}




