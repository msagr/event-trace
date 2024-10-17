#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>  

#define PROC_DIR "/proc"
#define LOG_FILE "process_log.txt"

int isNumeric(const char *str) {
    while (*str) {
        if (*str < '0' || *str > '9') {
            return 0;
        }
        str++;
    }
    return 1;
}

void logProcessNameAndStatus(char *pid, FILE *logFile) {
    char path[256];
    char name[256];
    char status[256];
    char state[32];
    FILE *file;

    snprintf(path, sizeof(path), "%s/%s/comm", PROC_DIR, pid);
    file = fopen(path, "r");
    if (file) {
        fgets(name, sizeof(name), file);
        fclose(file);
        name[strcspn(name, "\n")] = 0;
    } else {
        strcpy(name, "Unknown");
    }

    snprintf(path, sizeof(path), "%s/%s/status", PROC_DIR, pid);
    file = fopen(path, "r");
    if (file) {
        while (fgets(status, sizeof(status), file)) {
            if (strncmp(status, "State:", 6) == 0) {
                sscanf(status, "State: %31[^\n]", state);  // Read the state information
                break;
            }
        }
        fclose(file);
    } else {
        strcpy(state, "Unknown");
    }

    fprintf(logFile, "PID: %s, Name: %s, Status: %s\n", pid, name, state);
}

void clearLogFile() {
    FILE *logFile = fopen(LOG_FILE, "w");
    if (logFile) {
        fprintf(logFile, "Currently running processes (updated in real-time):\n");
        fprintf(logFile, "-----------------------------------\n");
        fclose(logFile);
    }
}

int main() {
    struct dirent *entry;
    DIR *dir;

    clearLogFile();

    FILE *logFile = fopen(LOG_FILE, "a");
    if (!logFile) {
        perror("Failed to open log file");
        return 1;
    }

    while (1) {
        dir = opendir(PROC_DIR);
        if (!dir) {
            perror("opendir failed");
            fclose(logFile);
            return 1;
        }

        while ((entry = readdir(dir)) != NULL) {
            if (isNumeric(entry->d_name)) {
                logProcessNameAndStatus(entry->d_name, logFile);
            }
        }

        closedir(dir);
        fflush(logFile); 
        sleep(5);
    }

    fclose(logFile);
    return 0;
}
