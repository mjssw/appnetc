
//https://github.com/lchb369/Aenet.git


#include <stdio.h>
#include <stddef.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include "ae.h"
#include <errno.h>
#include <sys/socket.h>
#include "aeserver.h"

//======================
void initWorkerOnLoopStart( aeEventLoop *l)
{
    //   puts("worker Event Loop Init!!! \n");
}


void sendMessageToReactor( int connfd , char* buff , int len )
{
    memset( servG->connlist[connfd].send_buffer , 0 , sizeof( servG->connlist[connfd].recv_buffer ) );
    memcpy( servG->connlist[connfd].send_buffer , buff , len );
    aePipeData data;
    data.type = PIPE_EVENT_MESSAGE;
    data.connfd = connfd;
    data.len = len;
    send2ReactorThread( connfd , data );
}

void sendCloseEventToReactor( int connfd  )
{
    aePipeData data;
    data.type = PIPE_EVENT_CLOSE;
    data.connfd = connfd;
    data.len = 0;
    send2ReactorThread( connfd , data );
}


int socketWrite(int __fd, void *__data, int __len)
{
    int n = 0;
    int written = 0;
    
    while (written < __len)
    {
        n = write(__fd, __data + written, __len - written);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            else if (errno == EAGAIN)
            {
                continue;
            }
            else
            {
                printf("write %d bytes failed.", __len);
                return AE_ERR;
            }
        }
        written += n;
    }
    return written;
}


int send2ReactorThread( int connfd , aePipeData data )
{
    int sendlen;
    int pipefd =  servG->worker->pipefd;
    sendlen = socketWrite( pipefd , &data , sizeof( data ) );
    if( sendlen < 0 )
    {
        printf( "send2ReactorThread error errno=%d \n" , errno );
    }
}


void recvFromPipe( aeEventLoop *el, int fd, void *privdata, int mask )
{
    int readlen =0;
    char buf[PIPE_DATA_LENG+PIPE_DATA_HEADER_LENG];
    
    //此处如果要把数据读取到大于包长的缓冲区中，不要用anetRead，否则就掉坑里了
    readlen = anetRead( fd, buf, sizeof( buf )  );
    if( readlen == 0 )
    {
        //printf( "worker read from pipe close event fd=%d...\n" , fd );
        //close( fd );
    }
    else if( readlen > 0 )
    {
        aePipeData data;
        memcpy( &data , &buf ,sizeof( data ) );
        //printf( "Worker Recv pipefd=%d, pid=%d,readlen=%d,connfd=%d...\n" , fd , getpid(),readlen , data.connfd );
        //connect,read,close
        if( data.type == PIPE_EVENT_CONNECT )
        {
            //printf( "Worker Recv New Connect fd=%d...\n" , data.connfd );
            if( servG->onConnect )
            {
                servG->onConnect( servG , data.connfd );
            }
        }
        else if( data.type == PIPE_EVENT_MESSAGE )
        {
            if( servG->onRecv )
            {
                servG->onRecv( servG , &servG->connlist[data.connfd] , data.len  );
            }
        }
        else if( data.type == PIPE_EVENT_CLOSE )
        {
            if( servG->onClose )
            {
                servG->onClose( servG , &servG->connlist[data.connfd] );
            }
        }
        else
        {
            printf( "recvFromPipe recv unkown data.type=%d" , data.type );
        }
    }
}

int timerCallback(struct aeEventLoop *l,long long id,void *data)
{
    printf("I'm time_cb,here [EventLoop: %p],[id : %lld],[data: %p] \n",l,id,data);
    return 5*1000;
}

void finalCallback(struct aeEventLoop *l,void *data)
{
    puts("call the unknow final function \n");
}

void childTermHandler( int sig )
{
    printf( "Worker Recv Int Signal...\n");
    servG->worker->running = 0;
    aeStop( servG->worker->el );
}

void childChildHandler( int sig )
{
    printf( "Worker Recv Child Signal...\n");
    pid_t pid;
    int stat;
    while ( ( pid = waitpid( -1, &stat, WNOHANG ) ) > 0 )
    {
        continue;
    }
}

/**
 * process event types:
 * 1,parent process send readable event
 */
void runWorkerProcess( int pidx ,int pipefd )
{
    printf( "run worker process...\n");
    //每个进程私有的。
    aeWorker* worker = zmalloc( sizeof( aeWorker ));
    worker->pid = getpid();
    worker->maxClient=1024;
    worker->pidx = pidx;
    worker->pipefd = pipefd;
    worker->running = 1;
    servG->worker = worker;
    
    //这里要安装信号接收器..
    addSignal( SIGTERM, childTermHandler, 0 );
    addSignal( SIGCHLD, childChildHandler , 1 );
    
    worker->el = aeCreateEventLoop( worker->maxClient );
    aeSetBeforeSleepProc( worker->el,initWorkerOnLoopStart );
    int res;
    
    printf( "Worker listen Pipefd = %d \n" , worker->pipefd );
    
    //监听父进程管道事件
    res = aeCreateFileEvent( worker->el,
                            worker->pipefd,
                            AE_READABLE,
                            recvFromPipe,NULL
                            );
    
    printf("Worker pid=%d create file event is ok? [%d]\n",worker->pid,res==0 );
    
    //定时器
    //res = aeCreateTimeEvent(el,5*1000,timerCallback,NULL,finalCallback);
    //printf("create time event is ok? [%d]\n",!res);
    
    aeMain(worker->el);
    aeDeleteEventLoop(worker->el);
    close( pipefd );
    zfree( worker );    
    shm_free( servG->connlist , 0 );
    printf( "Worker pid=%d exit...\n" , worker->pid );
}
