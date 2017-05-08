#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <iostream>
#include <fstream>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>


const char* usage =
"usage: myhttpd [-f|-t|-p] [<port>]                             \n"
"                                                               \n"
"   -f: Create a new process for each request                   \n"
"   -t: Create a new thread for each request                    \n"
"   -p: Pool of threads                                         \n"
"                                                               \n";


pthread_mutex_t mutex;
int QueueLength = 5;

extern "C" void killzombie(int sig)
{
    int pid = wait3(0, 0, NULL);
    while(waitpid(-1, NULL, WNOHANG) > 0);
}




void processRequest(int fd) {
	const int size = 1024;
	char * name = (char*) malloc(1024);
	int length = 0;
	unsigned char newChar;
	unsigned char oldChar = 0;
	int n;
	int check = 0;

	while ( length < size && (n =  read(fd, &newChar, sizeof(newChar)))> 0 ) {
		if(check == 0) {
			name[length] = newChar;
			if(oldChar == '\015' && newChar == '\012') 
				check = 1;
			oldChar = newChar;
		} else if(check == 1) {
			name[length] = newChar;
			if(oldChar == '\015' && newChar == '\012') {
				oldChar = newChar;
				break;
			}
			else if(oldChar == '\012' && newChar == '\015')
				check = 1;
			else 
				check = 0;
			oldChar = newChar;
		}
		length++;

	}

	name[length] = '\0';
	char * path = (char*)malloc(1024);
	char * d = &path[0];
	int index = 0;
	int i = 4;
	while(name[i] != ' ') 
		path[index++] = name[i++];

	path[index] = '\0';
	const char * dir = strdup("http-root-dir");
	const char * normal = strdup("http-root-dir/htdocs");
	const char * base = strdup("http-root-dir/htdocs/index.html");
	char * newPath = (char*)malloc(1024);	

	if(strstr(path, "./icons") != NULL || strstr(path, "./htdocs") != NULL) {
		strcpy(newPath, dir);
		strcat(newPath, path);
	} else {
		if (path[1] == 0) 
			strcpy(newPath, base);
		else {
			strcpy(newPath, normal);
			strcat(newPath, path);
		}
	}

	int f = open(newPath, O_RDONLY);
	char * data = (char*)malloc(10000);

	if (f  < 0 || strchr(newPath, '.') == NULL) {
		const char *notFound = strdup("HTTP/1.0 404FileNotFound\r\n"
									  "Server text/html\r\n"
									  "Content-type: CS252 lab5\r\n"
									  "Files not found\n");
		write(fd, notFound, strlen(notFound));
		return;
	}

	char c;
	i = 0;
	while(read(f, &c, sizeof(c)) > 0)
		data[i++] = c;

	data[i] = '\0';

	const char* reply1 = "HTTP/1.0 200 OK\n";
	const char* reply2 = "Server: CS252 lab5\n";
	const char* reply3;

	d = &path[0];
	while(*d != '.')
		d++;
	d++;
	if(*d == 'g')
		reply3 = "Content-type: image/gif \r\n\r\n";
	else if(*d == 'h')
		reply3 = "Content-type: text/html \r\n\r\n";
	else
		reply3 = "Content-type: text/plain \r\n\r\n";
	
	write(fd, reply1, strlen(reply1));
	write(fd, reply2, strlen(reply2));
	write(fd, reply3, strlen(reply3));

	write(fd, data, i);
}




void processRequestThread (int socket) {
	processRequest(socket);
	close(socket);
}

void poolSlave(int socket) {
	while(1) {
		struct sockaddr_in clientIP;
		int alen = sizeof(clientIP);
		pthread_mutex_lock(&mutex);
		int slave = accept(socket, (struct sockaddr*)&clientIP, (socklen_t*)&alen);
		pthread_mutex_unlock(&mutex);
		if (slave < 0) {
			perror("accept");
			exit(-1);
		}
		processRequest(slave);
		close(slave);
	}
}





int main( int argc, char **argv) {
    
    int port = 0;
	int concurrency = 0;

	if(argc == 1) 
		port = 9999;
	else if (argc == 2) 
		port = atoi(argv[1]);
	else if (argc == 3) {
		if (!strcmp(argv[1], "-f"))
			concurrency = 1;
		else if (!strcmp(argv[1], "-t"))
			concurrency = 2;
		else if (!strcmp(argv[1], "-p"))
			concurrency = 3;
		else {
			fprintf(stderr, "%s", usage);
        	exit(-1);
		}
		port = atoi(argv[2]);
	}
	else {
		fprintf(stderr, "%s", usage);
        exit(-1);
	}

	struct sigaction signalAction;
	signalAction.sa_handler = killzombie;
    sigemptyset(&signalAction.sa_mask);
    signalAction.sa_flags = SA_RESTART;

	int error1 = sigaction(SIGCHLD, &signalAction, NULL);
    if (error1) 
    {
        perror("sigaction");
        exit(-1);
    }

	struct sockaddr_in serverIP; 
    memset(&serverIP, 0, sizeof(serverIP));
    serverIP.sin_family = AF_INET;
    serverIP.sin_addr.s_addr = INADDR_ANY;
    serverIP.sin_port = htons((u_short) port);

	int masterSocket =  socket(PF_INET, SOCK_STREAM, 0);
    if (masterSocket < 0)
    {
        perror("socket");
        exit(-1);
    }

	int optval = 1; 
    int err = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, (char*) &optval, sizeof(int));

	int error = bind(masterSocket, (struct sockaddr *)&serverIP, sizeof(serverIP));
    if (error)
    {
        perror("bind");
        exit(-1);
    }

	error = listen(masterSocket, QueueLength);
    if (error)
    {
        perror("listen");
        exit(-1);
    }

	if (concurrency == 3) {
		pthread_t t[5];
		pthread_attr_t attr;
		pthread_attr_init( &attr );
		pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
		pthread_mutex_init(&mutex, NULL);
		for (int i = 0; i < 5; i++)
			pthread_create( &t[i], NULL, (void * (*)(void *))poolSlave, (void*)masterSocket);
		for (int i = 0; i < 5; i++) 
			pthread_join(t[i], NULL);
	} else {
		while(1) {
			struct sockaddr_in clientIP;
            int alen = sizeof(clientIP);
            int slave = accept(masterSocket, (struct sockaddr *)&clientIP, (socklen_t*)&alen);
			if (slave == -1 && errno == EINTR) {
				perror("accept");
				exit(-1);
			}


			if(concurrency == 0) {
				processRequest(slave);
				close(slave);
			} else if(concurrency == 1) {
				int pid = fork();
				if (pid == 0) {
					processRequest(slave);
					close(slave);
					exit(1);
				}
				close(slave);
			} else if(concurrency == 2) {
				pthread_t t;
				pthread_attr_t attr;
				pthread_attr_init( &attr );
				pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
				pthread_create( &t, NULL, (void * (*)(void *))poolSlave, (void*)masterSocket);
			}
		}
	}


}




