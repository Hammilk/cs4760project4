#include<stdio.h>
#include<math.h>
#include<sys/shm.h>
#include<sys/ipc.h>
#include<unistd.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/msg.h>
#include<string.h>

#define SHMKEY1 2031535
#define SHMKEY2 2031536
#define SHMKEY3 2041537
#define BUFF_SZ sizeof(int)
#define PERMS 0644



static int randomize_helper(FINE *in){
    unsigned int seed;
    if(!in)
        return -1;
    if(fread(&seed, sizeof seed, 1, in) == 1){
        fclose(in);
        srand(seed);
        return 0;
    }

    fclose(in);
    return -1;
}

static int randomize(void){
    if(!randomize_helper(fopen("/dev/urandom", "r")))
        return 0;
    if(!randomize_helper(fopen("/dev/arandom", "r")))
        return 0;
    if(!randomize_helper(fopen("/dev/random", "r")))
        return 0;
    return -1;
}

typedef struct msgbuffer{
    long mtype;
    //char strData[10]; I dont think i actually need this
    int intData; //I'll send pid with this
    int quanta; //This is for time quanta
} msgbuffer;

int main(int argc, char** argv){
    
    //Defining constant for termination chance
    const int terminationChance = 5;
    const int blockChance = 5;

    //Create message buffers
    msgbuffer buff;
    buff.mtype = 1;
    int msqid = 0;
    key_t key;

    if((key = ftok("oss.c", 1)) == -1){
        perror("ftok");
        exit(1);
    }
    
    //Create message queue
    if((msqid = msgget(key, PERMS)) == -1){
        perror("msgget in child");
        exit(1);
    }


    //Set up shared memory pointer for struct
    int shm_id = shmget(SHMKEY1, BUFF_SZ, IPC_CREAT | 0666);
    if(shm_id <= 0){
        fprintf(stderr, "Shared memory get failed1\n");
        exit(1);
    }
    int* sharedSeconds = shmat(shm_id, 0, 0);

    //Set up shared memory pointer
    shm_id = shmget(SHMKEY2, BUFF_SZ, IPC_CREAT | 0666);
    if(shm_id <= 0){
        fprintf(stderr, "Shared memory get failed2\n");
        exit(1);
    }
    int* sharedNano = shmat(shm_id, 0, 0);
    

    //Set up some variables so I don't have to make a bunch of function calls
   
    int pid = getpid();
    int ppid = getppid();
    buff.mtype = ppid;
    buff.intData = ppid;
    
    
    //Parse out current SysClock and SysNano time
    int sysClockS = *sharedSeconds; //Starting seconds
    int sysClockNano = *sharedNano; //Starting nanoseconds
    int timeLimitSeconds = atoi(argv[1]) + sysClockS; //upper bound, passed from calling parent
    int timeLimitNano = atoi(argv[2]) + sysClockNano; 
    int timeElapsed;
    int timer = 0;

    //Initial print data
    //printf("WORKER PID: %d PPID: %d SysClockS: %d SysClockNano: %d TermTimeS: %d TermTimeNano: %d\n--Just Starting\n"
      //      , pid, ppid, *sharedSeconds, *sharedNano, timeLimitSeconds, timeLimitNano);
    
    //Receive message...message queue controlling iterations of loops
    if(msgrcv(msqid, &buff, sizeof(msgbuffer), getpid(), 0) == -1){
        perror("Failed to receive message\n");
        exit(1);
    }
    //Seed random
    if(randomize()){
        fprintf(stderr, "Warning: No sources for randomness.\n");
    }

    //Calculate termination chance

    if(rand() % 101 <= terminationChance){
        int quantaPercentage = rand()%101;
        buff.quanta = -(buff.quanta*(quantaPercentage/100);
    }

    //Calculate block chance
        
    else if(rand() % 101 <= blockChance){
        int quantaPercentage = rand()%101;
        buff.quanta = buff.quanta*(quantaPercentage/100);
    }

    //Calculate if process used entire time allotted
    else{
        int quantaPercentage = 100;
        buff.quanta = buff.quanta; 
    }
                
    //Send message back to parent
    buff.mtype = ppid; //indicates who to send it to
    buff.intData = pid;  

    if(msgsnd(msqid, &buff, sizeof(msgbuffer)-sizeof(long), 0) == -1){
        perror("msgsnd to parent failed\n");
        exit(1);
    }

    /* 
    while(timeLimitSeconds > (*sharedSeconds) || ((timeLimitSeconds == (*sharedSeconds)) && (timeLimitNano > (*sharedNano)))){
        //Receive message...message queue controlling iterations of loops
        if(msgrcv(msqid, &buff, sizeof(msgbuffer), getpid(), 0) == -1){
            perror("Failed to receive message\n");
            exit(1);
        }
        //Seed random
        if(randomize()){
            fprintf(stderr, "Warning: No sources for randomness.\n");
        }

        //Calculate termination chance
   

        if(rand() % 101 <= terminationChance){
            int quantaPercentage = rand()%101;
            buff.mtype = ppid;
            buff.intData = pid;
            buff.quanta = -(buff.quanta*quantaPercentage);
            break;
        }

        //Calculate block chance
        
        else if(rand() % 101 <= blockChance){
            int quantaPercentage = rand()%101;
            buff.mtype = ppid;
            buff.intData = pid;
            buff.quanta = buff.quanta*quantaPercentage;
            break;
        }

        //Calculate if process used entire time allotted
        


        //Otherwise return normal quanta
        


        //Goal is 3 types of results
        //1. terminate after using entire time quanta
        //2. terminate after using percentage of time quanta
        //3. Blocked on i/o interrupt
        //
        //Normal Condition: Increment by time quanta received
        //
        //1. 
        //Generate Random Number
        //If terminate percentage begins
        //Return random percnetage of quanta allocated timeslice used
        //
        //If it does not terminate, then use time quanta or block
        //2. Determine if it will use entire timeslice of it will get blocked
        //Determine via random number
        //

        
                
        //Send message back to parent
        buff.mtype = ppid; //indicates who to send it to
        buff.intData = pid;  
        strcpy(buff.strData, "1"); //1 indicates process still running

        if(msgsnd(msqid, &buff, sizeof(msgbuffer)-sizeof(long), 0) == -1){
            perror("msgsnd to parent failed\n");
            exit(1);
        }

    }
    
    
   
    printf("WORKER PID: %d PPID: %d SysClockS: %d SysClockNano: %d TermTimeS: %d TermTimeNano: %d\n--Terminating\n"
            , pid, ppid, *sharedSeconds, *sharedNano, timeLimitSeconds, timeLimitNano);
    
    //Send terminating message back to parent
    buff.mtype = ppid;
    buff.intData = 0;
    strcpy(buff.strData, "0"); //0 indicates process intends to terminate

    if(msgsnd(msqid, &buff, sizeof(msgbuffer)-sizeof(long), 0) == -1){
        perror("msgsnd to parent failed\n");
        exit(1);
    }
    */

    //Unattach shared memory pointer
    shmdt(sharedSeconds);
    shmdt(sharedNano);
      
    return EXIT_SUCCESS;
}
