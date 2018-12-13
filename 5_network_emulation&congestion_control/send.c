//sender <Client>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include "sender.h"

#define PACKET_SIZE 1400
#define BILLION 1000000000
#define QUEUE_SIZE 1 << 20
#define THRESHOLD 10000000

typedef char bool;

#define true 1
#define false 0
long start_time;
typedef struct {
	int window_size;
	int sock_fd;
	int ack_port;
	struct sockaddr* addr;
} Params;

bool done = false;
int num_packets;
int num_acks;
double sum_rtt;
long SR, G;
long total_send, saved_send;
long total_ack, saved_ack;

double avg_rtt;
long g_ack_port;

long g_seq = -3;
bool g_dup3 = false;

pthread_mutex_t mutex;
pthread_cond_t condp, condc;

void cal_rtt() {
}

long get_current_time() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	long usec = tv.tv_usec / 1000;
	long sec = tv.tv_sec * 1000;

	return sec + usec;
}


void write_log(int signum) {
	char log_name[20];
	memset(log_name,0,20);
	sprintf(log_name, "%ld_log.txt", g_ack_port);
		
	long record_send = total_send - saved_send;
	saved_send = total_send;
	
	long record_ack = total_ack - saved_ack;
	saved_ack = total_ack;

	FILE* fp = fopen(log_name, "a");

	long spent = get_current_time() - start_time;

	//write on portnum_log
	fprintf(fp, "%ld.%03ld | avg_RTT: %f | SR: %ld | G:%ld\n", spent / 1000, spent % 1000,avg_rtt ,record_send, record_ack);


	fclose(fp);
}

void send_packet(int sock_fd, long seq, long ack_port, long cur_time, struct sockaddr *addr) {
	char msg[1400];
	int i;
	long* msg_l = (long*) msg;
	msg_l[0] = seq;
	msg_l[1] = ack_port;
	msg_l[2] = cur_time;
	memcpy(msg + 24, payload, PACKET_SIZE - 24);

	if (sendto(sock_fd, msg, PACKET_SIZE, 0,
		addr, sizeof(struct sockaddr)) != PACKET_SIZE) {
		perror("send failed");
		pthread_exit(NULL);
	}
	total_send++;
	printf("sent #%ld\n",seq);
}
struct timespec timeout_queue[QUEUE_SIZE + 2] = {0,};
long timeout_idx[QUEUE_SIZE + 2];

void* sender_func(void* arg) {

	total_send = saved_send = 0;
	//signal 2 sec
	struct sigaction sa;
	struct itimerval timer;
	memset(&sa, 0, sizeof(sa));
	if (signal(SIGALRM, (void(*))write_log) == SIG_ERR) {
		perror("Unable to catch SIGALRM");
		exit(1);
	}
	//sa.sa_handler = write_log;
	//sigaction(SIGVTALRM, &sa, NULL);

	timer.it_value.tv_sec = 2;
	timer.it_value.tv_usec = 0;

	timer.it_interval.tv_sec = 2;
	timer.it_interval.tv_usec = 0;

	if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
		perror("error");
		exit(1);
	};

	Params* params = arg;
	int sock_fd = params->sock_fd;
	long ack_port = (long) params->ack_port;
	int window_size = params->window_size;
	struct sockaddr_in* addr = (struct sockaddr_in*) params->addr;
	long start_time = get_current_time();
	long seq = 0, ack_seq = 0;
	int start = 0, end = 0;
	long cur_time;
	int errno = 0;
	bool dup3 = false;
	long prev_ack = -3;
	printf("new client comes up!\n");

	while (1) {
		clock_gettime(CLOCK_REALTIME, &timeout_queue[end]);
		timeout_idx[end] = seq;
		timeout_queue[end].tv_nsec += THRESHOLD;
		if (timeout_queue[end].tv_nsec >= BILLION) {
			timeout_queue[end].tv_nsec -= BILLION;
			timeout_queue[end].tv_sec ++;
		}
		//timeout_queue[end].tv_sec += 100;
		end = (end + 1) % QUEUE_SIZE;
		while (window_size + (ack_seq == -3 ? 0 : ack_seq) >= seq) {
			send_packet(sock_fd, seq, ack_port, get_current_time(), (struct sockaddr*) addr);
			seq ++;
		}

		pthread_mutex_lock(&mutex);
		if (g_seq == -3) {
			start = 0;
		} else {
			start = abs((g_seq + 1) % QUEUE_SIZE);
		}
		errno = 0;

		//should be modified
		while (window_size + g_seq < seq// window size over
					&& g_seq == ack_seq // dup ack
					&& !g_dup3 // dup3
					&& errno == 0) { // timeout
		// check 
		//printf("%d, %d, %d, %d\n", window_size + g_seq < seq, g_seq == prev_ack, !g_dup3, errno == 0);
		//printf("%d, %d, %d, %d\n", window_size, g_seq, seq, ack_seq);
			pthread_cond_timedwait(&condc, &mutex, &timeout_queue[start]);
		}
		if (errno != 0) {
			perror("pthread_cond_timedwait failed");
		}
		prev_ack = ack_seq;
		ack_seq = g_seq;
		dup3 = g_dup3;
		g_dup3 = false;
		pthread_cond_signal(&condp);
		pthread_mutex_unlock(&mutex);
		
		if (dup3) {
			printf("\n\ndup3!!!!!\n\n");
			seq = ack_seq;
			int tmp_w = window_size / 2;
			window_size = tmp_w ? tmp_w : 1;
		} else if (errno) {
			seq = timeout_idx[start];
			window_size = 1;
			continue; 
		} else if (ack_seq != prev_ack) {
			window_size ++;
		} else {
		}
		printf("window size: %d\n", window_size);

		/*if (ack_seq > seq) {
			seq = ack_seq;
		}
		else if (seq >= window_size + ack_seq || g_dup3) {
			errno = 0;
			pthread_mutex_lock(&mutex);
			while (g_seq == seq - window_size && !g_dup3 && g_seq != -3 && errno == 0) {
				pthread_cond_timedwait(&condc, &mutex, &timeout_queue[start]);
			}
			if (errno != 0) {
				seq = timeout_idx[start];
				continue;
			}
			g_dup3 = false;
			pthread_mutex_unlock(&mutex);
			seq = ack_seq;
		}*/
		//seq ++;
	}
}
 
void* ack_func(void* arg) {
	total_ack, saved_ack = 0;
	Params* params = arg;
	int sock_fd = params->sock_fd;
	struct sockaddr_in* ack_addr = (struct sockaddr_in*) params->addr;
	struct sockaddr_in recv_addr;
	char buf[26];
	long* header = (long*) buf;
	long seq, ack_port, time;
	int recv_len;
	char log_buf[200];
	int cnt = 0;
	long prev_seq = -1;
	sum_rtt = 0;

	if (bind(sock_fd, (struct sockaddr*)ack_addr, sizeof(recv_addr)) != 0) {
		perror("bind failed");
		exit(1);
	}

	while(1) {
		do {
			if ((recv_len = recvfrom(sock_fd, buf, 24, 0, NULL, NULL)) == -1) {
				perror("recvfrom failed");
				continue;
			}
			total_ack++;

			seq = header[0];
		} while (g_dup3 && seq == prev_seq);
		printf("ack %ld received\n", seq);
		ack_port = header[1];
		time = header[2];
		long tmp_rtt = get_current_time() - time;
		double rtt = (double)(tmp_rtt/1000) + (double)((tmp_rtt)%1000000000);
		sum_rtt += rtt;
		avg_rtt = sum_rtt/(double)seq;
		if (prev_seq == seq) {
			cnt ++;
			printf("\n\n\nduplicated! cnt: %d\n\n\n",cnt);
		}
		else {
			prev_seq = seq;
			cnt = 0;
		}

		long spent = get_current_time() - time;

		memset(log_buf, 0, 200);
		sprintf(log_buf, "%ld.%03ld ACK: %ld | received\n", spent / 1000, spent % 1000, seq);

		pthread_mutex_lock(&mutex);
		g_seq = seq;
		if (cnt == 3) {
			printf("ack func: set g_dup3 true!\n");
			g_dup3 = true;
		}
		printf("wow\n");
		pthread_cond_signal(&condc);
		pthread_mutex_unlock(&mutex);
	}
	close(sock_fd);
	pthread_exit(NULL);
}

int main(int argc, char** argv) {
	start_time = get_current_time();
	char cmd[100];
	pthread_t ack_tid, send_tid;
	int ack_sock, send_sock;
	struct sockaddr_in addr;
	struct sockaddr_in ack_addr;
	int addr_len;
	long ack_port;
	Params *params = malloc(sizeof(Params));

	pthread_mutex_init(&mutex, NULL);
	pthread_cond_init(&condp, NULL);
	pthread_cond_init(&condc, NULL);

	if (argc < 2) {
		fprintf(stderr, "$> %s ip_address\n", argv[0]);
		exit(1);
	}

	// Define socket for sender thread
	if ((send_sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("socket failed");
		exit(1);
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(argv[1]);
	addr.sin_port = htons(10080);

	params->sock_fd = send_sock;
	memset(cmd, 0, 100);
	long* dummy_ptr = (long*) cmd;
	dummy_ptr[0] = -1;
	if (sendto(send_sock, cmd, 8, 0, (struct sockaddr*) &addr, sizeof(addr)) != 0)


	// Define socket for ack thread
	if ((ack_sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("socket failed");
		exit(1);
	}
	memset(&ack_addr, 0, sizeof(addr));
	ack_addr.sin_family = AF_INET;
	ack_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	ack_addr.sin_port = htons(0);

	// Set params for ack thread
	Params* ack_params = malloc(sizeof(Params));
	ack_params->sock_fd = ack_sock;
	ack_params->addr = (struct sockaddr *)&ack_addr;
	//params->ack_port = 0;

	// Create thread for ack
	if (pthread_create(&ack_tid, NULL, ack_func, ack_params)) {
		perror("pthread create failed");
	}
	if (pthread_detach(ack_tid)) {
		perror("detach error");
	}

	params->window_size = 0;
	// Get start command and window_size
	do {
		printf("command >> ");
		fflush(stdin);
		scanf("%s\t%d", cmd, &params->window_size);
	} while (strncmp(cmd, "start", 5) != 0 || params->window_size <= 0);

	// Get ack socket port number
	if (getsockname(ack_sock, (struct sockaddr*)&ack_addr, &addr_len) != 0) {
		perror("cannot find socket");
		exit(1);
	}
	params->ack_port = (long) ack_addr.sin_port;//(long)ntohs(addr.sin_port); // not sure
	g_ack_port = (long) ack_addr.sin_port;
	params->addr = (struct sockaddr*) &addr;
	printf("port : %d\n", params->ack_port);


	// Create thread for sender
	pthread_create(&send_tid, NULL, sender_func, params);
	pthread_detach(send_tid);

	memset(cmd, 0, 100);
	// Get stop command
	do {
		printf("command >> ");
		fflush(stdin);
		scanf("%s", cmd);
	} while (strncmp(cmd, "stop", 4) != 0);

	// Kill threads and send
	pthread_kill(send_tid, 9);
	send_packet(send_sock, -1, ack_port, get_current_time(), (struct sockaddr*) &addr);

	pthread_mutex_lock(&mutex);
	while (g_seq != -1) {
		pthread_cond_wait(&condc, &mutex);
	}
	g_seq = 0;
	pthread_kill(ack_tid, 9);
	pthread_mutex_unlock(&mutex);
	done = true;

	return 0;
}
