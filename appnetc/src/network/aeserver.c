
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

void readFromWorker( aeEventLoop *el, int fd, void *privdata, int mask);

void initOnLoopStart(struct aeEventLoop *el)
{
    printf("initOnLoopStart threadid=%d \n" , pthread_self() );
}

void initThreadOnLoopStart( struct aeEventLoop *el )
{
    //printf("initThreadOnLoopStart threadid=%d \n" , pthread_self() );
}


//异步信号事件
void onSignEvent( aeEventLoop *el, int fd, void *privdata, int mask)
{
    if( fd == servG->sigPipefd[0] )
    {
        printf( "Recv Master Siginal fd=%d...\n" , fd );
        int sig,ret,i;
        char signals[1024];
        ret = recv( servG->sigPipefd[0], signals, sizeof( signals ), 0 );
        if( ret == -1 )
        {
            return;
        }
        else if( ret == 0 )
        {
            return;
        }
        else
        {
            for(  i = 0; i < ret; ++i )
            {
                switch( signals[i] )
                {
                        //child process stoped
                    case SIGCHLD:
                    {
                        printf( "Master recv child process stoped signal\n");
                        pid_t pid;
                        int stat,pidx;
                        //WNOHANG
                        while ( ( pid = waitpid( -1, &stat, 0 ) ) > 0 )
                        {
                            for( pidx = 0; pidx < servG->workerNum; pidx++ )
                            {
                                if( servG->workers[pidx].pid == pid )
                                {
                                    //aeDeleteFileEvent( aEvBase.el,aEvBase.worker_process[i].pipefd[0] ,AE_READABLE );
                                    close( servG->workers[pidx].pipefd[0] );
                                    servG->workers[pidx].pid = -1;
                                }
                            }
                        }
                        servG->running = 0;
                        //停止主循环
                        aeStop( servG->mainReactor->eventLoop );
                        
                        break;
                    }
                    case SIGTERM:
                    case SIGQUIT:
                    case SIGINT:
                    {
                        int i;
                        printf( "master kill all the clild now\n" );
                        for( i = 0; i < servG->workerNum; i++ )
                        {
                            int pid = servG->workers[i].pid;
                            //alarm/kill send signal to process,
                            //in child process need install catch signal functions;
                            kill( pid, SIGTERM );
                        }
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
            }
        }
    }
}

//关闭连接，并移除监听事件
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


void onReadDataFromClient( aeServer* serv , aeConnection* conn , int len  )
{
    if( len <= 0 )
    {
        return;
    }
    
    //printf( "onReadDataFromClient len=%d,data=%s,threadid=%d\n" ,strlen( conn->recv_buffer ) , conn->recv_buffer,pthread_self() );
    aePipeData  data = {0};
    data.type = PIPE_EVENT_MESSAGE;
    data.connfd = conn->fd;
    data.len = strlen( conn->recv_buffer );
    reactorSend2Worker( serv , conn->fd , data );
}

void onCloseByClient( aeServer* serv , aeConnection* conn  )
{
    if( conn == NULL )
    {
        return;
    }
    
    aePipeData  data = {0};
    data.type = PIPE_EVENT_CLOSE;
    data.connfd = conn->fd;
   	data.len = 0;
    
    reactorSend2Worker( serv , conn->fd , data );
    freeClient( conn );
}



//接收客户端的消息
void readFromClient(aeEventLoop *el, int fd, void *privdata, int mask)
{
    aeServer* serv = servG;
    int nread, readlen,bufflen;
    readlen = 1024;
    aeConnection* c = &serv->connlist[fd];
    
    memset( c->recv_buffer , 0 , sizeof(c->recv_buffer) );
    c->recv_length = 0;
    
    //if there use "read" need loop
    nread = recv(fd, c->recv_buffer , readlen , MSG_WAITALL );
    if (nread == -1) {
        if (errno == EAGAIN) {
            nread = 0;
            return;
        }
        else
        {
            onCloseByClient( serv,  &serv->connlist[fd] );
            return;
        }
    }
    else if (nread == 0)
    {
        onCloseByClient( serv ,  &serv->connlist[fd] );
        return;
    }
    
    c->recv_length = nread;
    onReadDataFromClient( serv, &serv->connlist[fd] ,nread );
}


//通过pipe发给子进程,如果多个线程同时发给同一个worker,要加锁，每个worker一个锁。
int reactorSend2Worker(  aeServer* serv , int fd , aePipeData data )
{
    int sendlen = 0;
    int workerid = fd % serv->workerNum;

	//此处可以考虑换成pthread_spin_lock
	pthread_mutex_lock( &serv->workers[workerid].w_mutex );
    sendlen = anetWrite( serv->workers[workerid].pipefd[0], &data , sizeof( aePipeData ) );
    pthread_mutex_unlock( &serv->workers[workerid].w_mutex );

    //printf( "reactorSend2Worker send len=%d,errno=%d,errstr=%s,fd=%d,workerid=%d \n" , sendlen ,errno, strerror(errno),fd,workerid );
    return sendlen;
}


void readFromWorkerPipe( aeEventLoop *el, int fd, void *privdata, int mask)
{
    int readlen =0;
    char buf[10240];
    
	int workerid = fd % servG->workerNum;
	pthread_mutex_lock( &servG->workers[workerid].r_mutex );
    readlen = read( fd, &buf, sizeof( buf ) ); //
	pthread_mutex_unlock( &servG->workers[workerid].r_mutex );
	
    if( readlen == 0 )
    {
        close( fd );
    }
    else if( readlen > 0 )
    {
        aePipeData data;
        memcpy( &data , &buf ,sizeof( data ) );
        
        //message,close
        if( data.type == PIPE_EVENT_MESSAGE )
        {
            if( servG->sendToClient )
            {
                servG->sendToClient( data.connfd , servG->connlist[data.connfd].send_buffer , data.len  );
            }
        }
        else if( data.type == PIPE_EVENT_CLOSE )
        {
            //close client socket
            if( servG->closeClient )
            {
                servG->closeClient( &servG->connlist[data.connfd] );
            }
        }
        else
        {
            printf( "recvFromPipe recv unkown data.type=%d" , data.type );
        }
    }
    else
    {
        if( errno == EAGAIN )
        {
            return;
        }
        else
        {
            printf( "Reactor Recv errno=%d,errstr=%s \n" , errno , strerror( errno ));
        }
    }
}



void acceptCommonHandler( aeServer* serv ,int fd,char* client_ip,int client_port, int flags)
{
    serv->connlist[fd].client_ip = client_ip;
    serv->connlist[fd].client_port = client_port;
    serv->connlist[fd].flags |= flags;
    serv->connlist[fd].fd = fd;
    //serv->onConnect( serv , fd );
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
    aePipeData  data = {0};
    data.type = PIPE_EVENT_CONNECT;
    data.connfd = fd;
    data.len = 0;
    reactorSend2Worker( serv , fd , data );
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
            //printf("Accepted a new connect=%d \n", connfd );
            acceptCommonHandler( servG , connfd,client_ip,client_port,0 );
        }
    }
    else
    {
        printf( "onAcceptEvent other fd=%d \n" , fd );
    }
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
    
    aeMain( serv->mainReactor->eventLoop );
    aeDeleteEventLoop( serv->mainReactor->eventLoop );
}


//此处send是发给了主进程的event_loop，而不是发给子进程的。
void masterSignalHandler( int sig )
{
    printf( "Master Send Sigal to Master Loop...\n");
    int save_errno = errno;
    int msg = sig;
    send( servG->sigPipefd[1], ( char* )&msg, 1, 0 );
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


//这里的信号是终端执行的中断等操作引起的事件。
//所以此处的addEvent是加入到主进程event_loop中的。
void installMasterSignal( aeServer* serv )
{
    int ret;
    char neterr[1024];
    
    //这个管道不是与worker通信的，而是与主线程的自己通信的
    ret  = socketpair( PF_UNIX, SOCK_STREAM, 0, serv->sigPipefd );
    assert( ret != -1 );
    anetNonBlock( neterr, serv->sigPipefd[1] );
    
    //把信号管道一端加到master event_loop中，使其被epoll关注
    ret = aeCreateFileEvent( serv->mainReactor->eventLoop ,
                            serv->sigPipefd[0],AE_READABLE,onSignEvent,NULL);
    
    //装载信号，指定回调函数,如果用户引发信号事件，则回调。
    addSignal( SIGCHLD, masterSignalHandler , 1 );	//catch child process exit event
    //addSignal( SIGTERM, masterSignalHandler , 1 );  //catch exit event by kill or Ctrl+C ..
    addSignal( SIGINT,  masterSignalHandler , 1 );
    addSignal( SIGPIPE, SIG_IGN , 1 );
}


aeServer* aeServerCreate( char* ip,int port )
{
    aeServer* serv = (aeServer*)zmalloc( sizeof(aeServer ));
    serv->runForever = startServer;
	serv->send =  sendMessageToReactor;
    serv->close = sendCloseEventToReactor;
	serv->sendToClient = anetWrite;
	serv->closeClient = freeClient;
    serv->listen_ip = ip;
    serv->port = port;
    serv->reactorNum = 2;
    serv->workerNum = 3;
    serv->connlist = shm_calloc( 1024 , sizeof( aeConnection ));
    serv->reactorThreads = zmalloc( serv->reactorNum * sizeof( aeReactorThread  ));
    serv->workers = zmalloc( serv->workerNum * sizeof(aeWorkerProcess));
    serv->mainReactor = zmalloc( sizeof( aeReactor ));
    
    serv->mainReactor->eventLoop = aeCreateEventLoop( 10 );
    aeSetBeforeSleepProc( serv->mainReactor->eventLoop ,initOnLoopStart );
    printf( "Main reactor event loop addr=%x,threadid=%d \n" , serv->mainReactor->eventLoop , pthread_self() );
    
    //安装信号装置
    installMasterSignal( serv  );
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
        thread->param = zmalloc( sizeof( reactorThreadParam ));
        thread->param->serv = serv;
        thread->param->thid = i;
        
        res = pthread_create(&threadid, NULL, reactorThreadRun , (void *)thread->param );
        if (res != 0)
        {
            perror("Thread creat failed!");
            exit(0);
        }
        thread->thread_id = threadid;
        printf( "create thread id=%d,i=%d \n" , thread->thread_id,i );
    }
}

aeReactorThread getReactorThread( aeServer* serv, int i )
{
    return (aeReactorThread)(serv->reactorThreads[i]);
}


void *reactorThreadRun(void *arg)
{
    reactorThreadParam* param = (reactorThreadParam*)arg;
    aeServer* serv = param->serv;
    int thid = param->thid;
    aeEventLoop* el = aeCreateEventLoop( 100 );
    serv->reactorThreads[thid].reactor.eventLoop = el;
    
    //注册worker管道事件,与每个worker的pipe都要注册
    int ret,i;
    for(  i = 0; i < serv->workerNum; i++ )
    {
        if ( aeCreateFileEvent( el,serv->workers[i].pipefd[0],
                               AE_READABLE,readFromWorkerPipe, NULL ) == -1 )
        {
            printf( "CreateFileEvent error fd "  );
            close(serv->workers[i].pipefd[0]);
        }
    }
    
    //printf( "threadReactor size=%d,el addr=%x,threadid=%d,thid=%d \n" , el->setsize,el,  pthread_self(),thid  );
    aeSetBeforeSleepProc( el ,initThreadOnLoopStart );
    aeMain(  el );
    aeDeleteEventLoop( el );
	el = NULL;
    //printf( "thread end loop id=%d \n" , thid );
}


int socketSetBufferSize(int fd, int buffer_size)
{
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size)) < 0)
    {
        printf("setsockopt(%d, SOL_SOCKET, SO_SNDBUF, %d) failed.", fd, buffer_size);
        return AE_ERR;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size)) < 0)
    {
        printf("setsockopt(%d, SOL_SOCKET, SO_RCVBUF, %d) failed.", fd, buffer_size);
        return AE_ERR;
    }
    return AE_OK;
}

void createWorkerProcess( aeServer* serv )
{
    char* neterr;
    int ret,i;
    for(  i = 0; i < serv->workerNum; i++ )
    {
		//init mutex
		pthread_mutex_init( &(serv->workers[i].r_mutex) ,NULL);
		pthread_mutex_init( &(serv->workers[i].w_mutex) ,NULL);
		
        ret = socketpair( PF_UNIX, SOCK_STREAM, 0, serv->workers[i].pipefd );
        assert( ret != -1 );
        serv->workers[i].pid = fork();
        if( serv->workers[i].pid < 0 )
        {
            continue;
        }
        else if( serv->workers[i].pid > 0 )
        {
            //父进程
            close( serv->workers[i].pipefd[1] );
            anetNonBlock( neterr , serv->workers[i].pipefd[0] );
            continue;
        }
        else
        {
            //子进程
            close( serv->workers[i].pipefd[0] );
            anetNonBlock( neterr, serv->workers[i].pipefd[1] );
            runWorkerProcess( i , serv->workers[i].pipefd[1]  );
            exit( 0 );
        }
    }
}

void stopReactorThread( aeServer* serv  )
{
    int i;
    for( i=0;i<serv->reactorNum;i++)
    {
        aeStop( serv->reactorThreads[i].reactor.eventLoop );
    }
    
    for( i=0;i<serv->reactorNum;i++)
    {
        usleep( 1000 );
        pthread_cancel( serv->reactorThreads[i].thread_id );
        if( pthread_join( serv->reactorThreads[i].thread_id , NULL ) )
        {
            printf( "pthread join error \n");
        }
    }
	
	//free memory
	for( i=0;i<serv->reactorNum;i++)
    {
		if( serv->reactorThreads[i].param  )
		{
			zfree( serv->reactorThreads[i].param );
		}
		
		if( serv->reactorThreads[i].reactor.eventLoop != NULL )
		{
		   aeDeleteEventLoop( serv->reactorThreads[i].reactor.eventLoop  );
		}
		
	}
}

void destroyServer( aeServer* serv )
{
    //1,停止,释放线程
    stopReactorThread( serv );
    
    //2,释放共享内存
    shm_free( serv->connlist,1 );
    
    //3,释放由zmalloc分配的内存
    if( serv->reactorThreads )
    {
        zfree( serv->reactorThreads );
    }
    if( serv->workers )
    {
        zfree( serv->workers );
    }		
    
    if( serv->mainReactor )
    {
        zfree( serv->mainReactor );
    }
    
    //4,最后释放这个全局大变量
    if( serv != NULL )
    {
        zfree( serv );
    }
    puts("Master Exit ,Everything is ok !!!\n");
}

int startServer( aeServer* serv )
{	
    int sockfd[2];
    int sock_count = 0;          
    
    //监听TCP端口，这个接口其实可以同时监听多个端口的。
    listenToPort( serv->listen_ip, serv->port , sockfd , &sock_count );
    serv->listenfd = sockfd[0];
    
    //创建进程要先于线程，否则，会连线程一起fork了。
    createWorkerProcess( serv );	
    
    //创建子线程,每个线程都监听所有worker管道
    createReactorThreads( serv );
    
    //运行主reactor
    runMainReactor( serv );
    
    //退出
    destroyServer( serv );
    return 0;
}
