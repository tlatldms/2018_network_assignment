#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <pthread.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

pthread_mutex_t mutex;

// pthread_create 파라미터 패싱을 위한 struct 정의
typedef struct {
	char org[128];
	char new[128];
	struct timeval start;
} FileInfo;

// struct timeval의 값을 초단위의 float 자료형으로 변환
float get_sec(struct timeval start) {
	struct timeval end;
	gettimeofday(&end, NULL);
	float usec = ((float)end.tv_usec - start.tv_usec)/1000000;
	float sec = end.tv_sec - start.tv_sec;
	return sec+usec;
}

// 실제 Copy 작업을 수행 (pthread_creat 함수로부터 실행됨)
void *Copy(void *args) {
	
	FileInfo *file_info = args;
	char *org = file_info->org;
	char *new = file_info->new;
	char buf[4100];
	int len;
	int fr = open(org, O_RDONLY | O_NONBLOCK);
	int fw = open(new, O_WRONLY | O_CREAT | O_NONBLOCK, S_IRWXU | S_IRWXG | S_IRWXO);
	
	if (fr < 0) {
		FILE* fp = fopen("log.txt", "a");
		fprintf(stderr, "%s file is not opened!\n", org);
		return NULL;
		fclose(fp);
	}


	// 로그파일 작성 (copy 시작)
	// 시간 순서대로 로깅하는 것을 보장하기 위한 mutex lock
	pthread_mutex_lock(&mutex);
	FILE* fp = fopen("log.txt", "a");
	fprintf(fp, "%.2f\tStart copying %s to %s\n", get_sec(file_info->start), org, new);
	fclose(fp);
	pthread_mutex_unlock(&mutex);

	// 파일 copy (4096bytes 단위로 수행)
	while ((len = read(fr, buf, 4096)) > 0) {
		write(fw, buf, len);
	}

	// 로그파일 작성 (copy 종료)
	pthread_mutex_lock(&mutex);
	fp = fopen("log.txt", "a");
	fprintf(fp, "%.2f\t%s is copied completely\n", get_sec(file_info->start), new);
	fclose(fp);
	pthread_mutex_unlock(&mutex);

	close(fr);
	close(fw);
}

int main() {
	pthread_t tid;
	struct timeval start;

	pthread_mutex_init(&mutex, NULL);
	gettimeofday(&start, NULL);

	while (1) {
		FileInfo *file_info = malloc(sizeof(FileInfo));
		file_info->start = start;

		printf("Input the file name: ");
		int ret = scanf("%s", file_info->org);
		if (ret <= 0) {
			break;
		}
		printf("Input the new name: ");
		scanf("%s", file_info->new);

		pthread_create(&tid, NULL, Copy, file_info);
		pthread_detach(tid);
	}

	pthread_exit(NULL);
}
