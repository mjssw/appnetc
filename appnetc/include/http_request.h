


#ifndef __HTTP_REQUEST_H_
#define __HTTP_REQUEST_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "websocket.h"

#define AE_SPACE       ' '
#define AE_EOL_CRLF        "\r\n"
#define AE_HEADER_END	   "\r\n\r\n"
/*
#define AE_OK        0
#define AE_ERR       -1
*/

#define AE_HTTP_HEADER_MAX_SIZE          8192

//http://www.w3.org/Protocols/rfc2616/rfc2616.html
//��Ӧ����ٽ���ͷ������recv_buffer���Ƴ�
//����eol���ַ����е�λ�ã��Ҳ����ͼ������ա�
//���header̫�������ش��󣬶Ͽ�

//��s��ͷ����len�����Ȳ��ң�ÿ��ѭ�������128bytes

typedef struct
{
  char* str_pos;
  int   str_len;
}headerString;

typedef struct
{
	char key[64];
	char value[1024];
	int  buffer_pos;//�����Ŀ�ʼλ����buffer�е�ƫ����
}headerFiled;

//���һЩ������option������������û�еģ�����ȥfileds��ȥ��,��߷���Ч�ʡ�
enum
{
	HEADER_METHOD = 1,
	HEADER_URI,
	HEADER_VERSION
};

typedef struct
{
	char method[16];
	char version[16];
	char uri[AE_HTTP_HEADER_MAX_SIZE];
	//..
	
}headerParams;
#define TCP 0
#define HTTP 1
#define WEBSOCKET 2
typedef struct
{
	int connfd;
	int header_length;//header�����ܳ�
	int content_length;//body��
	int complete_length; //������
	int filed_nums; //headerFiled numbers
	int buffer_pos; //������λ��,��Ϊ��ʼλ��
	char method[8]; //
	char uri[1024];
	char version[16];
	int  protocol;
	headerFiled fileds[30]; 
	headerParams params;   //�����Ľ���ṹ��
	sds buffer;
}httpHeader;


typedef enum
{
	HRET_OK = 0,
	HRET_NOT_HTTP_REQUEST,
	HRET_UNCOMPLETE
}HttpRequestErrorType;	

int wesocketRequestRarse( int connfd , sds buffer , int len , httpHeader* header ,  handshake* hs );
#define CHUNK_SZ 128
static char* findEolChar( const char* s , int len );

static int getLeftEolLength( const char* s );

int bufferLineSearchEOL( httpHeader* header , 
	const char* buffer , int len , char* eol_style );

int bufferReadln( httpHeader* header , const char* buffer , 
	int len , char* eol_style );
	
char* findChar(  char sp_char , const char* dest , int len );

char* findSpace(  const char* s , int len );


char* findChar(  char sp_char , const char* dest , int len );
/*
by RFC2616
http://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html#sec5.1
The Request-Line begins with a method token, followed by the Request-URI and the protocol version, and ending with CRLF. 
The elements are separated by SP characters. No CR or LF is allowed except in the final CRLF sequence.
Request-Line   = Method SP Request-URI SP HTTP-Version CRLF
*/
static int parseFirstLine( httpHeader* header , const char* buffer , int len );
static int  readingHeaderFirstLine( httpHeader* header ,
	const char* buffer , int len );
static int readingHeaders( httpHeader* header , const char* buffer , int len );
int readingSingleLine(  httpHeader* header , const char* org , int len );
static char* getHeaderParams(  httpHeader* header , char* pkey );
int isHttpProtocol( char* buffer , int len );
void getGetMethodBody();
void getPostMethodBody();
int httpRequestParse( int connfd , sds buffer , int len  );
static int httpHeaderParse( httpHeader* header ,  sds buffer , int len );

#endif
