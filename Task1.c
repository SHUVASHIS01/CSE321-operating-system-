#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    // filename ache naki
    if (argc != 2) {
        printf("Usage: %s <filename>\n", argv[0]);
        return 1;
    }
    //  append mode e (file na thakle create korbe)
    FILE *outputFile = fopen(argv[1], "a");

    if (outputFile == NULL) {
        printf("Error: Could not open or create the file.\n");
        return 1;
    }
    char userInput[100];
    while (1) {
        printf("Enter a string (type -1 to stop): ");
        fgets(userInput, sizeof(userInput), stdin);
        // Newline character remove
        userInput[strcspn(userInput, "\n")] = 0;
        // Exit condition check kri
        if (strcmp(userInput, "-1") == 0) {
            break;}
        // Input ta file e likha
        fprintf(outputFile, "%s\n", userInput);
    }
    
    fclose(outputFile);
    printf("Strings saved to '%s'.\n", argv[1]);
    return 0;
}
