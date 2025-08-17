#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main()
{
    pid_t childPid = fork();  // child create

    if (childPid > 0) 
    {
        printf("1. Parent process ID: 0\n");
        wait(NULL);  // child wait korche
    }
    else if (childPid == 0) 
    {
        printf("2. Child process ID: %d\n", getpid());

        pid_t gc1 = fork();  // 1st grandchild
        if (gc1 == 0) 
        {
            printf("3. Grand Child process ID: %d\n", getpid());
            exit(0);
        } 
        else 
        {
            wait(NULL);

            pid_t gc2 = fork();  // 2nd grandchild
            if (gc2 == 0) 
            {
                printf("4. Grand Child process ID: %d\n", getpid());
                exit(0);
            } 
            else 
            {
                wait(NULL);

                pid_t gc3 = fork();  // 3rd grandchild
                if (gc3 == 0) 
                {
                    printf("5. Grand Child process ID: %d\n", getpid());
                    exit(0);
                } 
                else 
                {
                    wait(NULL);
                }
            }
        }

        exit(0);
    }

    return 0;
}
