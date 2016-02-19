
#include "include/aeserver.h"

aeServer* appnetTcpServInit( char* listen_ip , int port  );

void appnetServer_onRecv( aeServer* serv, aeConnection *conn, char* buff , int len )
{
	 printf( "PHPD len=%d,recv=[%s] \n" , len ,buff ); 
	 char* send_buff = "ww";
	 
	 serv->send( conn->fd , send_buff , strlen( send_buff ));
//	 serv->close( conn->fd );
}

void appnetServer_onClose( aeServer* serv , aeConnection *c )
{
      printf( "PHPD close fd=%d,threadid=%d\n" , c->fd,pthread_self() );
}

void appnetServer_onConnect( aeServer* serv ,int fd )
{
     printf( "PHPD New Client Connected fd=%d,threadid=%d \n", fd,pthread_self()   );
     char* buff = "connect ok!"; 
}


void appnetServer_onStart( aeServer* s  )
{
    printf( "appnet worker start\n");

}


void appnetServer_onFinal( aeServer* s  )
{
    printf( "appnet worker final\n");

}


aeServer* appnetServInit( char* listen_ip , int port  )
{
     aeServer* serv = aeServerCreate( listen_ip , port );
     serv->onConnect =  &appnetServer_onConnect;
     serv->onRecv =     &appnetServer_onRecv;
     serv->onClose =    &appnetServer_onClose;
     serv->onStart =    &appnetServer_onStart;
     serv->onFinal =    &appnetServer_onFinal;
     return serv;
}

void appnetServRun( aeServer* serv )
{
     serv->runForever( serv );
}


int main()
{
  aeServer* serv = appnetServInit( "0.0.0.0" , 3011 );
  appnetServRun( serv );
return 0;
}
