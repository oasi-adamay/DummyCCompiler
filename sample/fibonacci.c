#include <stdio.h>

int fibonacci(int n){
	int ret;
	if(n==0) ret = 0;
	else if(n==1) ret = 1;
	else ret = fibonacci(n - 2) + fibonacci(n - 1);
	return ret;
}

int main(){
	int ret;
	int n;
	n=10;
	ret = fibonacci(n);
	printf("fibonacci(%d):%d\n",n,ret);
//	printnum(n);
//	printnum(ret);
	return ret;
}
