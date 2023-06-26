/* $begin shellmain */
#include "csapp.h"
#include <errno.h>
#define MAXARGS   128

/* struct for linked-list to implement history */
typedef struct _comm_node{
    int command_num;//#th
    int len_str;
    char* command;//#th command
    struct _comm_node* prev;//previous node
    struct _comm_node* next;//next node
}comm_node;

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

/* gloval variables */
comm_node* head;//head pointer of sturct comm_node
comm_node* curr;//current pointer of struct comm_node
comm_node* tail;//tail pointer of struct comm_node
int line_num;//number of the command_line stored in linked list
char start_path[MAXLINE];//directory path of history file open

int main() 
{
    char cmdline[MAXLINE]; /* Command line */
    char* buffer;
    read_history();
    while (1) {
	/* Read */
	printf("> ");                   
	buffer=fgets(cmdline, MAXLINE, stdin); 
	if (feof(stdin))
	    exit(0);

	/* Evaluate */
	eval(cmdline);
    }
}
/* $end shellmain */
  
/* $begin eval */
/* eval - Evaluate a command line */
void eval(char *cmdline) 
{
    char *argv[MAXARGS]; /* Argument list execve() */
    char buf[MAXLINE];   /* Holds modified command line */
    int bg;              /* Should the job run in bg or fg? */
    pid_t pid;           /* Process id */
    int child_status;
    insert_node(++line_num,cmdline);// insert cmdline to the linked list for history function
    strcpy(buf, cmdline);
    bg = parseline(buf, argv); 
    if (argv[0] == NULL)  return;   /* Ignore empty lines */
    if (!builtin_command(argv)) { //quit -> exit(0), & -> ignore, other -> run
        /* Child calls execve() to run other program */
        if(pid=Fork()==0){
            if(execvp(argv[0],argv)<0){
                printf("%s: Command not found.\n",argv[0]);
                exit(0);
            }
        }
       	/* Parent waits for foreground job to terminate */
        else{
            pid_t wpid=Waitpid(pid,&child_status,0);//wait for child process of pid to terminate.
            if(!bg){
                int status;
            }
            else{//when there is background process!
                printf("%d %s",pid,cmdline);
            }
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
    return 0;                     /* Not a builtin command */
}
/* $end eval */

/* $begin parseline */
/* parseline - Parse the command line and build the argv array */
int parseline(char *buf, char **argv) 
{
    char *delim;         /* Points to first space delimiter */
    int argc;            /* Number of args */
    int bg;              /* Background job? */
    int quote;           /* wrapped by double quote? */
    buf[strlen(buf)-1] = ' ';  /* Replace trailing '\n' with space */
    while (*buf && (*buf == ' ')){ /* Ignore leading spaces */
	    buf++;
    }
    /* Build the argv list */
    argc = 0;
    quote=0;
    for(delim=buf;*delim!='\0';delim++){
        if(quote==0&&(*delim)=='"'){//start quote
            buf++;
            quote=1;//in string
            continue;
        }
        if(quote==0&&(*delim)==' '){//blank not in the quote
            //parsing
            argv[argc++]=buf;
            *delim='\0';
            buf=delim+1;
            while(*buf&&(*buf==' ')){
                buf++;
            }
            delim=buf-1;
            continue;
        }
        if(quote==1&&(*delim)=='"'){//finish quote
            //parsing
            argv[argc++]=buf;
            *delim='\0';
            buf=delim+1;
            while(*buf&&(*buf==' ')){
                buf++;
            }
            delim=buf-1;
            quote=0;//out of string
            continue;
        }
    }
    
    argv[argc] = NULL;

    if (argc == 0)  /* Ignore blank line */
	return 1;

    /* Should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0)
	argv[--argc] = NULL;

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
        printf("current working directory get error : %s\n",strerror(errno));
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
        printf("fail to store history command in file\n");
        printf("%s\n,",strerror(errno));
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
    tmp->command=Malloc(sizeof(char)*sizeof(strlen(command_line)+1));
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
