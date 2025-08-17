#include <stdio.h>
#include <unistd.h>

// Odd PID hole ekta child create
int createChildProcesses(int processCounter) {
    int currentProcessID = getpid();
    if (currentProcessID % 2 != 0) {
        fork();
        processCounter++;
    }
    return processCounter;
}

int main() {
    int a, b, c;
    int totalProcessCount = 1;  // Main process dhore 1

    a = fork();  
    b = fork();  
    c = fork();  

    if (a == 0) {
        totalProcessCount = createChildProcesses(totalProcessCount);
    } else if (b == 0) {
        totalProcessCount = createChildProcesses(totalProcessCount);
    } else if (c == 0) {
        totalProcessCount = createChildProcesses(totalProcessCount);
    } else {
        totalProcessCount += 3;  // 3 ta fork er jonno
        printf("Total processes created: %d\n", totalProcessCount);
    }

    return 0;
}
