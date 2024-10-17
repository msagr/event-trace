#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>

#define PROC_DIR "/proc"
#define LOG_FILE "app_start_events.log"
#define MAX_PATH_LENGTH 512

// Function to log application events
void logEvent(const char *eventDescription) {
    FILE *logFile = fopen(LOG_FILE, "a");
    if (logFile) {
        time_t now = time(NULL);
        fprintf(logFile, "[%s] %s\n", ctime(&now), eventDescription);
        fclose(logFile);
    }
}

// Function to capture and log command-line arguments of the process
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

// Helper function to get current network connections and return them as a dynamically allocated string array
int getNetworkConnections(int pid, char ***connections) {
    char path[MAX_PATH_LENGTH];
    snprintf(path, sizeof(path), "%s/%d/net/tcp", PROC_DIR, pid);
    FILE *file = fopen(path, "r");
    if (!file) return 0;

    char line[256];
    int count = 0;
    int maxConnections = 100;
    *connections = (char **)malloc(maxConnections * sizeof(char *));

    // Skip the first header line
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

// Helper function to free the allocated memory for connections
void freeNetworkConnections(char **connections, int count) {
    for (int i = 0; i < count; i++) {
        free(connections[i]);
    }
    free(connections);
}

// Function to compare the old and new network connection states and log any changes
void compareAndLogNetworkChanges(char **oldConnections, int oldCount, char **newConnections, int newCount) {
    int i, j;
    int found;

    // Check for new connections
    for (i = 0; i < newCount; i++) {
        found = 0;
        for (j = 0; j < oldCount; j++) {
            if (strcmp(newConnections[i], oldConnections[j]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            logEvent("New Connection Detected:");
            logEvent(newConnections[i]);
        }
    }

    // Check for closed connections
    for (i = 0; i < oldCount; i++) {
        found = 0;
        for (j = 0; j < newCount; j++) {
            if (strcmp(oldConnections[i], newConnections[j]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            logEvent("Connection Closed:");
            logEvent(oldConnections[i]);
        }
    }
}

// Function to find the PID of the specified application
int findProcessId(const char *appName) {
    DIR *dir = opendir(PROC_DIR);
    struct dirent *entry;
    int pid = -1;

    while ((entry = readdir(dir)) != NULL) {
        if (isdigit(entry->d_name[0])) {
            char processName[MAX_PATH_LENGTH];
            snprintf(processName, sizeof(processName), "%s/%s/comm", PROC_DIR, entry->d_name);
            FILE *file = fopen(processName, "r");
            if (file) {
                fgets(processName, sizeof(processName), file);
                processName[strcspn(processName, "\n")] = 0;
                if (strcmp(processName, appName) == 0) {
                    pid = atoi(entry->d_name);
                    fclose(file);
                    break;
                }
                fclose(file);
            }
        }
    }

    closedir(dir);
    return pid;
}

// Function to retrieve memory usage of the process
void getMemoryUsage(int pid, long *vmSize, long *vmRSS) {
    char path[MAX_PATH_LENGTH];
    snprintf(path, sizeof(path), "%s/%d/status", PROC_DIR, pid);
    FILE *file = fopen(path, "r");
    if (file) {
        char line[256];
        while (fgets(line, sizeof(line), file)) {
            if (strncmp(line, "VmSize:", 7) == 0) {
                sscanf(line + 7, "%ld", vmSize); // Virtual memory size in KB
            } else if (strncmp(line, "VmRSS:", 6) == 0) {
                sscanf(line + 6, "%ld", vmRSS); // Resident Set Size (physical memory) in KB
            }
        }
        fclose(file);
    }
}

// Function to log memory usage
void logMemoryUsage(long vmSize, long vmRSS) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "Memory Usage: VmSize = %ld KB, VmRSS = %ld KB", vmSize, vmRSS);
    logEvent(buffer);
}

int main() {
    const char *appName = "chrome";  // Specify your application name
    int pid = findProcessId(appName);

    if (pid == -1) {
        printf("Application %s not found.\n", appName);
        return 1;
    }

    FILE *logFile = fopen(LOG_FILE, "w");
    if (logFile) {
        fprintf(logFile, "Monitoring log for %s (PID: %d)\n", appName, pid);
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
    logMemoryUsage(oldVmSize, oldVmRSS);  // Log initial memory usage

    while (1) {
        char **newConnections = NULL;
        int newCount = getNetworkConnections(pid, &newConnections);

        compareAndLogNetworkChanges(oldConnections, oldCount, newConnections, newCount);

        // Free the old connections and replace with the new ones
        freeNetworkConnections(oldConnections, oldCount);
        oldConnections = newConnections;
        oldCount = newCount;

        // Check and log memory usage
        long newVmSize = 0, newVmRSS = 0;
        getMemoryUsage(pid, &newVmSize, &newVmRSS);
        logMemoryUsage(newVmSize, newVmRSS);

        sleep(5);  // Adjust the interval if needed
    }

    return 0;
}
