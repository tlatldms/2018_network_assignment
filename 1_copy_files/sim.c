#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

int Done() { //log.txt.파일 생성
	FILE *fptr;
	fptr = fopen("log.txt", "w");
	if (fptr == NULL)
	{
		printf("Error!");
		return -1;
	}

	fprintf(fptr, "%s", "file copy is done");
	fclose(fptr);

	return 0;
}

int Copy(char *existed_file, char *new_file) { //binary mode로 파일 복사
	FILE *oldptr, *newptr;
	errno_t error_1 = 0, error_2 = 0;
	int temp;

	error_1 = fopen_s(&oldptr, existed_file, "rb"); //fopen_s로 파일 포인터를 알려주고 binary mode로 읽기와 쓰기
	error_2 = fopen_s(&newptr, new_file, "wb"); //error를 정의해줌

	if (error_1 != 0) //error가 0이여야 성공한것
		return -1;

	if (error_2 != 0)
		return -1;

	
	while (1) {
		temp = fgetc(oldptr); // end of file을 만나기 전까지 oldptr을 읽어온 temp를 newptr 뒤에 이어 씀
		
		if (!feof(oldptr)) 
			fputc(temp, newptr);
		else {
			Done();
			break;
		}
	}

	fclose(oldptr);
	fclose(newptr);
	return 0;
}

int main() {
	char oldn[101], newn[101];
	printf("File to be copied : ");
	gets_s(oldn, 100);

	printf("Name for new file : ");
	gets_s(newn, 100);

	if (Copy(oldn, newn) == 0) //성공하면
		printf("Success!");
	else
		printf("Fail!");
}
