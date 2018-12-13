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

#define BUFSIZE 1400

typedef char bool;

#define true 1
#define false 0

long g_cid = 0;
bool cid_used[128];
char cache[128][1400 << 7];
long cache_idx_table[128][128];
long seq_max_id[128];
long cum_seq[128];
long packet_size_table[128];
int filefd_table[128];
long start_time_table[128];
float loss_prob;

void init(int cid) {
	memset(cache[cid], 0, 1400 << 7);
	memset(cache_idx_table[cid], 0, 128);
	seq_max_id[cid] = 0;
	cum_seq[cid] = 0;
	packet_size_table[cid] = 0;
}

long get_cid() {
	while (cid_used[g_cid]) {
		g_cid ++;
		g_cid %= 128;
	}
	cid_used[g_cid] = true;
	return g_cid;
}

long get_current_time() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	long usec = tv.tv_usec / 1000;
	long sec = tv.tv_sec * 1000;
	return sec + usec;
}

long get_max_seq(int cid) {
	int i = 0;
	while (cache_idx_table[cid][i] + 1 == cache_idx_table[cid][i + 1])
		i ++;
	return cache_idx_table[cid][i];
}

int parse_packet(char* buf, int buf_len, FILE* fp) {
	long seq, cid, user_code; // 8 bytes each
	char* file_name = &buf[24]; // 128 bytes
	char* payload = &buf[152]; // 128 + 8 * 3 bytes
	// payload has 1400 - 152 - 1 bytes (= 1240 bytes)
	long* head = (long*)buf;
	int i;
	long spent;

	seq = head[1];
	if (seq == 0) {
		printf("seq#0 (new client submitted)\n");
		cid = get_cid();
		init(cid);
		packet_size_table[cid] = head[0];
		start_time_table[cid] = get_current_time();
		fprintf(fp, "0.000 pkt: 0 | received\n");
		memcpy(buf, &cid, 8);
		filefd_table[cid] = open(file_name, O_CREAT | O_WRONLY, S_IRWXU);
		write(filefd_table[cid], payload, buf_len - 152);
	} else {
		cid = head[0];
		spent = get_current_time() - start_time_table[cid];

		fprintf(fp, "%ld.%03ld pkt: %ld | received\n", spent / 1000, spent % 1000, seq);
		printf("%f\n", loss_prob);
		if (rand() % 10000 < loss_prob) {
			fprintf(fp, "%ld.%03ld pkt: %ld | dropped\n", spent / 1000, spent % 1000, seq);
			return -1;
		}
		if (cum_seq[cid] + 1 < seq) {
			int cache_idx = seq - cum_seq[cid] - 2;
			memcpy(buf + 8, &cum_seq[cid], 8);
			memcpy(&cache[cid][cache_idx * 1240], payload, buf_len - 152);
			cache_idx_table[cid][cache_idx] = seq;
			seq_max_id[cid] = get_max_seq(cid);
		} else {
			write(filefd_table[cid], payload, buf_len - 152);
			cum_seq[cid] ++;
			if (cum_seq[cid] < seq_max_id[cid]) {
				write(filefd_table[cid], cache[cid], (seq_max_id[cid] - cum_seq[cid] - 1) * 1240);
				cum_seq[cid] = seq_max_id[cid];
			}
			memcpy(buf + 8, &cum_seq[cid], 8);
			if (cum_seq[cid] == packet_size_table[cid] - 1) {
				spent = get_current_time() - start_time_table[cid];
				fprintf(fp, "%ld.%03ld ACK: %ld | sent\n", spent / 1000, spent % 1000, seq);
				fprintf(fp, "\nFile transfer is finished.\n");
				fprintf(fp, "Throughput: %.2f pkts / sec", cum_seq[cid] / (float)(get_current_time() - start_time_table[cid]) * 1000);
				printf("%s copy completed\n", file_name);
				close(filefd_table[cid]);
				cid_used[cid] = false;
				return 1;
			}
		}
		seq = cum_seq[cid];
	}
	buf[24] = '\0';
	spent = get_current_time() - start_time_table[cid];
	fprintf(fp, "%ld.%03ld ACK: %ld | sent\n", spent / 1000, spent % 1000, seq);
	return 1;
}

int main(){
	srand(time(NULL));
	int recv_soc;
	struct sockaddr_in recv_addr, send_addr;
	char buffer[BUFSIZE+2];
	int recv_len, send_len;
	printf("Enter the packet loss probability : ");
	scanf("%f",&loss_prob);
	loss_prob *= 10000;
	
	if ((recv_soc = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
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
	FILE* fp;
	char log_name[200];
	
	while(1) {
		send_len = sizeof(send_addr);
		if ((recv_len = recvfrom(recv_soc, buffer, BUFSIZE, 0, (struct sockaddr *)&send_addr, &send_len)) == -1) {
			perror("recvfrom failed");
			exit(1);
		}

		memset(log_name, 0, 200);
		sprintf(log_name, "%s_receiving_log.txt", buffer + 24);
		fp = fopen(log_name, "a");

		// 1. packet parsing (extract header and payload)
		if (parse_packet(buffer, recv_len, fp) < 0) {
			fclose(fp);
		 continue;	
		}
		send_addr.sin_port = htons(10070);

		// 2. send ACK ?
		fclose(fp);
		if (sendto(recv_soc, buffer, 24, 0, (struct sockaddr *)&send_addr,sizeof(send_addr)) != 24) {
			perror("sendto failed");
			exit(1);
		}
	}
}
