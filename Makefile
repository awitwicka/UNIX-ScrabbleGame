COMMON_FILES = common.c


all: client  server
client: client.c ${COMMON_FILES}	
	gcc -Wall -o client client.c ${COMMON_FILES}
server: server.c ${COMMON_FILES}	
	gcc -Wall -o server server.c ${COMMON_FILES}
.PHONY: clean
clean:
	rm client server
