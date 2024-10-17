#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>

#define PROC_DIR "/proc"

int isNumeric(const char *str) {
    while (*str) {
        if (*str < '0' || *str > '9') {
            return 0;
        }
        str++;
    }
    return 1;
}

void getProcessNameAndStatus(char *pid) {
    char path[256];
    char name[256];
    char status[256];
    FILE *file;

    snprintf(path, sizeof(path), "%s/%s/comm", PROC_DIR, pid);
    file = fopen(path, "r");
    if (file) {
        fgets(name, sizeof(name), file);
        fclose(file);
    } else {
        strcpy(name, "Unknown");
    }

    snprintf(path, sizeof(path), "%s/%s/status", PROC_DIR, pid);
    file = fopen(path, "r");
    if (file) {
        while (fgets(status, sizeof(status), file)) {
            if (strncmp(status, "State:", 6) == 0) {
                break;
            }
        }
        fclose(file);
    } else {
        strcpy(status, "State: Unknown");
    }

    printf("PID: %s, Name: %s, %s", pid, name, status);
}

int main() {
    struct dirent *entry;
    DIR *dir = opendir(PROC_DIR);

    if (!dir) {
        perror("opendir failed");
        return 1;
    }

    printf("Currently running processes:\n");
    printf("-----------------------------------\n");

    while ((entry = readdir(dir)) != NULL) {
        if (isNumeric(entry->d_name)) {
            getProcessNameAndStatus(entry->d_name);
        }
    }

    closedir(dir);
    return 0;
}
