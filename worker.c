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

typedef struct msgbuffer{
    long mtype;
    char strData[10];
    int intData;
} msgbuffer;


int main(int argc, char** argv){
    
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
    
    
    

    //Work
   
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

    printf("WORKER PID: %d PPID: %d SysClockS: %d SysClockNano: %d TermTimeS: %d TermTimeNano: %d\n--Just Starting\n"
            , pid, ppid, *sharedSeconds, *sharedNano, timeLimitSeconds, timeLimitNano);

     
    while(timeLimitSeconds > (*sharedSeconds) || ((timeLimitSeconds == (*sharedSeconds)) && (timeLimitNano > (*sharedNano)))){
        //Receive message...message queue controlling iterations of loops
        if(msgrcv(msqid, &buff, sizeof(msgbuffer), getpid(), 0) == -1){
            perror("Failed to receive message\n");
            exit(1);
        }
        


        
                
        //Send message back to parent
        buff.mtype = ppid;
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

    //Unattach shared memory pointer
    shmdt(sharedSeconds);
    shmdt(sharedNano);
      
    return EXIT_SUCCESS;
}
