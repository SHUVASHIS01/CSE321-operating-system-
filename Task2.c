#include <stdio.h>
#include <unistd.h>

int createChildProcesses(int count) {
    int pid = getpid();
    if (pid % 2 != 0) {
        fork();
        count++;
    }
    return count;
}

int main() {
    int a, b, c;
    int count = 1; 

    a = fork();
    b = fork();
    c = fork();

    if (a == 0) {
        
        count = createChildProcesses(count);
    } else if (b == 0) {
        
        count = createChildProcesses(count);
    } else if (c == 0) {
        
        count = createChildProcesses(count);
    } else {
        
        count += 3; 

        printf("Total processes created: %d\n", count);
    }

    return 0;
}