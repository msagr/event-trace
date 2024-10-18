#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>

#define PROC_DIR "/proc"
#define LOG_FILE "app_start_events.log"
#define MAX_PATH_LENGTH 512
#define LOG_INTERVAL 10  
#define MAX_LOG_SIZE (2 * 1024 * 1024)

typedef struct {
    int newConnectionsCount;
    int closedConnectionsCount;
} ConnectionSummary;

void logEvent(const char *eventDescription) {
    FILE *logFile = fopen(LOG_FILE, "a");
    if (logFile) {
        time_t now = time(NULL);
        fprintf(logFile, "[%s] %s\n", ctime(&now), eventDescription);
        fclose(logFile);
    }
}

void checkLogSizeAndTerminate() {
    struct stat st;
    if (stat(LOG_FILE, &st) == 0) {
        if (st.st_size > MAX_LOG_SIZE) {
            logEvent("Log file size exceeded 2 MB. Terminating the application.");
            exit(1);
        }
    }
}

void getCommandLine(int pid) {
    char path[MAX_PATH_LENGTH];
    snprintf(path, sizeof(path), "%s/%d/cmdline", PROC_DIR, pid);
    FILE *file = fopen(path, "r");
    if (file) {
        char cmdline[1024];
        fgets(cmdline, sizeof(cmdline), file);
        fclose(file);
        char *token = strtok(cmdline, "\0");
        while (token != NULL) {
            logEvent(token);
            token = strtok(NULL, "\0");
        }
    }
}

int getNetworkConnections(int pid, char ***connections) {
    char path[MAX_PATH_LENGTH];
    snprintf(path, sizeof(path), "%s/%d/net/tcp", PROC_DIR, pid);
    FILE *file = fopen(path, "r");
    if (!file) return 0;

    char line[256];
    int count = 0;
    int maxConnections = 100;
    *connections = (char **)malloc(maxConnections * sizeof(char *));

    fgets(line, sizeof(line), file);

    while (fgets(line, sizeof(line), file)) {
        if (count >= maxConnections) {
            maxConnections *= 2;
            *connections = (char **)realloc(*connections, maxConnections * sizeof(char *));
        }
        (*connections)[count] = strdup(line);  
        count++;
    }

    fclose(file);
    return count;
}

void freeNetworkConnections(char **connections, int count) {
    for (int i = 0; i < count; i++) {
        free(connections[i]);
    }
    free(connections);
}

ConnectionSummary compareAndSummarizeNetworkChanges(char **oldConnections, int oldCount, char **newConnections, int newCount) {
    ConnectionSummary summary = {0, 0};
    int i, j;
    int found;

    for (i = 0; i < newCount; i++) {
        found = 0;
        for (j = 0; j < oldCount; j++) {
            if (strcmp(newConnections[i], oldConnections[j]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            summary.newConnectionsCount++;
        }
    }

    for (i = 0; i < oldCount; i++) {
        found = 0;
        for (j = 0; j < newCount; j++) {
            if (strcmp(oldConnections[i], newConnections[j]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            summary.closedConnectionsCount++;
        }
    }

    return summary;
}

void getMemoryUsage(int pid, long *vmSize, long *vmRSS) {
    char path[MAX_PATH_LENGTH];
    snprintf(path, sizeof(path), "%s/%d/status", PROC_DIR, pid);
    FILE *file = fopen(path, "r");
    if (file) {
        char line[256];
        while (fgets(line, sizeof(line), file)) {
            if (strncmp(line, "VmSize:", 7) == 0) {
                sscanf(line + 7, "%ld", vmSize);
            } else if (strncmp(line, "VmRSS:", 6) == 0) {
                sscanf(line + 6, "%ld", vmRSS);
            }
        }
        fclose(file);
    }
}

void logMemoryUsage(long vmSize, long vmRSS) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "Memory Usage: VmSize = %ld KB, VmRSS = %ld KB", vmSize, vmRSS);
    logEvent(buffer);
}

int isProcessRunning(int pid) {
    char path[MAX_PATH_LENGTH];
    snprintf(path, sizeof(path), "%s/%d", PROC_DIR, pid);
    return access(path, F_OK) == 0; 
}

void logMemoryFreed(long oldVmSize, long oldVmRSS, long finalVmSize, long finalVmRSS) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "Memory Freed: VmSize = %ld KB, VmRSS = %ld KB", 
             oldVmSize - finalVmSize, oldVmRSS - finalVmRSS);
    logEvent(buffer);
}

void logNetworkSummary(ConnectionSummary summary) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "Network Changes: New Connections = %d, Closed Connections = %d",
             summary.newConnectionsCount, summary.closedConnectionsCount);
    logEvent(buffer);
}

int listProcessesAndChoose() {
    DIR *dir = opendir(PROC_DIR);
    struct dirent *entry;
    int count = 0;
    char processNames[1024][MAX_PATH_LENGTH];
    int pids[1024];

    while ((entry = readdir(dir)) != NULL) {
        if (isdigit(entry->d_name[0])) {
            char processName[MAX_PATH_LENGTH];
            snprintf(processName, sizeof(processName), "%s/%s/comm", PROC_DIR, entry->d_name);
            FILE *file = fopen(processName, "r");
            if (file) {
                fgets(processName, sizeof(processName), file);
                processName[strcspn(processName, "\n")] = 0;
                printf("[%d] %s (PID: %s)\n", count + 1, processName, entry->d_name);
                strcpy(processNames[count], processName);
                pids[count] = atoi(entry->d_name);
                count++;
                fclose(file);
            }
        }
    }

    closedir(dir);

    if (count == 0) {
        printf("No processes found.\n");
        return -1;
    }

    int choice;
    printf("Choose a process to track (Enter the number): ");
    scanf("%d", &choice);

    if (choice > 0 && choice <= count) {
        return pids[choice - 1];
    } else {
        printf("Invalid choice.\n");
        return -1;
    }
}

int main() {
    int pid = listProcessesAndChoose();

    if (pid == -1) {
        printf("No valid process selected.\n");
        return 1;
    }

    FILE *logFile = fopen(LOG_FILE, "w");
    if (logFile) {
        fprintf(logFile, "Monitoring log for PID: %d\n", pid);
        fprintf(logFile, "-----------------------------------\n");
        fclose(logFile);
    }

    logEvent("Process Creation: Application started");
    logEvent("Initialization: Application is initializing");
    getCommandLine(pid);
    logEvent("Event Logging: Application start event logged");

    char **oldConnections = NULL;
    int oldCount = getNetworkConnections(pid, &oldConnections);

    long oldVmSize = 0, oldVmRSS = 0;
    getMemoryUsage(pid, &oldVmSize, &oldVmRSS);
    logMemoryUsage(oldVmSize, oldVmRSS);  

    time_t lastLogTime = time(NULL);

    while (isProcessRunning(pid)) { 
        checkLogSizeAndTerminate();
        time_t currentTime = time(NULL);
        if (difftime(currentTime, lastLogTime) >= LOG_INTERVAL) {
            char **newConnections = NULL;
            int newCount = getNetworkConnections(pid, &newConnections);

            ConnectionSummary summary = compareAndSummarizeNetworkChanges(oldConnections, oldCount, newConnections, newCount);
            logNetworkSummary(summary);

            freeNetworkConnections(oldConnections, oldCount);
            oldConnections = newConnections;
            oldCount = newCount;

            long newVmSize = 0, newVmRSS = 0;
            getMemoryUsage(pid, &newVmSize, &newVmRSS);
            logMemoryUsage(newVmSize, newVmRSS);

            lastLogTime = currentTime;
        }

        sleep(5);  
    }

    logEvent("Process Exit: Application has terminated");

    long finalVmSize = 0, finalVmRSS = 0;
    getMemoryUsage(pid, &finalVmSize, &finalVmRSS);

        logMemoryFreed(oldVmSize, oldVmRSS, finalVmSize, finalVmRSS);

    if (oldConnections) {
        freeNetworkConnections(oldConnections, oldCount);
    }

    logEvent("Final Log: Memory freed and connections released.");
    logEvent("Application Terminated: Logging session complete.");

    return 0;
}

