/* $begin shellmain */
#include "csapp.h"
#include <errno.h>
#define MAXARGS    128
#define STDIN      0
#define STDOUT     1
#define STDERR     2
#define EMPTY      10
#define RUNNING    11
#define SUSPENDED  12
#define FOREGROUND 20
#define BACKGROUND 21

 
/* struct for linked-list to implement history */
typedef struct _comm_node{
    int command_num;//#th
    int len_str;
    char* command;//#th command
    struct _comm_node* prev;//previous node
    struct _comm_node* next;//next node
}comm_node;

/* struct for job list for command "jobs" */
typedef struct _job_node{
    int job_num; // job number in job_list
    int state; //RUNNING || SUSPENDED
    int backORfore;// FOREGROUND || BACKGROUND
    pid_t pid;//process id
    pid_t ppid;//parent process id
    char command[MAXLINE];//command
}job_node;

/* Function prototypes */
void eval(char *cmdline);
int parseline(char *buf, char **argv);
int builtin_command(char **argv); 
void command_cd(char** argv);
void command_history(char** argv);
void read_history();
void write_history();
void insert_node(int,char*);
void delete_node();

void command_pipe(char**,int,char*);

void sigchld_handler(int);
void sigint_handler(int);
void sigtstp_handler(int);
void initjob();
void addjob(int,int,pid_t,pid_t,char*);
void deletejob(pid_t);
void command_jobs(char**);
void command_bg_job(char**);
void command_fg_job(char**);
void command_kill_job(char**);

/* gloval variables */
comm_node* head;//head pointer of sturct comm_node
comm_node* curr;//current pointer of struct comm_node
comm_node* tail;//tail pointer of struct comm_node
int line_num;//number of the command_line stored in linked list
char start_path[MAXLINE];//directory path of history file open

int pipeline_num;
int pipeIdx[MAXARGS];

job_node job_list[MAXLINE];
int numofjobs;
volatile pid_t global_pid;


int main() 
{
    char cmdline[MAXLINE]; /* Command line */
    char* buffer;

    /* set user-made signal handler */
    Signal(SIGCHLD,sigchld_handler);
    Signal(SIGTSTP,sigtstp_handler);
    Signal(SIGINT,sigint_handler);

    /* initialize history and job_list */
    read_history();
    initjob();
    
    while (1) {
	    /* Read */
	    printf("> ");                   
	    buffer=fgets(cmdline, MAXLINE, stdin); 
	    if (feof(stdin)){
	        exit(0);
        }
	    /* Evaluate */
	    eval(cmdline);
    }
}
/* $end shellmain */
  
/* $begin eval */
/* eval - Evaluate a command line */
void eval(char *cmdline) 
{
    char *argv[MAXARGS]; /* Argument list execvp() */
    char buf[MAXLINE];   /* Holds modified command line */
    int bg;              /* Should the job run in bg or fg? */
    pid_t pid;           /* Process id */
    sigset_t mask_all,mask_one, prev_one;

    Sigfillset(&mask_all);
    Sigemptyset(&mask_one);
    Sigaddset(&mask_one,SIGCHLD);

    insert_node(++line_num,cmdline);// insert cmdline to the linked list for history function
    strcpy(buf, cmdline);
    bg = parseline(buf, argv); 
    if (argv[0] == NULL) return;   /* Ignore empty lines */


    if(pipeline_num!=0){//if command line consists of pipes('|')
        command_pipe(argv,bg,cmdline);
    }
    /* command line doesn't consist of pipe('|') */
    else if (!builtin_command(argv)) { //quit -> exit(0), & -> ignore, other -> run
        /* Child calls execvp() to run other program */
        Sigprocmask(SIG_BLOCK,&mask_one,&prev_one); //blocking SIGCHLD signal
        if((pid=Fork())==0){
            Sigprocmask(SIG_SETMASK,&prev_one,NULL);//unblocking SIGCHLD signal
            setpgid(0,0);//branch to a new group
            if(execvp(argv[0],argv)<0){
                printf("%s: Command not found.\n",argv[0]);
                exit(0);
            }
        }
       	/* Parent waits for foreground job to terminate */
        else{
            Sigprocmask(SIG_BLOCK,&mask_all,NULL);//blocking all signals
            /* add executing job to job_list */
            global_pid=0;
            if(!bg){//not background
                addjob(RUNNING,FOREGROUND,pid,getpid(),cmdline);//add foreground job to job_list
                int status;
                Sigprocmask(SIG_SETMASK,&prev_one,NULL);
                Sigsuspend(&prev_one);//wait for terminated job to reaped
            }
            else{//when there is background process!
                addjob(RUNNING,BACKGROUND,pid,getpid(),cmdline);//add background job to job_list
                Sigprocmask(SIG_SETMASK,&prev_one,NULL);
                printf("%d %s",pid,cmdline);
            }
            //pid_t wpid=Waitpid(pid,&child_status,0);//wait for child process of pid to terminate.
        }
    }
	
}

/* If first arg is a builtin command, run it and return true */
int builtin_command(char **argv) 
{
    if (!strcmp(argv[0], "quit")||!strcmp(argv[0],"exit")){ /* quit or exit command */
	    write_history();//before call exit(), store history command lines to the history.txt file
        exit(0);  
    }
    if (!strcmp(argv[0], "&")){    /* Ignore singleton & */
	    return 1;
    }
    if(!strcmp(argv[0],"cd")){ /* cd is built-in command to navigate the directories in your shell*/
        command_cd(argv);
        return 1;
    }
    if(!strcmp(argv[0],"history")||argv[0][0]=='!'){/* history is built-in command to track shell commands executed since your shell started*/
        command_history(argv);
        return 1;
    }
    if(!strcmp(argv[0],"jobs")){/* list the running and stopped background jobs */
        command_jobs(argv);
        return 1;
    }
    if(!strcmp(argv[0],"bg")){/* change a stopped background job to a running background job */
        command_bg_job(argv);
        return 1;
    }
    if(!strcmp(argv[0],"fg")){/* change a stopped or running a background job to a running in the foreground */
        command_fg_job(argv);
        return 1;
    }
    if(!strcmp(argv[0],"kill")){ /* terminate a job */
        command_kill_job(argv);
        return 1;
    }

    return 0;                     /* Not a builtin command */
}
/* $end eval */

/* $begin parseline */
/* parseline - Parse the command line and build the argv array */
int parseline(char *buf, char **argv) {
    char *delim;         /* Points to first space delimiter */
    char* pipe_delim;    /* Points to first pipe(|) delimiter */
    int argc;            /* Number of args */
    int bg=0;              /* Background job? */
    int quote;           /* wrapped bt double quote? */
    char* parsePipe[MAXARGS]; /* store string pointer divided by '|' */
    int pipeNum; /* num of pipe('|') */

    buf[strlen(buf)-1] = ' ';  /* Replace trailing '\n' with space */
    /* parse '&' replace '&' to ' ' and, set bg flag */
    /* Should the job run in the background? */
    for(int buf_last=strlen(buf)-1;buf_last>=0;buf_last--){
        if(buf[buf_last]=='&'){
            bg=1;
            buf[buf_last]=' ';
            break;
        }
        if(buf[buf_last]!=' '){
            bg=0;
            break;
        }
    }
    /* make blank front and back of '|' */
    char tmp[MAXLINE*2];
    int idx=0;
    for(int i=0;i<=strlen(buf);i++){
        if(buf[i]=='|'){
            tmp[idx++]=' ';
            tmp[idx++]='|';
            tmp[idx++]=' ';
            continue;
        }
        tmp[idx++]=buf[i];
    }
    strcpy(buf,tmp);

    strcat(buf,"|"); //add '|' to the end of string 
    while (*buf && (*buf == ' ')){ /* Ignore leading spaces */
	    buf++;
    }
    pipeNum=0;
    argc=0;
    pipeIdx[0]=0;
    pipeline_num=0;
    while((delim=strchr(buf,'|'))){//divide string by the '|'
        parsePipe[pipeNum++]=buf;
        *delim='\0';
        buf=delim+1;
        while(*buf&&((*buf=='|')||(*buf==' '))){//delete '|' and ' ' in front of string
            buf++;
        }
    }
    
    for(int i=0;i<pipeNum;i++){//parsing the string which is divided by '|' with blank
        strcat(parsePipe[i]," ");
        quote=0;
        for(delim=parsePipe[i];(*delim)!='\0';delim++){
            if(quote==0&&(*delim)=='"'){
                (parsePipe[i])++;
                quote=1;
                continue;
            }
            if(quote==0&&(*delim)==' '){
                argv[argc++]=parsePipe[i];
                *delim='\0';
                parsePipe[i]=delim+1;
                while(*(parsePipe[i])&&(*(parsePipe[i]))==' '){
                    (parsePipe[i])++;
                }
                delim=(parsePipe[i])-1;
                continue;
            }
            if(quote==1&&(*delim)=='"'){
                argv[argc++]=parsePipe[i];
                *delim='\0';
                parsePipe[i]=delim+1;
                while(*(parsePipe[i])&&(*(parsePipe[i]))==' '){
                    (parsePipe[i])++;
                }
                delim=(parsePipe[i])-1;
                quote=0;
                continue;
            }
        }
        pipeIdx[++pipeline_num]=argc;//store idx of the first argument after pipeline
    }
    
    argv[argc]=NULL;
    pipeIdx[pipeline_num+1]=argc;
    pipeline_num--;

    if (argc == 0)  /* Ignore blank line */
	return 1;
    return bg;
}
/* $end parseline */

void command_cd(char** argv){//excute "cd" command
    char dirName[MAXLINE+5]="./";//store present directory
    if(argv[1]!=0){//concatenating path in argv[1]
        strcat(dirName,argv[1]);
    }
    else{//no argument in argv[1]
        strcpy(dirName,getenv("HOME"));//dirname is home directory
    }
    int result_chdir=chdir(dirName);//change directory by calling chdir();
    if(result_chdir==-1){//occurs error during chdir() function
        /* print error message by stdout */
        int idx=0;
        printf("-bash: ");
        while(argv[idx]!=0){
            printf("%s: ",argv[idx++]);
        }
        printf("%s\n",strerror(errno));
    }
}

void command_history(char** argv){//excute "history", "!!", "!#" commands
    /* excute "history" command */
    if(!strcmp("history",argv[0])){
        int idx=1;
        curr=head->next;//start from head point.
        while(curr!=tail){//finish from tail point.
            printf("%d: %s",idx++,curr->command);//show command line on the shell
            curr=curr->next;
        }
    }
    /* excute "!!" command */
    else if(!strcmp("!!",argv[0])){
        if(line_num==0) return;
        printf("%s",tail->prev->command);//show last command line on the shell
        eval(tail->prev->command);//excute the last command
    }
    /* excute "!#" command */
    else if(argv[0][0]=='!'){
        int idx=0;
        for(int i=1;i<(int)strlen(argv[0]);i++){//parsing the index number
            idx*=10;
            if(argv[0][i]-'0'<0||argv[0][i]-'0'>9){//index num is not integer
                printf("Invalid input\n");
                return;
            }
            idx+=(argv[0][i]-'0');
        }
        if(idx<0||idx>line_num){//out of the range
            printf("Invalid Range\n");
            return;
        }
        for(curr=head->next;curr->command_num!=idx;curr=curr->next){//move to #th node
            ;
        }
        printf("%s",curr->command);//show #th command line on the shell
        eval(curr->command);//excute the #th command
    }

}

void read_history(){//read Past command lines from history.txt
    FILE* fp;
    char* tmp;
    char buffer[MAXLINE+1];
    if(getcwd(start_path,MAXLINE)==NULL){//store directory path when opening history.txt
        perror("current working directory get error");
        exit(1);
    }
    /* initialize linked list for history command */
    head=Malloc(sizeof(comm_node));
    tail=Malloc(sizeof(comm_node));
    head->prev=NULL;
    head->next=tail;
    tail->prev=head;
    tail->next=NULL;
    curr=head;
    fp=fopen("history.txt","rt");
    if(fp==NULL){
        line_num=0;
        return;
    }

    /* load command lines in history.txt */
    int scanfTmp=fscanf(fp,"%d ",&line_num);
    for(int i=1;i<=line_num;i++){
        tmp=fgets(buffer,MAXLINE,fp);
        insert_node(i,buffer);
    }
    fclose(fp);
}

void write_history(){//write command lines to history.txt before exit the shell
    FILE* fp;
    int result_chdir=chdir(start_path);//move directory path where history.txt is
    if(result_chdir==-1){
        perror("fail to store history command in file");
        exit(1);
    }
    curr=head->next; //start from head point
    fp=fopen("history.txt","wt");
    fprintf(fp,"%d\n",line_num);
    for(int i=1;i<=line_num;i++){//write command line to file and delete written node
        int re=fputs(curr->command,fp);
        delete_node();
    }
    free(tail);
    free(head);
    fclose(fp);
}

void insert_node(int command_num, char* command_line){//insert node to the tail side of linked list
    if(command_line[0]=='!'){//if command_line is "!#" || "!!", then return without insertion
        line_num--;
        return;
    }
    if(tail->prev!=head&&!strcmp(command_line,tail->prev->command)){//if duplicated command is inserted, then return without insertion
        line_num--;
        return;
    }
    /* allocate new node */
    comm_node* tmp=Malloc(sizeof(comm_node));
    tmp->command_num=command_num;
    tmp->command=Malloc(sizeof(char)*(strlen(command_line)+1));
    strcpy(tmp->command,command_line);
    tmp->len_str=(int)strlen(command_line);
    tail->prev->next=tmp;
    tmp->prev=tail->prev;
    tail->prev=tmp;
    tmp->next=tail;
}

void delete_node(){//delete node at the curr pointer 
    comm_node* tmp=curr;
    curr->prev->next=curr->next;
    curr->next->prev=curr->prev;
    curr=curr->next;
    free(tmp->command);
    free(tmp);
}

void command_pipe(char** argv,int bg,char* cmdline){

    sigset_t mask_all,mask_one, prev_one;
    Sigfillset(&mask_all);
    Sigemptyset(&mask_one);
    Sigaddset(&mask_one,SIGCHLD);

    //parse argv by pipe('|')
    char* newArgv[MAXARGS][MAXARGS];
    int rIdx=0,cIdx=0;
    for(int i=0;argv[i]!=NULL;i++){
        if(i==pipeIdx[rIdx+1]){
            newArgv[rIdx][cIdx]='\0';
            rIdx++;
            cIdx=0;
        }
        newArgv[rIdx][cIdx]=argv[i];
        cIdx++;
    }


    
    //make pipeline
    int pipefds[2*pipeline_num];
    for(int i=0;i<pipeline_num;i++){
        if(pipe(&pipefds[2*i])<0){
            perror("pipe error");
            exit(1);
        }
    }

    //execute parsed cmd for pipeline_num+1 times
    int status;
    pid_t pid;
    int idx=0;//pipe's index
    //setpgid(0,0);
    for(int i=0;i<=pipeline_num;i++,idx=idx+2){
        Sigprocmask(SIG_BLOCK,&mask_one,&prev_one);
        if((pid=fork())==0){//child process
            if(i==0) setpgid(0,0);
            if(i!=pipeline_num){//not last command
                if(dup2(pipefds[idx+1],STDOUT)<0){//change stdout
                    perror("1.dup2-1 error");
                    exit(1);
                }
            }
            if(idx!=0){//not first command
                if(dup2(pipefds[idx-2],STDIN)<0){//change stdin
                    perror("1,dup2-2 error");
                    exit(1);
                }
            }
            for(int j=0;j<2*pipeline_num;j++){//close pipelines
                close(pipefds[j]);
            }
            Sigprocmask(SIG_SETMASK,&prev_one,NULL);
            setpgid(0,0);
            if(!builtin_command(newArgv[i])){//if parsed cmd is built-in cmd, then execute and return 1
                //not built-in cmd
                if(execvp(newArgv[i][0],newArgv[i])<0){//execute not-built-in cmd
                    perror("execvp error");
                    exit(1);
                }
            }
            exit(1);
        }
        
    }
    for(int i=0;i<2*pipeline_num;i++){//close all pipelines
        close(pipefds[i]);
    }
    /* add job to job_list */
    if(!bg){
        addjob(RUNNING,FOREGROUND,pid,getpid(),cmdline);
    }
    else{
        addjob(RUNNING,BACKGROUND,pid,getpid(),cmdline);
    }
    for(int i=0;i<=pipeline_num;i++){//reaping for child processes
        pid_t tmp=wait(&status);
        deletejob(tmp);//delete job for terminated job
    }
    
}

void sigchld_handler(int s){//signal handler for SIGCHLD
    int olderrno=errno;
    sigset_t mask_all,prev_all;
    pid_t pid;
    int status;

    Sigfillset(&mask_all);
    while((pid=waitpid(-1,&status,WUNTRACED|WNOHANG))>0){//wait for child's terminating
        if(WIFSTOPPED(status)){//signal is SIGTSTP
            for(int i=0;i<MAXLINE;i++){
                if(job_list[i].pid==pid){//find job
                    job_list[i].state=SUSPENDED;
                    printf("Job %d stopped by signal %d\n",pid,WSTOPSIG(status));
                }
            }
        }
        else if(WIFSIGNALED(status)){//return true when child process terminated by any signal
            if(WTERMSIG(status)!=SIGKILL){//signal which terminate child process is not SIGKILL
                printf("Job %d terminated by signal %d\n",pid, WTERMSIG(status));
            }
            Sigprocmask(SIG_BLOCK,&mask_all,&prev_all);//blocking all signals
            deletejob(pid);//delete job from the job_list;
            Sigprocmask(SIG_SETMASK,&prev_all,NULL);//revert to previous status
        }
        else if(WIFEXITED(status)){//child process is terminated normally
            Sigprocmask(SIG_BLOCK,&mask_all,&prev_all);
            deletejob(pid);
            Sigprocmask(SIG_SETMASK,&prev_all,NULL);
        }
    }
    errno=olderrno;
}

void sigint_handler(int s){//signal handler for SIGINT
    int olderrno=errno;
    for(int i=0;i<MAXLINE;i++){
        if(job_list[i].state!=EMPTY&&job_list[i].backORfore==FOREGROUND){
                Kill(-job_list[i].pid,SIGINT);//send SIGINT signal to FOREGOURND process
                break;  
        }
    }
    errno=olderrno;
}

void sigtstp_handler(int s){//signal handler for SIGTSTP
    int olderrno=errno;
    for(int i=0;i<MAXLINE;i++){
        if(job_list[i].state!=EMPTY&&job_list[i].backORfore==FOREGROUND){
                Kill(-job_list[i].pid,SIGTSTP);//send SIGTSTP signal to FOREGROUND process
                break;  
        }
    }
    errno=olderrno;
}
 
void initjob(){//initialize job_list
    numofjobs=0;
    for(int i=0;i<MAXLINE;i++){
        job_list[i].state=EMPTY;
    }
}

void addjob(int state, int bf, pid_t pid, pid_t ppid, char* command){//add job to job_list
    int idx;
    for(idx=0;idx<MAXLINE;idx++){
        if(job_list[idx].state==EMPTY){
            job_list[idx].job_num=numofjobs++;
            job_list[idx].state=state;
            job_list[idx].backORfore=bf;
            job_list[idx].pid=pid;
            job_list[idx].ppid=getpgrp();
            strcpy(job_list[idx].command,command);
            return;
        }
    }
}

void deletejob(pid_t pid){//delete job from job_list
    int idx;
    for(idx=0;idx<MAXLINE;idx++){
        if(job_list[idx].pid==pid){
            job_list[idx].state=EMPTY;
            numofjobs--;
            return;
        }
    }
}

void command_jobs(char** argv){//list the running and stopped background jobs
    printf("JN  State\t\t\tcommand\n");
    for(int i=0;i<MAXLINE;i++){
        if(job_list[i].state==EMPTY) continue;
        /* print job list which is not EMPTY */
        printf("[%d]",job_list[i].job_num);
        if(job_list[i].state==RUNNING){
            printf("%8s","RUNNING");
        }
        else{
            printf("%8s","SUSPENDED");
        }
        printf("\t\t%s",job_list[i].command);
    }
}

void command_bg_job(char** argv){//change a stopped background job to a running background job
    if(argv[1][0]!='%'){
        printf("bash: bg: (%s) - Operation not permitted\n",argv[1]);
        return;
    }
    int jobNum=0;
    for(int i=1;i<strlen(argv[1]);i++){//calculate job num
        if(!('0'<=argv[1][i]&&argv[1][i]<'9')){//not integer
            printf("bash: bg: %s: arguments must be process or job IDs",argv[1]);
            return;
        }
        jobNum=jobNum*10;
        jobNum=jobNum+(argv[1][i]-'0');
    }
    
    int idx=0;
    for(int idx=0;idx<MAXLINE;idx++){//find jobNum in job_list
        if(job_list[idx].job_num==jobNum&&job_list[idx].state!=EMPTY){//find the jobNum
            kill(-(job_list[idx].pid),SIGCONT);//send SIGCONT signal to joba
            //change state
            job_list[idx].backORfore=BACKGROUND;
            job_list[idx].state=RUNNING;
            break;
        }
    }
    if(idx==MAXLINE){//jobNum doesn't exist
        printf("bash: bg: %s: no such job\n",argv[1]);
    }
}

void command_fg_job(char** argv){//change a stopped or running a background job to a running in the foreground
    if(argv[1][0]!='%'){
        printf("bash: fg: (%s) - Operation not permitted\n",argv[1]);
        return;
    }
    int jobNum=0;
    for(int i=1;i<strlen(argv[1]);i++){//calculate job num
        if(!('0'<=argv[1][i]&&argv[1][i]<'9')){//not integer
            printf("bash: bg: %s: arguments must be process or job IDs",argv[1]);
            return;
        }
        jobNum=jobNum*10;
        jobNum=jobNum+(argv[1][i]-'0');
    }
    int idx;
    for(idx=0;idx<MAXLINE;idx++){//find jobNum in job_list
        if(job_list[idx].job_num==jobNum&&job_list[idx].state!=EMPTY){//find the jobNum
            kill((job_list[idx].pid),SIGCONT);//send SIGCONT signal to job
            //change status
            job_list[idx].backORfore=FOREGROUND;
            job_list[idx].state=RUNNING;
            break;
        }
    }
    if(idx==MAXLINE){//jobNum doesn't exist
        printf("bash: bg: %s: no such job\n",argv[1]);
    }
}

void command_kill_job(char** argv){//terminate a job
    if(argv[1][0]!='%'){//not start with %
        printf("bash: kill: (%s) - Operation not permitted\n",argv[1]);
        return;
    }
    int jobNum=0;
    for(int i=1;i<strlen(argv[1]);i++){//calculate job num
        if(!('0'<=argv[1][i]&&argv[1][i]<'9')){//not integer
            printf("bash: kill: %s: arguments must be process or job IDs",argv[1]);
            return;
        }
        jobNum=jobNum*10;
        jobNum=jobNum+(argv[1][i]-'0');
    }
    int idx=0;
    for(int idx=0;idx<MAXLINE;idx++){//find jobNum in job_list
        if(job_list[idx].job_num==jobNum&&job_list[idx].state!=EMPTY){//find the jobNum
            Kill((job_list[idx].pid),SIGKILL);//send SIGKILL signal to job for terminating
            break;
        }
    }
    if(idx==MAXLINE){//jobNum doesn't exist
        printf("bash: kill: %s: no such job\n",argv[1]);
    }
}