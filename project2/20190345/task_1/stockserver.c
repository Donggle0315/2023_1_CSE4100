/* 
 * echoserveri.c - An iterative echo server 
 */ 
/* $begin echoserverimain */
#include "csapp.h"
#include "time.h"

typedef struct{ /* represents a pool of connected descriptors */
    int maxfd; /* largest descriptor in read_set */
    fd_set read_set; /* set of all active descriptors */
    fd_set ready_set; /* subset of descriptors ready for reading */
    int nready; /* number of ready descriptors from select */
    int maxi; /* high water index into client array */
    int clientfd[FD_SETSIZE]; /* set of active descriptors */
    rio_t clientrio[FD_SETSIZE]; /* set of active read buffers */
}pool;

typedef struct _item{
    int ID;
    int left_stock;
    int price;
    int readcnt;
    sem_t mutex;
    struct _item* parent;
    struct _item* left_child;
    struct _item* right_child;
}item;


void init_pool(int,pool*);
void add_client(int, pool*);
void check_clients(pool*);

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

int byte_cnt=0; /* counts total bytes received by server */
item* head; /* header node of BST */
clock_t start;

int main(int argc, char **argv) {
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
    static pool pool;
    char client_hostname[MAXLINE], client_port[MAXLINE];

    if (argc != 2) {
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(0);
    }

    listenfd = Open_listenfd(argv[1]);
    Signal(SIGINT,sigint_handler);
    init_pool(listenfd,&pool);
    init_stock();
    while (1) {
        /* wait for listening/connected descriptor to become ready */
        pool.ready_set=pool.read_set;
        pool.nready=Select(pool.maxfd+1,&pool.ready_set,NULL,NULL,NULL);
        /* if listening descriptor ready, add new client to pool */
        if(FD_ISSET(listenfd,&pool.ready_set)){
            clientlen=sizeof(struct sockaddr_storage);
            connfd=Accept(listenfd,(SA*)&clientaddr,&clientlen);
            Getnameinfo((SA*)&clientaddr,clientlen,client_hostname,MAXLINE,client_port,MAXLINE,0);
            printf("Connected to (%s,%s)\n",client_hostname,client_port);
            add_client(connfd,&pool);
        }
        /* echo a text line from each ready connected descriptor */
	    check_clients(&pool);
    }
    
    writeStockToFile();
    deallocate_stock(head);
    exit(0); 
}
/* $end echoserverimain */

void init_pool(int listenfd,pool* p){
    /* initially, there are no connected descriptors */
    p->maxi=-1;
    for(int i=0;i<FD_SETSIZE;i++){
        p->clientfd[i]=-1;
    }
    /* initially, listenfd is only member of select read set */
    p->maxfd=listenfd;
    FD_ZERO(&p->read_set);
    FD_SET(listenfd,&p->read_set);
}

void add_client(int connfd, pool* p){
    int i;
    p->nready--;
    for(i=0;i<FD_SETSIZE;i++){ /* find an available slot */
        if(p->clientfd[i]<0){
            /* add connected descriptor to the pool */
            p->clientfd[i]=connfd;
            Rio_readinitb(&p->clientrio[i],connfd);
            /* add the descriptor to descriptor set */
            FD_SET(connfd,&p->read_set);
            /* update max descriptor and pool high water mark */
            if(connfd>p->maxfd) p->maxfd=connfd;
            if(i>p->maxi) p->maxi=i;
            break;
        }
    }
    if(i==FD_SETSIZE){/* couldn't find an empty slot */
        app_error("add_client error : Too many clients");
    }
}

void check_clients(pool* p){
    int connfd,n;
    char buf[MAXLINE];
    rio_t rio;

    for(int i=0;(i<=p->maxi)&&(p->nready>0);i++){
        connfd=p->clientfd[i];
        rio=p->clientrio[i];

        /* if the descriptor is ready, echo a text line from it */
        if((connfd>0)&&(FD_ISSET(connfd,&p->ready_set))){
            p->nready--;
            if((n=Rio_readlineb(&rio,buf,MAXLINE))!=0){
                byte_cnt+=n;
                printf("Sever received %d (%d total) bytes on fd %d\n",n,byte_cnt,connfd);
                //Rio_writen(connfd,buf,n);
                if(!strncmp(buf,"exit",4)){//command is "exit" from the client
                    Close(connfd);
                    FD_CLR(connfd,&p->read_set);
                    p->clientfd[i]=-1;
                    printf("close client %d\n",connfd);
                    continue;
                }
                handle_client_request(connfd,buf);//handling the request of the client[i]
            }
            /* EOF detected, remove descriptor from pool */
            else{
                Close(connfd);
                FD_CLR(connfd,&p->read_set);
                p->clientfd[i]=-1;
                printf("close client %d\n",connfd);

            }
        }
    }
}

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

void print_stock(char* result,item* curr,int* cnt,int connfd){
    if(curr==NULL) return;
    if(curr->left_child!=NULL) print_stock(result,curr->left_child,cnt,connfd);
    if(curr->right_child!=NULL) print_stock(result,curr->right_child,cnt,connfd);
    /* make string buffer for one stock state */
    char buf[100];
    sprintf(buf,"%d %d %d\n",curr->ID,curr->left_stock,curr->price);
    int n;
    for(n=0;buf[n]!='\0';n++){
        ;
    }
    (*cnt)+=n;
    strcat(result,buf);
}

void handle_client_request(int connfd,char* buf){
    if(!strncmp(buf,"show",4)){
        command_show(connfd);
    }
    else if(!strncmp(buf,"sell",4)){
        /* parse the buffer string */
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
        /* parse the buffer string */
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
    if(node==NULL){//no such stock id
        Rio_writen(connfd,"fail to sell stock\n",MAXLINE);
        return;
    }
    node->left_stock+=cnt; //increase the stock state
    Rio_writen(connfd,"[sell] success\n",MAXLINE);
}

void command_buy(int connfd,int id,int cnt){
    item* node=find_stock_item(id);
    if(node==NULL){//no such stock id
        Rio_writen(connfd,"fail to buy stock\n",MAXLINE);
        return;
    }
    if(node->left_stock<cnt){//left stock is less than buy request
        Rio_writen(connfd,"Not enough left stock\n",MAXLINE);
        return;
    }
    node->left_stock-=cnt;//decrease the stock state
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
    /* print stock information to FILE pointer */
    fprintf(fp,"%d %d %d\n",curr->ID,curr->left_stock,curr->price);
}

void sigint_handler(int s){//signal handler for SIGINT
    sigset_t mask_all,prev_all;
    Sigfillset(&mask_all);
    writeStockToFile();
    Sigprocmask(SIG_BLOCK,&mask_all,&prev_all); // block all siganl for unsafe function
    deallocate_stock(head);
    Sigprocmask(SIG_SETMASK,&prev_all,NULL); // restore sigset_t
    exit(0);
}