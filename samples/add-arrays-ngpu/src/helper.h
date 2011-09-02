#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WIDTH 240
#define MAX_LINES 10000

// loads a file
void loadSource(char* path, char ***lines, int *count) {
	char curline[MAX_WIDTH + 1], *lines1[MAX_LINES];
	int count1 = 0;
	FILE *f = fopen(path, "r");
	if(!f) {
		printf("can\'t open file for reading\n");
		exit(-1);
	}
	while(fgets(curline, MAX_WIDTH, f)) {
		int len = strlen(curline);
		if(len == MAX_WIDTH) {
			printf("line is too long\n");
			exit(-1);
		}
		char *line = (char*)malloc(sizeof(char) * (len + 1));
		strcpy(line, curline);
		if(count1 > MAX_LINES) {
			printf("too many lines in file\n");
			exit(-1);
		}
		lines1[count1++] = line;
	}  // end of while
	fclose(f);
	*lines = (char**)malloc(sizeof(char**) * count1);
	memcpy(*lines, lines1, count1 * sizeof(char**));
	*count = count1;
}
