//receiver.c <Server>
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

#define PACKETSIZE 1400
#define TABLESIZE 200000
#define t_idx(x) ((x->sin_addr.s_addr + x->sin_port) % TABLESIZE)
long num_rm_pkts, saved_num_for_rm;
long sender_to_nem, saved_stn;
long nem_to_rm, saved_ntr;

typedef struct {
	unsigned short port;
	char ip[16];
	int incomings;
	int received;
	int cum_seq;
} Log_elem;
long idx_table[TABLESIZE];
int top = 0;
Log_elem log_table[TABLESIZE];
int queue_size;
long start_time;
struct sockaddr_in g_use;
typedef enum { false, true } bool;
typedef struct {
	char buf[33];
} Elem;

typedef struct {
	int front, rear;
	Elem items[1024];
} Queue;

// Make queue empty.
void InitQueue(Queue *pqueue) {
	pqueue->front = pqueue->rear = 0;
}

// Check whether queue is full.
bool IsFull(Queue *pqueue, int MAX_QUEUE)
{
	return pqueue->front
		== (pqueue->rear + 1) % MAX_QUEUE;
}

// Check whether queue is empty.
bool IsEmpty(Queue *pqueue) {
	return pqueue->front == pqueue->rear;
}
// Read the item at the front.
Elem Peek(Queue *pqueue)
{
	if (IsEmpty(pqueue)) {
		printf ("Peek error: empty stack\n");
		exit(1); //error: empty stack
	}
	return pqueue->items[pqueue->front];
}

// Insert an item at the rear.
void EnQueue(Queue *pqueue, Elem item, int MAX_QUEUE)
{
	if (IsFull(pqueue, MAX_QUEUE)) {
		printf ("EnQueue error : stack is full\n");
		exit(1); //error: stack full
	}
	pqueue->items[pqueue->rear] = item;
	pqueue->rear = (pqueue->rear + 1) % MAX_QUEUE;
}

// Delete an item at the front.
void DeQueue (Queue *pqueue, int MAX_QUEUE)
{
	if (IsEmpty(pqueue)) {
		printf ("DeQueue error: empty stack\n");
		exit(1); //error: empty stack
	}
	pqueue->front = (pqueue->front + 1) % MAX_QUEUE;
}

typedef struct {
	int recv_soc;
	struct sockaddr_in send_addr;
	int BLR;
	int queue_size;
} NEM_Params;

typedef struct {
	int recv_soc;
	struct sockaddr_in send_addr;
	int BLR;
	int queue_size;
}	RM_Params;

//To forward
Queue queue = {.front =0, .rear = 0};

pthread_mutex_t mutex;

long get_current_time() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	long usec = tv.tv_usec / 1000;
	long sec = tv.tv_sec * 1000;
	return sec+usec;
}

uint16_t parse_item(char* buf, struct sockaddr_in* addr) {
	//printf("parse_item");
	long seq, ack_port, sent_time, spent;
	char* payload = &buf[24];
	long* head = (long*) buf;
	int i;
	char log_name[30];
	memset(log_name,0,30);

	seq = head[0];
	printf(" nem | %ld\n", seq);
	ack_port = head[1];
	sent_time = head[2];

	//sender killed the thread
	if (seq == -100) {
		printf("sender killed the program.");
		exit(1);
	}
	else if (seq == 0) {
		printf("seq#0. new client!)\n");
		idx_table[top ++] = t_idx(addr);
		Log_elem* log_elem = &log_table[t_idx(addr)];
		log_elem->port = ntohs(addr->sin_port);
		char* ip = inet_ntoa(addr->sin_addr);
		memcpy(log_elem->ip, ip, strlen(ip));
		log_elem->received = 0;
		log_elem->incomings = 1;
		log_elem->cum_seq = 0;
	} else {
		Log_elem* log_elem = &log_table[t_idx(addr)];
		log_elem->incomings ++;
		//printf("port: %ld, seq# :%ld received\n",ack_port, seq);
		//receive in order
	}
	spent = get_current_time() - start_time;
	return ack_port;
}

int less_than_sec(long std) {
	if (get_current_time() - std < 1000)
		return 1;
	else return 0;
}

void write_log(int signum) {	
	long record_rm = num_rm_pkts - saved_num_for_rm;
	saved_num_for_rm = num_rm_pkts;

	long record_stn = sender_to_nem - saved_stn;
	saved_stn = sender_to_nem; 

	long record_ntr = nem_to_rm - saved_ntr;
	saved_ntr = nem_to_rm;

	//printf("\n\nrm\n\n");

	FILE* rm = fopen("RM_log.txt","a");
	FILE* nem = fopen("NEM_log.txt","a");

	long spent = get_current_time() - start_time;
	
	//write on nem
	fprintf(nem, "%ld.%03ld | incoming: %ld | forwarding: %ld \n", spent/1000, spent%1000, record_stn, record_ntr);

	//write on rm
	int i;
		fprintf(rm, "%ld.%03ld |\n", spent/1000, spent%1000);
	for (i = 0; i < top; i ++) {
		Log_elem elem = log_table[idx_table[i]];
		fprintf(rm, "          | %s:%hu  | rate: %f\n", elem.ip, elem.port, elem.received / (float) elem.incomings);
		elem.received = 0;
		elem.incomings = 0;
	}
	

	fclose(rm);
	fclose(nem);
}

void* rm_func(void* arg) {
	//signal 2 sec
	struct sigaction sa;
	struct itimerval timer;
	memset(&sa, 0, sizeof(sa));
	if (signal(SIGALRM,(void(*))write_log) == SIG_ERR) {
		perror("Unable to catch SIGALRM");
		exit(1);
	}
	//sa.sa_handler = write_log;
	//sigaction(SIGVTALRM, &sa, NULL);

	timer.it_value.tv_sec = 2;
	timer.it_value.tv_usec = 0;

	timer.it_interval.tv_sec = 2;
	timer.it_interval.tv_usec = 0;

	if (setitimer(ITIMER_REAL, &timer, NULL) == -1){
		perror("error");
		exit(1);
	};
	//end setitimer
	


	saved_num_for_rm=0;
	RM_Params *params=arg;
	int recv_soc = params->recv_soc;
	struct sockaddr_in send_addr = params->send_addr;
	int queue_size = params->queue_size;
	int BLR = params->BLR;
	//peek_form_queue
	Elem target_item, delayed_item;
	char buffer[26];
	long sec_std;
	int cnt=0;
	num_rm_pkts=0;

	

	while (1) {

		//update each 1 sec
		if (less_than_sec(sec_std) == 0) {
			//printf("update sec_flag.");
			sec_std = get_current_time();
			cnt=0;
		}
		
		if (less_than_sec(sec_std) == 1 && cnt<=queue_size) {
			//if delaying queue is empty than go forwarding q
			if (!IsEmpty(&queue)) {
				target_item = Peek(&queue);
				//Dequeue from forwarding queue
				DeQueue(&queue, queue_size);
				
				/*spent = get_current_time() - start_time;
				fprintf(fp, "%ld.%03ld pkt: %ld | received\n",
				spent/1000, spent%1000, seq);
				*/
			} else
					continue;
			
			//SET ACK AND SEND IT
			memset(&send_addr.sin_port,0,sizeof(send_addr.sin_port));
			//cpy send_addr itself from queue-popped item  
			memcpy(&send_addr,target_item.buf +24, 8);
			//cpy ack_port from queue-popped item
			//memcpy(&send_addr.sin_port, target_item.buf +8, sizeof(send_addr.sin_port));
			memset(buffer,0,24);	
			//memcpy(buffer,target_item.buf,24);
			long* th=(long*)target_item.buf;
			int cum = log_table[t_idx((&send_addr))].cum_seq;
			long* tmp = (long*)buffer;
			//printf("%d - %d (%d)\n", cum, th[0], log_table[t_idx((&send_addr))].cum_seq);
			if (cum == th[0])
				cum ++;
			//printf("%d - %d (%d)\n", cum, th[0], log_table[t_idx((&send_addr))].cum_seq);
			tmp[0] = cum;
			tmp[1]=th[1];
			tmp[2]=th[2];
			log_table[t_idx((&send_addr))].cum_seq = cum;
			//printf("%d - %d (%d)\n", cum, th[0], log_table[t_idx((&send_addr))].cum_seq);
			send_addr.sin_port = th[1];

			printf("send ack from => seq : %ld\n",tmp[0]);
			num_rm_pkts++;
			if (sendto(recv_soc, buffer, 24, 0,
						(struct sockaddr *)&send_addr,
						sizeof(send_addr)) < 0) {
				fprintf(stderr,"sendto failed: %s\n", strerror(errno));
				exit(1);
			}
			cnt++;
		}
	}//end of while

	while(1);
}


void* nem_func(void* arg) {
	saved_stn, saved_ntr, sender_to_nem , nem_to_rm =0;
	long seq, ack_port, s_time;
	NEM_Params *params = arg;
	int recv_soc = params->recv_soc;
	struct sockaddr_in send_addr = params->send_addr;
	int BLR = params->BLR;
	int queue_size = params->queue_size;
	//receive packets.
	int send_len, recv_len;
	char buffer[PACKETSIZE + 2];

	while(1) {
		//receive
		send_len = sizeof(send_addr);
		if ((recv_len = recvfrom(recv_soc, buffer,
						PACKETSIZE, 0, (struct sockaddr *)&send_addr,
						&send_len)) == -1) {
			perror("recvfrom failed");
			exit(1);
		}
		sender_to_nem++;

		//parse and set item to Enqueue forward or queue
		uint16_t pt = parse_item(buffer, &send_addr);

		Elem item;
		memset(item.buf, 0, 32);
		long* tm = (long*) buffer;
		long* itembuf = (long*)item.buf;
		seq = tm[0];
		ack_port= tm[1];
		s_time = tm[2];
		if (seq == 1) {
			printf("\n\nport-> %ld\n\n", ack_port);
		}
		itembuf[0] = seq;
		if (seq == -1)
			continue;
		itembuf[1] = ack_port;
		itembuf[2] = s_time;

		memcpy((item.buf)+24, &send_addr,8);
		//end of setting item

		//over 1 sec : update cnt and start std of 1 sec
		//over BLR : enqueue into delay queue



		if (IsFull(&queue, queue_size)) {
			printf("FULL! DROP \n");
		}
		//enqueue to forwarding queue
		//while (sec_flag == 1 && cnt <= BLR)
			// enqueue to forwarding queue
		else {
				//printf("not full\n");
			log_table[t_idx((&send_addr))].received ++;
			printf("seq#%ld forwarding\n", seq);
			nem_to_rm++;	
			EnQueue(&queue, item, queue_size);
		}
			//cnt++;
	}//end of while
}


int main(int argc, char** argv) {
		pthread_t nem_tid, rm_tid;
		start_time = get_current_time();

	Queue q;
	InitQueue(&q);
	if (argc<3) {
		fprintf(stderr, "$> %s BLR,queue_size\n",argv[0]);
		exit(1);
	}

	pthread_mutex_init(&mutex, NULL);

	int BLR = atoi(argv[1]);
	queue_size = atoi(argv[2]);
	//printf("%d %d ", BLR, queue_size);
	int recv_soc;
	struct sockaddr_in recv_addr, send_addr;
	//char buffer[PACKETSIZE+2];
	int recv_len;

	if ((recv_soc = socket(AF_INET, SOCK_DGRAM,0)) == -1) {
		perror("socket failed");
		exit(1);
	}
	memset(&recv_addr, 0, sizeof(recv_addr));
	recv_addr.sin_family = AF_INET;
	recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	recv_addr.sin_port = htons(10080);

	if (bind(recv_soc, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) == -1) {
		perror("bind failed");
		exit(1);
	}

	NEM_Params *nem_params = malloc(sizeof(NEM_Params));
	nem_params->recv_soc =recv_soc;
	nem_params->send_addr = send_addr;
	nem_params->BLR=BLR;
	nem_params->queue_size = queue_size;

	RM_Params *rm_params = malloc(sizeof(RM_Params));
	rm_params->recv_soc = recv_soc;
	rm_params->send_addr = send_addr;
	rm_params->BLR = BLR;
	rm_params->queue_size = queue_size;

	pthread_create(&nem_tid, NULL, nem_func, nem_params);
	pthread_detach(nem_tid);

	pthread_create(&rm_tid, NULL, rm_func, rm_params);
	pthread_detach(rm_tid);

	pthread_exit(0);
	while(1);
}
