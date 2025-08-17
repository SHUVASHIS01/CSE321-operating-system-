#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <numbers>\n", argv[0]);
        return 1;
    }

    char tempStr[100];
    pid_t childPid = fork();

    if (childPid == 0) {
        char *sortArgs[argc + 1];
        sortArgs[0] = "./sort";

        for (int i = 1; i < argc; i++) {
            sprintf(tempStr, "%s", argv[i]);
            sortArgs[i] = strdup(tempStr);
        }
        sortArgs[argc] = NULL;

        execvp(sortArgs[0], sortArgs);
        perror("exec failed");
        exit(1);
    } else {
        wait(NULL);

        char *evenOddArgs[argc + 1];
        evenOddArgs[0] = "./oddeven";

        for (int i = 1; i < argc; i++) {
            sprintf(tempStr, "%s", argv[i]);
            evenOddArgs[i] = strdup(tempStr);
        }
        evenOddArgs[argc] = NULL;

        execvp(evenOddArgs[0], evenOddArgs);
        perror("exec failed");
        exit(1);
    }

    return 0;
}
