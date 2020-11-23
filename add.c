#include <stdio.h>
#include <stdlib.h>
int main(int num, char** argv){
	int a, b;
	scanf("%d%d", &a, &b);
	if(argv[1][0] == 'a'){
		printf("%d", a+b);
	}
	else if(argv[1][0] == 's'){
		printf("%d", a-b);
	}
	else if(argv[1][0] == 'm'){
		printf("%d", a*b);
	}
	else if(argv[1][0] == 'd'){
		printf("%d", a/b);
	}
	else if(argv[1][0] == 'e'){
		printf("%d %d", b, a);
	}
	return 0;
}
