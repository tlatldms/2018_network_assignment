#define _CRT_SECURE_NO_WARNINGS
#define CRLF "\r\n"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <pthread.h>
////server.c

char video_packet[256]="HTTP/1.1 200 OK\r\nContent-Type: video/*\r\nConnection: keep-alive\r\nKeep-Alive: timeout=30, max=200\r\n\r\n";
char image_packet[256]="HTTP/1.1 200 OK\r\nContent-Type: image/*\r\nConnection: keep-alive\r\nKeep-Alive: timeout=30, max=200\r\n\r\n";
char others_packet[256]="HTTP/1.1 200 OK\r\nContent-Type: */*\r\nConnection: keep-alive\r\nKeep-Alive: timeout=30, max=200\r\nConnection: keep-alive\r\nKeep-Alive: timeout=300, max=200\r\n\r\n";
char html_packet[256] = "HTTP/1.1 200 OK\r\nContent-Type:text/html\r\nConnection: keep-alive\r\nKeep-Alive: timeout=30, max=200\r\n\r\n";
char _403_packet[256] = "HTTP/1.1 403 Forbidden\r\nConnection: keep-alive\r\nKeep-Alive: timeout=30, max=200\r\n\r\nYou dont't have permission to access\r\n\r\n";
char _404_packet[256] = "HTTP/1.1 404 Not Found\r\nConnection: keep-alive\r\nKeep-Alive: timeout=30, max=200\r\n\r\nFile is not found\r\n\r\n";


//params for thread
typedef struct {
	int argc;
	char **argv;
	int client_socket;
	int server_socket;
} Params;

int file_exists(char* filename);
int isthere_params(char* msg);
char* get_filename(char* msg);
void get_id(char* , char*);
int isthere_cookie(char* msg);
char* get_type(char *name);
void* mfun(void *args);

int main(int argc, char *argv[]) {
	//thread
	int temp=0;
	pthread_t tid;
	int server_socket, client_socket, client_addr_size, cnt=0;
	struct sockaddr_in server_addr, client_addr;

	if (argc != 2) {
		printf("Usage : %s <port>\n", argv[0]);
		return -1;
	}

	server_socket = socket(PF_INET, SOCK_STREAM, 0);

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(atoi(argv[1]));
	//bind
	if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
		puts("bind error!");
		return -1;
	}
	//listen
	listen(server_socket, 20);
	client_addr_size = sizeof(client_addr);

	while(1) {
		printf("accept.... \n");
		client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_size);
		if (client_socket == -1) {
			fprintf(stderr, "Fail to accept client connection!\n");
			continue;
		}
		//structure to send mfun
		Params *params = malloc(sizeof(Params));
		params->argc = argc;
		params->argv = argv;
		params->client_socket=client_socket;
		params->server_socket=server_socket;

		pthread_create(&tid,NULL, mfun, params );
		pthread_detach(tid);
	}
	close(server_socket);
	pthread_exit(NULL);
}


void* mfun(void *args) {
	Params *params = args;
	int argc = params->argc;
	char **argv=params->argv;
	int client_socket=params->client_socket;
	int server_socket=params->server_socket;
	char client_msg[10000];
	memset(&client_msg,0,sizeof(client_msg));

	struct timeval start, now;
	char* user_id = (char*)malloc(sizeof(char)*500);
	char login_packet[256];
	char* check_cookie=(char*)malloc(sizeof(char)*7);
	int* login_flag = (int*)malloc(sizeof(int));
	*(login_flag)=0;
	int len,is_params, client_msg_size, login_socket, login_msg_size,cnt=0, cotime;
	char* buf=malloc(sizeof(char)*4100);
	struct timeval timeout;
	timeout.tv_sec = 3;
	timeout.tv_usec = 0;
	setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	if (client_socket == -1) {
		puts("fail to accept client connection! ");
		return NULL;
	}

	if ((client_msg_size = recv(client_socket, client_msg,10000,0)) > 0) {
		printf("\n%s\n", client_msg);
		if (isthere_cookie(client_msg) == -1)
			*(login_flag)=0;
		else
			*(login_flag) = 1;
		//is_params 1:OK. -1:Entrance url. 0:file name input.
		is_params = isthere_params(client_msg);
		if (is_params == -1) {
			send(client_socket, html_packet,strlen(html_packet),0);
			int fr = open("index.html",O_RDONLY);
			if (fr <0) {
				puts("Error(index)");
				exit(1);
			}
			while ((len=read(fr,buf, 4096)) > 0 ) {
				send(client_socket,buf,len, 0);
			}
			close(fr);
		}

		else if (is_params == 1) {
			*(login_flag)=1;
			get_id(client_msg, user_id);
			sprintf(login_packet, "HTTP/1.1 200 OK\r\nContent-Type:text/html\r\nConnection: keep-alive\r\nKeep-Alive: timeout=30, max=200\r\nSet-Cookie: name=%s/%lu; Max-age=30\r\n\r\n", user_id, time(NULL));
			send(client_socket, login_packet,strlen(login_packet),0);
			int secret = open("secret.html",O_RDONLY);
			if ( secret<0) {
				puts ("Error(secret)");
				exit(1);
			}  
			while ((len = read(secret, buf, 4096)) >0) {
				send(client_socket,buf,len, 0);
			}				
		}	

		else {
			//when failed to login
			if (*login_flag == 0)
				send(client_socket, _403_packet, strlen(_403_packet),0);
			else {
				char* file_name = get_filename(client_msg);
				if (file_exists(file_name)== -1) {
					send(client_socket, _404_packet, strlen(_404_packet),0);
				} 
				else if (!strcmp(file_name,"cookie.html")){
					send(client_socket, html_packet, strlen(html_packet), 0);
					int fr = open("cookie.html", O_RDONLY);
					while ((len=read(fr,buf,4096)) > 0 ) {
						send(client_socket,buf,len, 0);
					}
					close(fr);
				}
				else {
					char* type = get_type(file_name);
					//transfer video
					if (!strcmp(type,"mp4")|| !strcmp(type,"avi") || !strcmp(type,"mkv") || !strcmp(type,"wmv") || !strcmp(type, "mpeg") || !strcmp(type,"mpg")) {
						send(client_socket, video_packet,strlen(video_packet),0);
					}
					//html
					else if (!strcmp(type,"html"))
					{send(client_socket, html_packet, strlen(html_packet),0); 	
					}
					//img
					else if (!strcmp(type,"jpg") || !strcmp(type,"jpeg") || !strcmp(type,"png") ||!strcmp(type,"bmp") || !strcmp(type,"gif"))
						send(client_socket, image_packet, strlen(image_packet),0);
					//etc
					else {
						send(client_socket, others_packet, strlen(others_packet),0);
					}

					int fr = open(file_name,O_RDONLY);
					if (fr <0) {
						puts("Error(file)");
						exit(1);
					}

					while ((len=read(fr,buf,4096)) > 0 ) {
						send(client_socket,buf,len, 0);
					}

					close(fr);
					memset(&client_msg,0,10000);
				}
			}
		}
	}
	free(params);
	free(user_id);
	free(login_flag);
	free(check_cookie);
	free(buf);
	close(client_socket);
}

int file_exists(char* filename) {
	FILE* file;
	if (file = fopen(filename, "rb")) {
		fclose(file);
		return 1;
	}
	return -1;
}
void get_id(char*msg, char* id) {
	int i=9,j=0;
	while (msg[i] != '&') 
		id[j++] = msg[i++];
}

int isthere_params(char* msg) {
	//First access. show index.html
	if (msg[5] ==' ') return -1;
	//log in
	else if (msg[5] == '?')
		return 1;
	//others- file request
	else return 0;
}
char* get_filename(char* msg) {
	char * parsed_name = (char*)malloc(sizeof(char)*200);
	int i,idx=0;
	for(i=5; i<200; i++) {
		if (msg[i] == ' ') break;
		parsed_name[idx++] = msg[i];
	}
	parsed_name[idx] = '\0';
	return parsed_name;
}

int isthere_cookie(char* msg) {
	int length = strlen(msg), i,j=0;
	char* newarr=(char*)malloc(sizeof(char)*7);
	char ck[7]="Cookie";
	if (strstr(msg, ck)!=NULL) return 1;
	else return -1;
}

char* get_type(char *name) {
	int i, flag=0, j=0;
	char* type= (char*)malloc(sizeof(char)*7);
	memset(type, 0, 7);
	for (i=0;i<strlen(name); i++) {
		if (flag == 1) type[j++]=name[i]; 
		if (name[i]== '.') flag=1;
	}
	type[j] = '\0';
	return type;
}
