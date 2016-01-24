
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
#define REACTOR_THREAD_COUNT 2

struct aeEventLoop;
typedef struct _aeConnection
{
  int flags;
  int fd;
  char recv_buffer[10240];
  char send_buffer[10240];
  int  recv_length; //last recv length
  int  read_index;
  int  send_index;
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
};


typedef struct _aeWorkerProcess
{
    pid_t pid;
    int pipefd[2];
}aeWorkerProcess;

typedef struct _aeWorker
{
	int   pidx; //主进程中分的编号0-x
	pid_t pid;
	int pipefd;
	int running;
	int maxClient;
	aeEventLoop *el;
}aeWorker;


typedef struct _aeReactorThread
{
    pthread_t thread_id;
    aeReactor reactor;
    //swLock lock;
} aeReactorThread;

typedef enum
{
	PIPE_EVENT_CONNECT = 1,
	PIPE_EVENT_MESSAGE,
	PIPE_EVENT_CLOSE,
}PipeEventType;	


struct _aeServer
{
   char* listen_ip;
   int listenfd;
   int   port;
   int   running;
   void *ptr2;
   int reactorNum;
   int workerNum;
   aeReactor* mainReactor;
   aeConnection* connlist;
   aeReactorThread *reactorThreads;
   //pthread_barrier_t barrier;
   aeWorkerProcess *workers; //主进程中保存的worker相当信息数组。
   aeWorker* worker;	//子进程中的全局变量,子进程是独立空间，所以只要一个标识当前进程
   int sigPipefd[2];
   int recvPipefd[2];

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


#define PIPE_DATA_LENG 8
#define PIPE_DATA_HEADER_LENG 1+2*sizeof(int)

#pragma pack(1)
typedef struct _aePipeData
{
	char type;
	int len;
	int connfd;
	char data[PIPE_DATA_LENG];
}aePipeData;



void initOnLoopStart( aeEventLoop *el );
void onReadableEvent(aeEventLoop *el, int fd, void *privdata, int mask);
void createWorkerProcess( aeServer* serv );
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
#endif
