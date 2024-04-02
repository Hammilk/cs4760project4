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


static int randomize_helper(FILE *in){
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
    int intData; //I'll send pid with this
    int quanta; //This is for time quanta
} msgbuffer;

int main(int argc, char** argv){
    //Passed command line arguments
    //arg[1] = termination chance
    //arg[2] = block chance
    //arg[3] = Upper bound for time spent in system


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
    int quantaPercentage;
    int earlyTerm = 0;
    
    //Parse out current SysClock and SysNano time
    int sysClockS = *sharedSeconds; //Starting seconds
    int sysClockNano = *sharedNano; //Starting nanoseconds

    //Defining constant for termination and block chance
    const int terminationChance = atoi(argv[1]);
    const int blockChance = atoi(argv[2]);
    printf("t: %d\n", atoi(argv[1]));
    printf("b: %d\n", atoi(argv[2]));
    printf("lim: %d\n", atoi(argv[3]));

    //Define upper bound for child to spent in system
    int timeLimitNano = atoi(argv[3]) + sysClockNano;
    int timeLimitSeconds = sysClockS;

    if(timeLimitNano > pow(10, 9)){
        timeLimitSeconds++;
        timeLimitNano = timeLimitNano - pow(10, 9);
    }

    //Seed random  
    if(randomize()){
        fprintf(stderr, "Warning: No sources for randomness.\n");
    }
    

    while(timeLimitSeconds > sysClockS || ((timeLimitSeconds == sysClockS) && (timeLimitNano > sysClockNano))){
        printf("Child work section entered\n");    
        //Receive message...message queue controlling iterations of loops
        if(msgrcv(msqid, &buff, sizeof(msgbuffer), getpid(), 0) == -1){
            perror("Failed to receive message\n");
            exit(1);
        }
        printf("Child received message\n");

        //Calculate termination chance

        if(rand() % 501 <= terminationChance){
            quantaPercentage = rand()%101;
            printf("qP %d\n", quantaPercentage);
            buff.quanta = -(buff.quanta*quantaPercentage/100);
            printf("Test: Termination\n");
            buff.mtype = ppid;
            buff.intData = pid;
            if(msgsnd(msqid, &buff, sizeof(msgbuffer)-sizeof(long), 0) == -1){
                perror("msgsnd to parent failed\n");
                exit(1);
            }
            printf("buff quanta: %d\n", buff.quanta);
            earlyTerm = 1;
            break;
        }

        //Calculate block chance
        
        else if(rand() % 501 <= blockChance){
            quantaPercentage = rand()%101;
            buff.quanta = buff.quanta*quantaPercentage/100;
            printf("Test: Block\n");
        }

        else printf("Test: Whole\n");
        
        //If child uses entire time quanta, it will default to this.
        //Child will send back the quanta it was sent
        //Send message back to parent
        buff.mtype = ppid; //indicates who to send it to
        buff.intData = pid;  

        if(msgsnd(msqid, &buff, sizeof(msgbuffer)-sizeof(long), 0) == -1){
            perror("msgsnd to parent failed\n");
            exit(1);
        }
        printf("Message sent from child\n");
    }
    printf("Exit child loop\n");
    if(earlyTerm == 0){
        buff.mtype = ppid;
        buff.intData = pid;
        int timeElapsed = (sysClockS * pow(10, 9) + sysClockNano) - (timeLimitSeconds * pow(10, 9) + timeLimitNano);
        buff.quanta = -(buff.quanta - timeElapsed);

        if(msgsnd(msqid, &buff, sizeof(msgbuffer)-sizeof(long), 0) == -1){
            perror("msgsnd to parent failed\n");
            exit(1);
        }
    }    
    
    //Unattach shared memory pointer
    shmdt(sharedSeconds);
    shmdt(sharedNano);
      
    return EXIT_SUCCESS;
}
