
//https://github.com/lchb369/Aenet.git

/*************************************************************************
 多线程的网络IO，多进程的任务处理服务器。
 --------------------master------------------
 
 1,主进程listen到一个端口上
 2,创建M对用于与worker通信的pipe，创建M个woker进程.
 3,创建N个reactor线程，reactor是event loop的封装，并提供回调函数。
 线程数量可配置,将线程数组保存在全局变量中.
 4,收到新连接，将收到连接的消息和fd发给worker，可用fd%N确定发给哪个worker，并记录下workerid
 5,收到客户端消息,将收到连接的消息和fd发给相应id的worker
 6,收到关闭消息，也通知给worker
 
 --------------------worker------------------
 1,worker进程是用来处理逻辑的。
 2,worker进程的消息来源于pipe,是由主进程中的某个线程reactor发过来的
 3,worker进程只接收pipe事件，所以初始化时，就要将pipe加入event loop中
 4,发送消息，也要通过pipe先发送给主进程中相应的reactor,主进程将其发送到客户端
 5,关闭连接也是一样的，通过pipe发送给主进程中的reactor,告诉他要关闭连接fd。
 6,worker在收到消息时将其设为忙状态，处理完将其设为闲。以供reactor调度。
 这个变量需要放在共享内存中
 
 --------------------------------------
 1,首先开发一个多线程的网络io事件库。ReactorThread
 2,再开发一个多进程的任务处理程序.   WorkerProcess
 
 
 aeReactor结构体
 
 
 ***************************************************************************/

#include <stdio.h>
#include <stddef.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include "../include/ae.h"
#include "../include/anet.h"
#include <errno.h>
#include <sys/socket.h>
#include "../include/aeserver.h"

#include <unistd.h>
//aEventBase aEvBase;
//aWorkerBase aWorker;

void initOnLoopStart(struct aeEventLoop *el)
{
    puts("initOnLoopStart \n");
}


void onSignEvent( aeEventLoop *el, int fd, void *privdata, int mask)
{
    printf( "onSignEvent....\n");
}


void freeClient( aeConnection* c  )
{
    if (c->fd != -1)
    {
        int reactor_id = c->fd%servG->reactorNum;
        aeEventLoop* el = servG->reactorThreads[reactor_id].reactor.eventLoop;
        
        aeDeleteFileEvent( el ,c->fd,AE_READABLE);
        aeDeleteFileEvent( el,c->fd,AE_WRITABLE);
        close(c->fd);
    }
    //zfree(c);
}


void readFromClient(aeEventLoop *el, int fd, void *privdata, int mask)
{
    printf( "readfromclient fd=%d,threadid=%d.........\n" , fd,pthread_self() );
    aeServer* serv = servG;
    int nread, readlen,bufflen;
    readlen = 1024;
    aeConnection* c = &serv->connlist[fd];
    
    memset( c->recv_buffer , 0 , sizeof(c->recv_buffer) );
    c->recv_length = 0;
    //if there use "read" need loop
    nread = recv(fd, c->recv_buffer , readlen , MSG_WAITALL );
    if (nread == -1) {
        printf( "nread=%d \n" , nread );
        if (errno == EAGAIN) {
            nread = 0;
        } else {
            serv->onClose( serv,  &serv->connlist[fd] );
            return;
        }
    } else if (nread == 0) {
        printf( "nread=%d,errno=%d \n" , nread,errno );
        serv->onClose( serv ,  &serv->connlist[fd] );
        return;
    }
    
    c->recv_length = nread;
    serv->onRecv( serv, &serv->connlist[fd] ,nread );
}


void acceptCommonHandler( aeServer* serv ,int fd,char* client_ip,int client_port, int flags)
{
    serv->connlist[fd].client_ip = client_ip;
    serv->connlist[fd].client_port = client_port;
    serv->connlist[fd].flags |= flags;
    serv->connlist[fd].fd = fd;
    serv->onConnect( serv , fd );
    if (fd != -1) {
        anetNonBlock(NULL,fd);
        anetEnableTcpNoDelay(NULL,fd);
        
        int reactor_id = fd%serv->reactorNum;
        aeEventLoop* el = serv->reactorThreads[reactor_id].reactor.eventLoop;
        if (aeCreateFileEvent( el ,fd,AE_READABLE,readFromClient, fd ) == -1 )
        {
            printf( "CreateFileEvent error fd =%d,errno=%d,errstr=%s  \n" ,fd  , errno, strerror( errno )  );
            close(fd);
        }
    }
}

void onAcceptEvent( aeEventLoop *el, int fd, void *privdata, int mask)
{
    if( servG->listenfd == fd )
    {
        int client_port, connfd, max = 10;
        char client_ip[46];
        char neterr[1024];
        while(max--)//TODO::
        {
            connfd = anetTcpAccept( neterr, fd , client_ip, sizeof(client_ip), &client_port );
            if ( connfd == -1 ) {
                if (errno != EWOULDBLOCK)
                    printf("Accepting client Error connection: %s \n", neterr);
                return;
            }
            printf("Accepted a new connect=%d \n", connfd );
            acceptCommonHandler( servG , connfd,client_ip,client_port,0 );
        }
    }
}


void dispatchConn2ThreadReactor()
{
    
}


void runMainReactor( aeServer* serv )
{
    int res;
    //listenfd event,主进程主线程监听连接事件
    res = aeCreateFileEvent( serv->mainReactor->eventLoop,
                            serv->listenfd,
                            AE_READABLE,
                            onAcceptEvent,
                            NULL
                            );
    
    printf("master create file event is ok? [%d]\n",res==0 );
    //timer event
    //res = aeCreateTimeEvent( aEvBase.el,5*1000,timerCallback,NULL,finalCallback);
    //printf("master create time event is ok? [%d]\n",!res);
    
    aeMain( serv->mainReactor->eventLoop );
    aeDeleteEventLoop( serv->mainReactor->eventLoop );
}


//此处send是发给了主进程的event_loop，而不是发给子进程的。
void masterSignalHandler( int sig )
{
    printf( "Master Send Sigal to Master Loop...\n");
    int save_errno = errno;
    int msg = sig;
    //send( aEvBase.sig_pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}



void addSignal( int sig, void(*handler)(int), int restart  )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    if( restart == 1 )
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}


//这里的信号是server管理员执行的中断等操作引起的事件。
//所以此处的addEvent是加入到主进程event_loop中的。
void installMasterSignal( aeServer* serv )
{
    int ret;
    char neterr[1024];
    ret  = socketpair( PF_UNIX, SOCK_STREAM, 0, serv->sigPipefd );
    assert( ret != -1 );
    anetNonBlock( neterr, serv->sigPipefd[1] );
    
    //把信号管道一端加到master event_loop中，使其被epoll关注
    ret = aeCreateFileEvent( serv->mainReactor->eventLoop ,
                            serv->sigPipefd[0],AE_READABLE,onSignEvent,NULL);
    
    //装载信号，指定回调函数,如果用户引发信号事件，则回调。
    addSignal( SIGCHLD, masterSignalHandler , 1 );	//catch child process exit event
    addSignal( SIGTERM, masterSignalHandler , 1 );  //catch exit event by kill or Ctrl+C ..
    addSignal( SIGINT,  masterSignalHandler , 1 );
    //     addSignal( SIGQUIT , masterSignalHandler, 1 );
    addSignal( SIGPIPE, SIG_IGN , 1 );
}

aeServer* aeServerCreate( char* ip,int port )
{
    aeServer* serv = (aeServer*)zmalloc( sizeof(aeServer ));
    //aeServer serv;
    serv->runForever = startServer;
    
    serv->send = anetWrite;
    serv->close = freeClient;
    serv->listen_ip = ip;
    serv->port = port;
    serv->connlist = shm_calloc( 1024 , sizeof( aeConnection ));
    
    serv->reactorNum = 2;
    //    serv->reactorThreads = (aeReactorThread*)zmalloc( serv->reactorNum * sizeof( aeReactorThread ) );
    serv->reactorThreads = zmalloc( serv->reactorNum * sizeof( aeReactorThread  ));
    serv->mainReactor = zmalloc( sizeof( aeReactor ));
    serv->mainReactor->eventLoop = aeCreateEventLoop( 10 );
    aeSetBeforeSleepProc( serv->mainReactor->eventLoop ,initOnLoopStart );
    
    printf( "Main reactor event loop addr=%x,threadid=%d \n" , serv->mainReactor->eventLoop , pthread_self() );
    //install signal
    //installMasterSignal( serv  );
    
    servG = serv;
    return serv;
}
void *reactorThreadRun(void *arg);
//reactor线程,
//创建子线程
//并在每个子线程中创建一个reactor/eventloop,放到全局变量中
void createReactorThreads( aeServer* serv  )
{
    int i,res;
    pthread_t threadid;
    void *thread_result;
    aeReactorThread *thread;
    
    
    //	pthread_barrier_init(&serv->barrier, NULL, serv->reactorNum+1 );
    for( i=0;i<serv->reactorNum;i++)
    {
        thread = &(serv->reactorThreads[i]);
        
        reactorThreadParam* param = zmalloc( sizeof( reactorThreadParam ));
        param->serv = serv;
        param->thid = i;
        
        res = pthread_create(&threadid, NULL, reactorThreadRun , (void *)param );
        if (res != 0)
        {
            perror("Thread creat failed!");
            exit(0);
        }
        thread->thread_id = threadid;
        printf( "create thread id=%d,i=%d \n" , thread->thread_id,i );
    }
    //	printf( "pthread_barrier_wait start........\n");
    //	pthread_barrier_wait(&serv->barrier);
    //	printf( "pthread_barrier_wait end........\n");
    
}

aeReactorThread getReactorThread( aeServer* serv, int i )
{
    return (aeReactorThread)(serv->reactorThreads[i]);
}


void *reactorThreadRun(void *arg)
{
    printf( "reactorThreadRun.........\n");
    //aeServer* serv = (aeServer*)arg;
    reactorThreadParam* param = (reactorThreadParam*)arg;
    aeServer* serv = param->serv;
    int thid = param->thid;
    aeEventLoop* el = aeCreateEventLoop( 100 );
    serv->reactorThreads[thid].reactor.eventLoop = el;
    
    
    printf( "threadReactor size=%d,el addr=%x,threadid=%d,thid=%d \n" , el->setsize,el,  pthread_self(),thid  );
    aeSetBeforeSleepProc( el ,initOnLoopStart );
    aeMain(  el );
    aeDeleteEventLoop( el );
    //pthread_exit("reactor thread run over \n");
}


void shutDown( aeServer * serv )
{
    puts("Master Exit ,Everything is ok !!!\n");
    //shm_free
    shm_free( serv->connlist , 1 );
    //pthread join
    
    //waitpid
    
    //zfree
    if( serv != NULL )
    {
        zfree( serv );
    }
}

int startServer( aeServer* serv )
{
    int sockfd[2];
    int sock_count = 0;
    
    listenToPort( serv->listen_ip, serv->port , sockfd , &sock_count );
    serv->listenfd = sockfd[0];
    
    printf( "master listen count %d,listen fd %d \n",sock_count,serv->listenfd );
    //installWorkerProcess();
    
    //创建子线程
    printf( "createReactorThreads....\n");
    createReactorThreads( serv );
    
    //运行主reactor
    printf( "runMainReactor....\n");
    runMainReactor( serv );
    
    shutDown( serv );
    return 0;
}
