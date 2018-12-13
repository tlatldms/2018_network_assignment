#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

int Done() { //log.txt.���� ����
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

int Copy(char *existed_file, char *new_file) { //binary mode�� ���� ����
	FILE *oldptr, *newptr;
	errno_t error_1 = 0, error_2 = 0;
	int temp;

	error_1 = fopen_s(&oldptr, existed_file, "rb"); //fopen_s�� ���� �����͸� �˷��ְ� binary mode�� �б�� ����
	error_2 = fopen_s(&newptr, new_file, "wb"); //error�� ��������

	if (error_1 != 0) //error�� 0�̿��� �����Ѱ�
		return -1;

	if (error_2 != 0)
		return -1;

	
	while (1) {
		temp = fgetc(oldptr); // end of file�� ������ ������ oldptr�� �о�� temp�� newptr �ڿ� �̾� ��
		
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

	if (Copy(oldn, newn) == 0) //�����ϸ�
		printf("Success!");
	else
		printf("Fail!");
}
