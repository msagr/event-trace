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
#define MAX_PROCESSES 1024

typedef struct {
    char name[MAX_PATH_LENGTH];
    int count;
    int pids[MAX_PROCESSES];
} ProcessInfo;

const char *targetProcesses[] = {
    "chrome",  // Name for Chrome
    "code",    // Name for VS Code
    "bash"     // Name for Bash
};
const int targetProcessCount = sizeof(targetProcesses) / sizeof(targetProcesses[0]);

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

int isTargetProcess(const char *name) {
    for (int i = 0; i < targetProcessCount; i++) {
        if (strstr(name, targetProcesses[i]) != NULL) {
            return 1; // Found a target process
        }
    }
    return 0; // Not a target process
}

int listActiveProcessesAndChoose(ProcessInfo *processes, int *processCount) {
    DIR *dir = opendir(PROC_DIR);
    struct dirent *entry;
    *processCount = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (isdigit(entry->d_name[0])) {
            int pid = atoi(entry->d_name);
            if (!isProcessRunning(pid)) {
                continue; // Skip non-running processes
            }

            char processName[MAX_PATH_LENGTH];
            snprintf(processName, sizeof(processName), "%s/%s/comm", PROC_DIR, entry->d_name);
            FILE *file = fopen(processName, "r");
            if (file) {
                char name[256];
                fgets(name, sizeof(name), file);
                name[strcspn(name, "\n")] = 0;

                // Check if the process name is one of the target processes
                if (isTargetProcess(name)) {
                    // Check if the process name already exists in the list
                    int found = 0;
                    for (int i = 0; i < *processCount; i++) {
                        if (strcmp(processes[i].name, name) == 0) {
                            processes[i].pids[processes[i].count++] = pid;
                            found = 1;
                            break;
                        }
                    }

                    // If not found, create a new entry
                    if (!found && *processCount < MAX_PROCESSES) {
                        strcpy(processes[*processCount].name, name);
                        processes[*processCount].pids[0] = pid;
                        processes[*processCount].count = 1;
                        (*processCount)++;
                    }
                }
                fclose(file);
            }
        }
    }

    closedir(dir);

    if (*processCount == 0) {
        printf("No target active processes found.\n");
        return -1;
    }

    // Display unique processes with their counts
    printf("Active Target Processes:\n");
    for (int i = 0; i < *processCount; i++) {
        printf("[%d] %s (Processes: %d)\n", i + 1, processes[i].name, processes[i].count);
    }

    int choice;
    printf("Choose a process to track (Enter the number): ");
    scanf("%d", &choice);

    if (choice > 0 && choice <= *processCount) {
        return processes[choice - 1].pids[0];  // Return the first PID of the selected process
    } else {
        printf("Invalid choice.\n");
        return -1;
    }
}

int main() {
    ProcessInfo processes[MAX_PROCESSES];
    int processCount = 0;

    int pid = listActiveProcessesAndChoose(processes, &processCount);

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

    long oldVmSize = 0, oldVmRSS = 0;
    getMemoryUsage(pid, &oldVmSize, &oldVmRSS);

    while (isProcessRunning(pid)) {
        checkLogSizeAndTerminate();

        // Log command line arguments
        getCommandLine(pid);

        // Get and log memory usage
        long vmSize, vmRSS;
        getMemoryUsage(pid, &vmSize, &vmRSS);
        logMemoryUsage(vmSize, vmRSS);

        // Sleep for a specified interval before the next check
        sleep(LOG_INTERVAL);
    }

    // After the process exits, log memory freed
    long finalVmSize, finalVmRSS;
    getMemoryUsage(pid, &finalVmSize, &finalVmRSS);
    logMemoryFreed(oldVmSize, oldVmRSS, finalVmSize, finalVmRSS);

    return 0;
}
