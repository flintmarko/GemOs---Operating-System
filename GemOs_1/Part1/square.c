#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char* argv[])
{
    if(argc < 2)
    {
        printf("Unable to execute\n");
        exit(-1);
    }
    long long num = atoll(argv[ argc - 1]);
    long long result = num * num;
    char buff[20];
    sprintf(buff, "%lld", result);
    char* new_argv[argc];
    for (int i = 0; i < argc - 2; i++) 
    {
        new_argv[i] = argv[i+1];
    }
    new_argv[argc - 2] = buff;
    new_argv[argc - 1] = NULL;
    if(argc == 2)
    {
        printf("%lld\n", result);
        exit(0);
    }
    else
    {
       if(execv(argv[1], new_argv) == -1)
       {        
         printf("Unable to execute \n");
         exit(-1);
       }
    }
}


