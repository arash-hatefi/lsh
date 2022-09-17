#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>







void SigintHandler(int signum)
{	
  printf("interrupt recieved \n");
}



int main()
{
	// int a = fork();
	// if(a==0)
	// {
	// 	printf("from child: %d\n", f());
	// }
	// else{
	// printf("from parrent: %d\n", f());	
	// }
	printf("%d\n\n\n", getppid());
  signal(SIGINT, SigintHandler);

	fork();
	fork();
	fork();
	while(1) {sleep(1);}
	return 0;
}
