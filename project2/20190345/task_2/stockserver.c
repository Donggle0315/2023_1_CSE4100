/* 
 * echoserveri.c - An iterative echo server 
 */ 
/* $begin echoserverimain */
#include "csapp.h"

#define NTHREADS 100 /* number of worker threads */
#define SBUFSIZE 100 /* length of SBUF */

typedef struct _item{
    int ID;
    int left_stock;
    int price;
    int readcnt;
    sem_t mutex; /* counting semaphore for reading and writing */
    sem_t rw_mutex; /* mutex semaphore for reading and writing */
    struct _item* parent;
    struct _item* left_child;
    struct _item* right_child;
}item;

typedef struct{
    int *buf; /* buffer array */
    int n; /* maximum number of slots */
    int front; /* buf[(front+1)%n] is first item */
    int rear; /* buf[rear%n] is last item */
    sem_t mutex; /* protects accesses to buf */
    sem_t slots; /* counts available slots */
    sem_t items; /* counts available items */
}sbuf_t;


void init_stock(); /* make stock BST from the "stock.txt" file */
void insertItemToBinTree(item*); /* insert item node to stock BST sorted by ID key */
void deallocate_stock(item*); /* deallocate all node in stock BST */
item* find_stock_item(int); /* find target ID node by traversaling stock BST */
void print_stock(char*,item*,int*,int); /* print all stock state by recursive call */

void handle_client_request(int,char*); /* handle client commands to the server input */
void command_show(int); /* show all stock state by calling print_stock() */
void command_sell(int,int,int); /* sell stock from the request of the client */
void command_buy(int,int,int); /* buy stock from the request of the client */

void writeStockToFile(); /* saving BST to the stock.txt file by calling file_print_stock */
void file_print_stock(FILE*,item*); /* saving BST to the stock.txt file */
void sigint_handler(int); /* install sigint handler for deallocating and saving stock */

void* thread(void*);
static void init_echo_service();
void echo_service(int);
void sbuf_init(sbuf_t*, int); /* create an empty, bounded, shared FIFO buffer with n slots */
void sbuf_deinit(sbuf_t*);/* clean up buffer sp */
void sbuf_insert(sbuf_t*, int); /* insert item onto the rear of shared buffer sp */
int sbuf_remove(sbuf_t*); /* remove and return the first item from buffer sp */

int byte_cnt=0; /* counts total bytes received by server */
item* head; /* header node of BST */
sbuf_t sbuf; /* shared buffer of connected descriptors */
sem_t mutex;

int main(int argc, char **argv) {
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
    pthread_t tid;
    char client_hostname[MAXLINE], client_port[MAXLINE];

    if (argc != 2) {
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(0);
    }

    listenfd = Open_listenfd(argv[1]);
    Signal(SIGINT,sigint_handler);
    init_stock();
    sbuf_init(&sbuf,SBUFSIZE);
    Sem_init(&mutex,0,1);
    for(int i=0;i<NTHREADS;i++){ /* create worker threads */
        Pthread_create(&tid,NULL,thread,NULL);
    }
    
    while (1) {
        // /* if listening descriptor ready, add new client to sbuf */
        clientlen=sizeof(struct sockaddr_storage);
        connfd=Accept(listenfd,(SA*)&clientaddr,&clientlen);
        Getnameinfo((SA*)&clientaddr,clientlen,client_hostname,MAXLINE,client_port,MAXLINE,0);
            printf("Connected to (%s,%s)\n",client_hostname,client_port);
        if(connfd!=0){
            sbuf_insert(&sbuf,connfd);
        }
    }

    writeStockToFile();
    deallocate_stock(head);
    sbuf_deinit(&sbuf);
    exit(0);
}
/* $end echoserverimain */

void init_stock(){
    FILE* fp=fopen("stock.txt","rt");
    if(fp==NULL){//fail to open file
        printf("fail to open stock.txt :");
        exit(1);
    }

    int id,left_stock,price;
    head=NULL;
    /* make stock node and insert to BST */
    while(fscanf(fp,"%d %d %d",&id,&left_stock,&price)>0){
        item* node=(item*)malloc(sizeof(item));
        node->ID=id;
        node->left_stock=left_stock;
        node->price=price;
        node->readcnt=0;
        Sem_init(&node->mutex,0,1); //counting semaphore for readers-writers problem
        Sem_init(&node->rw_mutex,0,1); //mutex semaphore for readers-writers problem
        node->parent=NULL;
        node->left_child=NULL;
        node->right_child=NULL;

        insertItemToBinTree(node);
    }
}

void insertItemToBinTree(item* node){
    /* if the BST is empty */
    if(head==NULL){
        head=node;
        return;
    }
    /* insert to BST. smaller to left and bigger to right child */
    item* curr=head;
    while(1){
        if(curr->ID < node->ID){
            if(curr->right_child==NULL){
                curr->right_child=node;
                node->parent=curr;
                break;
            }
            else{
                curr=curr->right_child;
            }
        }
        else if(curr->ID > node->ID){ //curr->id >= node->id
            if(curr->left_child==NULL){
                curr->left_child=node;
                node->parent=curr;
                break;
            }
            else{
                curr=curr->left_child;
            }
        }
    }
}

void deallocate_stock(item* curr){
    if(curr==NULL) return;
    if(curr->left_child!=NULL) deallocate_stock(curr->left_child);
    if(curr->right_child!=NULL) deallocate_stock(curr->right_child);
    free(curr); //deallocate the leaf node
}

item* find_stock_item(int id){
    item* curr=head;
    /* traversal to find target node */
    while(curr!=NULL){
        if(curr->ID==id) return curr; //find target node
        else if(curr->ID<id) curr=curr->right_child;
        else curr=curr->left_child;
    }
    return NULL;//not found
}

void print_stock(char* result,item* head,int* cnt,int connfd){/* excuted by a reader thread */
    item* curr=head;
    P(&curr->rw_mutex);
    (curr->readcnt)++;
    if(curr->readcnt==1) P(&curr->mutex); /* First-in reader */
    V(&curr->rw_mutex);
    if(curr==NULL) return;
    if(curr->left_child!=NULL) print_stock(result,curr->left_child,cnt,connfd);
    if(curr->right_child!=NULL) print_stock(result,curr->right_child,cnt,connfd);
    /* make string buffer for one stock state */
    char buf[100];
    sprintf(buf,"%d %d %d\n",curr->ID,curr->left_stock,curr->price); //reading
    int n;
    for(n=0;buf[n]!='\0';n++){
        ;
    }
    (*cnt)+=n;
    strcat(result,buf);
    P(&curr->rw_mutex);
    (curr->readcnt)--;
    if(curr->readcnt==0) V(&curr->mutex); /* Last-out reader */
    V(&curr->rw_mutex);
}

void handle_client_request(int connfd,char* buf){
    if(!strncmp(buf,"show",4)){
        command_show(connfd);
    }
    else if(!strncmp(buf,"sell",4)){
        /* parse the buffer string to integer data */
        char* val[3];
        int i=0;
        char* result=strtok(buf," ");
        while(result!=NULL){
            val[i++]=result;
            result=strtok(NULL," ");
        }
        command_sell(connfd,atoi(val[1]),atoi(val[2]));
    } 
    else if(!strncmp(buf,"buy",3)){
        /* parse the buffer string to integer data */
        char* val[3];
        int i=0;
        char* result=strtok(buf," ");
        while(result!=NULL){
            val[i++]=result;
            result=strtok(NULL," ");
        }
        command_buy(connfd,atoi(val[1]),atoi(val[2]));
    }
}

void command_show(int connfd){
    char result[MAXLINE]="";
    int cnt=0;
    print_stock(result,head,&cnt,connfd);
    Rio_writen(connfd,result,MAXLINE);
}

void command_sell(int connfd,int id, int cnt){
    item* node=find_stock_item(id);
    if(node==NULL){
        Rio_writen(connfd,"fail to sell stock\n",MAXLINE);
        return;
    }
    P(&node->mutex);
    node->left_stock+=cnt; //increase the stock state, writing
    V(&node->mutex);
    Rio_writen(connfd,"[sell] success\n",MAXLINE);
}

void command_buy(int connfd,int id,int cnt){
    item* node=find_stock_item(id);
    if(node==NULL){
        Rio_writen(connfd,"fail to buy stock\n",MAXLINE);
        return;
    }
    if(node->left_stock<cnt){
        Rio_writen(connfd,"Not enough left stock\n",MAXLINE);
        return;
    }
    P(&node->mutex);
    node->left_stock-=cnt;//decrease the stock state, writing
    V(&node->mutex);
    Rio_writen(connfd,"[buy] success\n",MAXLINE);
}

void writeStockToFile(){
    FILE* fp=fopen("stock.txt","wt");
    if(fp==NULL){
        printf("fail to file open\n");
        exit(1);
    }

    file_print_stock(fp,head);   
}

void file_print_stock(FILE* fp,item* curr){
    if(curr==NULL) return;
    if(curr->left_child!=NULL) file_print_stock(fp,curr->left_child);
    if(curr->right_child!=NULL) file_print_stock(fp,curr->right_child);

    fprintf(fp,"%d %d %d\n",curr->ID,curr->left_stock,curr->price);
}

void sigint_handler(int s){//signal handler for SIGINT
    sigset_t mask_all,prev_all;
    Sigfillset(&mask_all);
    writeStockToFile();
    Sigprocmask(SIG_BLOCK,&mask_all,&prev_all); // block all siganl for unsafe function
    deallocate_stock(head);
    sbuf_deinit(&sbuf);
    Sigprocmask(SIG_SETMASK,&prev_all,NULL); // restore sigset_t
    exit(0);
}

void* thread(void* vargp){
    Pthread_detach(pthread_self());
    while(1){
        int connfd=sbuf_remove(&sbuf); /* remove connfd from buf */
        if(connfd==0) continue;
        echo_service(connfd); /* service client */
    }
}

static void init_echo_service(void){
    Sem_init(&mutex,0,1);
    byte_cnt=0;
}

void echo_service(int connfd){
    int n;
    char buf[MAXLINE];
    rio_t rio;
    static pthread_once_t once=PTHREAD_ONCE_INIT;
    
    Pthread_once(&once,init_echo_service);
    Rio_readinitb(&rio,connfd);
    while((n=Rio_readlineb(&rio,buf,MAXLINE))!=0){
        P(&mutex);
        byte_cnt+=n;
        V(&mutex);
        if(!strncmp(buf,"exit",4)){
            Close(connfd);
            printf("%d thread closes client %d\n",(int)pthread_self(),connfd);
            return;
        }
        handle_client_request(connfd,buf);
        printf("thread %d received %d (%d total) bytes on fd %d\n",(int)pthread_self(),n,byte_cnt,connfd);
    }

    Close(connfd);
    printf("%d thread closes client %d\n",(int)pthread_self(),connfd);
}

void sbuf_init(sbuf_t* sp,int n){
    sp->buf=Calloc(n,sizeof(int));
    sp->n=n; /* buffer holds max of n items */
    sp->front=sp->rear=0; /* empty buffer iff front==rear */
    Sem_init(&sp->mutex,0,1); /* binary semaphore for locking */
    Sem_init(&sp->slots,0,n); /* initially, buf has n empty slots */
    Sem_init(&sp->items,0,0); /* initially, buf has zero data items */
}

void sbuf_deinit(sbuf_t* sp){
    Free(sp->buf);
}

void sbuf_insert(sbuf_t* sp, int item){
    P(&sp->slots); /* wait for available slot */
    P(&sp->mutex); /* lock the buffer */
    sp->buf[((sp->rear)++)%(sp->n)]=item; /* insert the item */
    V(&sp->mutex); /* unlock the buffer */
    V(&sp->items); /* announce available item */
}

int sbuf_remove(sbuf_t* sp){
    int item;
    P(&sp->items); /* wait for available item */
    P(&sp->mutex); /* lock the buffer */
    item=sp->buf[((sp->front)++)%(sp->n)]; /* remove the item */
    V(&sp->mutex); /* unlock the buffer */
    V(&sp->slots); /* announce available slot */
    return item;
}