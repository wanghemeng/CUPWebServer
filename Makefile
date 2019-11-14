all: client webserver

client: client.c
	gcc -E client.c -o client.i
	gcc -S client.i -o client.s
	gcc -c client.s -o client.o
	gcc client.o -o client

webserver: webserver.c
	gcc -E webserver.c -o webserver.i
	gcc -S webserver.i -o webserver.s
	gcc -c webserver.s -o webserver.o
	gcc webserver.o -o webserver

start: webserver
	./webserver 8088 /Users/Jarvis/Desktop/WebServer

clean:
	rm *.i *.s *.o client webserver *.log

