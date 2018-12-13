//sender.c <Client>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>

#define PACKET_SIZE 1400

typedef char bool;

#define true 1
#define false 0

typedef struct {
	char filename[128];
	int window_size;
	int timeout;
	char* ip;
	int socket_fd;
	long uid;
} Params;

struct ack_buffer {
	long cid;
	long seq;
};

pthread_mutex_t uid_mutex;
long guid = 0;

bool used_uid[128];
pthread_mutex_t mutex[128];
pthread_cond_t condc[128], condp[128];
struct ack_buffer ack_val[128];
int log_table[128];
long start_time_table[128];

long get_current_time() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	long usec = tv.tv_usec / 1000;
	long sec = tv.tv_sec * 1000;
	return sec+usec;
}

long get_user_id() {
	long uid;
	pthread_mutex_lock(&uid_mutex);
	uid = guid ++;
	guid %= 128;
	used_uid[uid] = true;
	pthread_mutex_unlock(&uid_mutex);
	return uid;
}

void* send_thread(void* args) {
	fprintf(stderr, "send thread started\n");
	struct timeval tv;
	Params *params = args;
	int len;
	char buf[1280];
	char sendbuf[PACKET_SIZE];
	long seq = 0, cid = 0;
	char head[120];
	long start_time = get_current_time();
	int sockfd = params->socket_fd;
	struct sockaddr_in server_addr;
	int window_size = params->window_size;
	int org_window_size = window_size;
	char* file_name = params->filename;

	int fr = open(params->filename, O_RDONLY);
	int file_size = lseek(fr, 0, SEEK_END);
	long num_packets = file_size / 1240;
	num_packets += file_size % 1240 ? 1 : 0;
	lseek(fr, 0, SEEK_SET);

	long uid = params->uid;
	printf("user id: %ld\n", uid);
	ack_val[uid].cid = -1;
	ack_val[uid].seq = -1;
	pthread_mutex_t *my_mutex = &mutex[uid];
	pthread_cond_t *my_condc = &condc[uid];
	pthread_cond_t *my_condp = &condp[uid];

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(params->ip);
	server_addr.sin_port = htons(10080);

	while (1) {
		lseek(fr, 0, SEEK_SET);
		if ((len = read(fr, buf, 1240))>0) {
			memcpy(sendbuf, &num_packets, 8);
			memcpy(sendbuf+8, &seq, 8);
			memcpy(sendbuf+16, &uid, 8);
			memset(sendbuf+24, 0, 128);
			memcpy(sendbuf+24, file_name, strlen(file_name));
			memcpy(sendbuf+152,&buf,len);
			if (sendto(sockfd, sendbuf, len + 152, 0,
								(struct sockaddr*) &server_addr,
								sizeof(struct sockaddr)) != len + 152) {
				perror("send failed");
				pthread_exit(NULL);
			}
			printf("seq#0 packet sent\n");
			seq ++;
		} else {
			perror("read failed");
			pthread_exit(NULL);
		}

		pthread_mutex_lock(my_mutex);
		while (ack_val[uid].cid == -1)
			pthread_cond_wait(my_condc, my_mutex);
		int tmp_cid = ack_val[uid].cid;
		pthread_cond_signal(my_condp);
		pthread_mutex_unlock(my_mutex);
		if (tmp_cid != -1)
			break;
	}

	int dup_cnt = 0;
	int cum_seq = 0;
	
	printf("num packets : %ld\n", num_packets);
	char log_buf[128];
	while (1) {
		pthread_mutex_lock(my_mutex);
		while (ack_val[uid].cid == -1)
			pthread_cond_wait(my_condc, my_mutex);

		struct ack_buffer ack = ack_val[uid];
		ack_val[uid].cid = -1;

		pthread_cond_signal(my_condp);
		pthread_mutex_unlock(my_mutex);
		
		if (ack.seq == num_packets - 1) {
			break;
		}

		if (ack.seq == cum_seq) {
			dup_cnt ++;
			window_size = 1;
			if (dup_cnt == 3) {
				long spent = get_current_time() - start_time_table[uid];
				memset(log_buf, 0, 200);
				sprintf(log_buf, "%ld.%03ld pkt: %d | 3 duplicated ACKs\n", spent / 1000, spent % 1000, cum_seq);
				write(log_table[uid], log_buf, strlen(log_buf));
				seq = ack.seq + 1;
			}
		} else if (seq <= ack.seq) {
			seq = ack.seq + 1;
			window_size = org_window_size;
			dup_cnt = 0;
		} else if (seq > ack.seq) {
			window_size = org_window_size;
			dup_cnt = 0;
		}

		cum_seq = ack.seq;
		
		while (seq < num_packets && seq <= cum_seq + window_size) {
			//printf("seq#%ld packet sent\n", seq);
			lseek(fr, seq * 1240, SEEK_SET);
			if ((len = read(fr, buf, 1240))>0) {
				memcpy(sendbuf, &ack.cid, 8);
				memcpy(sendbuf+8, &seq, 8);
				memcpy(sendbuf+16, &uid, 8);
				memcpy(sendbuf+152,&buf,len);

				long spent = get_current_time() - start_time_table[uid];
				memset(log_buf, 0, 200);
				sprintf(log_buf, "%ld.%03ld pkt: %ld | sent\n", spent / 1000, spent % 1000, seq);
				write(log_table[uid], log_buf, strlen(log_buf));
				if (sendto(sockfd, sendbuf, len + 152, 0,
									(struct sockaddr*) &server_addr,
									sizeof(struct sockaddr)) != len + 152) {
					perror("send failed");
					pthread_exit(NULL);
				}
			} else break;
			seq++;
		}
		window_size = org_window_size;

	}
	write(log_table[uid], "\n", 1);
	memset(log_buf, 0, 200);
	sprintf(log_buf, "File transfer is finished.\n");
	write(log_table[uid], log_buf, strlen(log_buf));
	memset(log_buf, 0, 200);
	sprintf(log_buf, "Throughput: %.2f pkts / sec\n", num_packets / (float) (get_current_time() - start_time_table[uid]) * 1000);
	write(log_table[uid], log_buf, strlen(log_buf));
	close(log_table[uid]);
	used_uid[uid] = false;
	fprintf(stderr, "thread is exited\n");
}

void* recv_ack(void* arg) {
	int sockfd;
	struct sockaddr_in recv_addr;
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("socket failed");
		exit(1);
	}
	memset(&recv_addr, 0, sizeof(recv_addr));
	recv_addr.sin_family = AF_INET;
	recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	recv_addr.sin_port = htons(10070);

	if (bind(sockfd, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) == -1) {
		perror("bind failed");
		exit(1);
	}

	int recv_len;
	char buf[24];
	long* header = (long*) buf;
	long uid;
	int seq;
	struct ack_buffer ack;
	char log_buf[200];
	while (1) {
		//printf("wait for packet\n");
		if ((recv_len = recvfrom(sockfd, buf, 24, 0, NULL, NULL)) == -1) {
			perror("recvfrom failed");
			continue;
		}

		ack.cid = header[0];
		ack.seq = header[1];
		uid = header[2];
		if (!used_uid[uid]) {
			continue;
		}
		long spent = get_current_time() - start_time_table[uid];
		memset(log_buf, 0, 200);
		sprintf(log_buf, "%ld.%03ld ACK: %ld | received\n\0", spent / 1000, spent % 1000, ack.seq);
		write(log_table[uid], log_buf, strlen(log_buf));
		printf("ack received (%ld, %ld, %ld)\n", ack.cid, uid, ack.seq);
		
		pthread_mutex_lock(&mutex[uid]);
		while (ack_val[uid].cid != -1)
			pthread_cond_wait(&condp[uid], &mutex[uid]);
		ack_val[uid] = ack;
		pthread_cond_signal(&condc[uid]);
		pthread_mutex_unlock(&mutex[uid]);
	}
	close(sockfd);
	pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
	int sockfd;
	char file_name[128];
	pthread_t tid;
	bool sw = false;
	int i;
	for (i = 0; i < 128; i ++) {
		pthread_mutex_init(mutex + i, NULL);
		pthread_cond_init(condp + i, NULL);
		pthread_cond_init(condc + i, NULL);
	}

	if (argc < 3) {
		fprintf(stderr, "$> %s ip_address timeout window_size\n", argv[0]);
		exit(1);
	}

	struct sockaddr_in recv_addr;
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("socket failed");
		exit(1);
	}
	memset(&recv_addr, 0, sizeof(recv_addr));
	recv_addr.sin_family = AF_INET;
	recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	recv_addr.sin_port = htons(0);

	if (bind(sockfd, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) == -1) {
		perror("bind failed");
		exit(1);
	}

	printf("Receiver IP address: %s\n", argv[1]);
	printf("window size: %s\n", argv[3]);
	printf("timeout (sec): %s\n", argv[2]);

	char log_file_buf[200];
	while (1) {
		printf("file name: ");
		memset(file_name, 0, 128);
		fflush(stdin);
		scanf("%s", file_name);

		Params *params = malloc(sizeof(Params));
		params->ip = argv[1];
		params->timeout = atoi(argv[2]);
		params->window_size = atoi(argv[3]);
		strcpy(params->filename, file_name);
		params->uid = get_user_id();
		params->socket_fd = sockfd;
		memset(log_file_buf, 0, 200);
		sprintf(log_file_buf, "%s_sending_log.txt", file_name);
		log_table[params->uid] = open(log_file_buf, O_CREAT | O_WRONLY, S_IRWXU);
		start_time_table[params->uid] = get_current_time();
		write(log_table[params->uid], "0.000 pkt: 0 | sent\n", 20);

		pthread_create(&tid, NULL, send_thread, params);
		pthread_detach(tid);
		printf("new thread executed\n");

		if (sw == false) {
			pthread_create(&tid, NULL, recv_ack, params);
			pthread_detach(tid);
			sw = true;
		}
	}
	close(sockfd);
	
	return 0;
}
