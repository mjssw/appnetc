
#ifndef __AE_SERVER_H__
#define __AE_SERVER_H__
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "ae.h"
#include "share_memory.h"
#define MAXFD 1024
#define WORKER_PROCESS_COUNT 3

struct aeEventLoop;
typedef struct _aeConnection
{
    int flags;
    int fd;
    char recv_buffer[10240];
    int  recv_length; //last recv length
    int  read_index;
    char* client_ip;
    int client_port;
}aeConnection;

typedef struct _aeServer aeServer;
typedef struct _aeReactor aeReactor;
//reactor结构体
struct _aeReactor
{
    void *object;
    void *ptr;  //reserve
    aeEventLoop *eventLoop;
    
    int epfd;
    int id;
    int event_num;
    int max_event_num;
    int running :1;
    
    aeConnection *socket_list;
    //对事件操作
    int (*add)(aeReactor *, int fd, int fdtype);
    int (*set)(aeReactor *, int fd, int fdtype);
    int (*del)(aeReactor *, int fd);
    int (*wait)(aeReactor *, struct timeval *);
    void (*free)(aeReactor *);
    
    int (*write)(aeReactor *, int __fd, void *__buf, int __n);
    int (*close)(aeReactor *, int __fd);
};


typedef struct _aeWorkerInfo
{
    pid_t pid;
    int pipefd[2];
}aeWorkerInfo;

typedef struct _workerBase
{
    int   pidx; //主进程中分的编号0-x
    pid_t pid;
    int pipefd;
    int running;
    
}aWorkerBase;


typedef struct _aeReactorThread
{
    pthread_t thread_id;
    aeReactor reactor;
    //swLock lock;
} aeReactorThread;


struct _aeServer
{
    char* listen_ip;
    int listenfd;
    int   port;
    void *ptr2;
    int reactorNum;
    aeReactor* mainReactor;
    aeConnection* connlist;
    aeReactorThread *reactorThreads;
    //pthread_barrier_t barrier;
    
    int sigPipefd[2];
    
    int  (*send)(  int fd, char* data , int len );
    void (*close)( aeConnection *c  );
    void (*onConnect)( aeServer* serv ,int fd );
    void (*onRecv)( aeServer *serv, aeConnection* c , int len );
    void (*onClose)( aeServer *serv , aeConnection *c );
    void (*runForever )( aeServer* serv );
};


typedef struct _reactorThreadParam
{
    int thid;
    aeServer* serv;
}reactorThreadParam;


void initOnLoopStart( aeEventLoop *el );
void onReadableEvent(aeEventLoop *el, int fd, void *privdata, int mask);
void installWorkerProcess();
void runMasterLoop();
void masterSignalHandler( int sig );
void installMasterSignal( aeServer* serv );
aeServer* aeServerCreate( char* ip , int port );
int startServer( aeServer* serv );

//=============child process============

aeConnection *newConnection( aeEventLoop *el , int fd);
void readFromClient(aeEventLoop *el, int fd, void *privdata, int mask);
void initWorkerOnLoopStart( aeEventLoop *l);
void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask);
//======
void freeClient( aeConnection* c  );
//======
void onRecv( aeConnection *c , int len );
void onClose( aeConnection *c );
//int onTimer(struct aeEventLoop *l,long long id,void *data);
//======
void acceptCommonHandler( aeServer* serv ,int fd,char* client_ip,int client_port, int flags);
void readFromClient(aeEventLoop *el, int fd, void *privdata, int mask);
int timerCallback(struct aeEventLoop *l,long long id,void *data);
void finalCallback( struct aeEventLoop *l,void *data );
void addSignal( int sig, void(*handler)(int), int restart );
void runWorkerProcess( int pidx ,int pipefd );

aeServer*  servG;
aWorkerBase aWorker;
#endif
