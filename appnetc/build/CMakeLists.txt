
cmake_minimum_required (VERSION 2.6)
project ( SockServer )

include_directories("../include/")
include_directories("../src/network/")
SET (HEADER_FILES
  ../include/aeserver.h 
  ../include/zmalloc.h 
  ../include/ae.h 
  ../include/anet.h 
  ../include/config.h
  ../include/share_memory.h
  ../include/ring_buffer.h
  ../include/sds.h
  ../include/http_request.h
  ../include/dict.h
  ../include/sha1.h
  ../include/websocket.h
  ../include/base64.h
)

SET (SOURCE_FILES
 ../socketserver.c
 ../src/network/aeserver.c 
 ../src/network/worker.c 
 ../src/network/zmalloc.c 
 ../src/network/ae.c 
 ../src/network/anet.c 
 ../src/network/ae_epoll.c
 ../src/network/share_memory.c
 ../src/network/ring_buffer.c
 ../src/network/sds.c
 ../src/network/http_request.c
 ../src/network/dict.c
 ../src/network/base64.c
 ../src/network/sha1.c
 ../src/network/websocket.c
)

ADD_EXECUTABLE( SockServer ${HEADER_FILES}  ${SOURCE_FILES} )
TARGET_LINK_LIBRARIES( SockServer pthread  )  
