
#include "include/aeserver.h"
aeServer* serv;

void appnetServerOnRecv( aeServer* s , aeConnection *c , int len );
void appnetServerOnClose( aeServer* s , aeConnection *c );
void appnetServerOnConnect( aeServer* s , int fd );
aeServer* appnetTcpServInit( char* listen_ip , int port  );

void appnetServerOnRecv( aeServer* s , aeConnection *c , int len )
{
     printf( "PHPD recv len=%d,data=%s,threadid=%d\n" ,strlen( c->recv_buffer ) , c->recv_buffer,pthread_self() );
     //s->send( c->fd , c->recv_buffer, strlen( c->recv_buffer ) );
     char* buff = "recv ok!";
     sendMessageToReactor( c->fd , buff , strlen( buff ));
}

void appnetServerOnClose( aeServer* s , aeConnection *c )
{
      printf( "PHPD close fd=%d,threadid=%d\n" , c->fd,pthread_self() );
}

void appnetServerOnConnect( aeServer* s , int fd )
{
     printf( "PHPD New Client Connected fd=%d,threadid=%d \n", fd,pthread_self()   );
     char* buff = "connect ok!"; 
  ///  sendMessageToReactor( fd , buff , strlen( buff ));
   // s->send( fd , buff , strlen( buff ) );
}


aeServer* appnetTcpServInit( char* listen_ip , int port  )
{
     serv = aeServerCreate( listen_ip , port );

	 //回调改为从reactor线程中回调，如果加上worker后，在worker中回调。
     serv->onConnect = 	&appnetServerOnConnect;
     serv->onRecv = 	&appnetServerOnRecv;
     serv->onClose = 	&appnetServerOnClose;
     return serv;
}

void appnetTcpServRun()
{
     serv->runForever( serv );
}

int main()
{
  aeServer* serv = appnetTcpServInit( "0.0.0.0" , 3011 );
  appnetTcpServRun();
return 0;
}
