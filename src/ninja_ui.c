#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include "aake.h"

// Helper to determine color based on duration
static const char *get_color_for_time(double elapsed) {
    if (elapsed > 3600.0) return "\033[1;37m"; // White (> 1 hr)
    if (elapsed > 1800.0) return "\033[1;33m"; // Yellow (> 30 min)
    if (elapsed > 60.0) return "\033[1;31m";   // Orange (using Red for > 1 min)
    if (elapsed > 30.0) return "\033[1;33m";   // Yellow (> 30 sec)
    return "\033[1;32m";                       // Green (< 30 sec)
}

int run_ninja_ui(int jobs, bool verbose, const char *arch_str) {
    char cmd[512];
    if (jobs > 0) {
        snprintf(cmd, sizeof(cmd), "NINJA_STATUS=\"[%%s/%%t] %%e | \" ninja -C builddir -j %d", jobs);
    } else {
        snprintf(cmd, sizeof(cmd), "NINJA_STATUS=\"[%%s/%%t] %%e | \" ninja -C builddir");
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        perror("popen");
        return 1;
    }

    printf("\033[2J\033[H"); // Clear screen
    printf("BUILDING FOR : < %s >\n\n", arch_str);

    char line[1024];
    int failed = 0;
    time_t start_time = time(NULL);

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, "FAILED:", 7) == 0) {
            failed = 1;
            printf("\n\033[1;31m!WARN! : Build Failed : %s\033[0m", line + 8);
            continue;
        }

        // Parse: [12/50] 1.543 | filename
        if (line[0] == '[') {
            char *close_bracket = strchr(line, ']');
            if (close_bracket) {
                *close_bracket = '\0';
                char *progress = line + 1; // e.g., "12/50"
                
                char *bar = strchr(close_bracket + 1, '|');
                if (bar) {
                    *bar = '\0';
                    char *time_str = close_bracket + 1;
                    double elapsed = atof(time_str);
                    
                    char *file_info = bar + 1;
                    // Trim newline
                    char *nl = strchr(file_info, '\n');
                    if (nl) *nl = '\0';
                    
                    const char *color = get_color_for_time(elapsed);
                    
                    printf("\033[K[%s] %s\n", progress, file_info);
                    printf("\033[K      %s[ %.2fs ]\033[0m : %s (Task)\n", color, elapsed, file_info);
                    
                    // Move cursor up 2 lines to overwrite next time unless verbose
                    if (!verbose) {
                        printf("\033[2A");
                    }
                }
            }
        } else {
            // Print other ninja output (like compiler warnings)
            printf("\033[K%s", line);
        }
    }

    if (!verbose) {
        printf("\n\n"); // Move past the overwriting lines
    }

    int ret = pclose(fp);
    time_t end_time = time(NULL);
    int total_secs = (int)(end_time - start_time);
    int hours = total_secs / 3600;
    int mins = (total_secs % 3600) / 60;
    int secs = total_secs % 60;

    if (ret != 0 || failed) {
        printf("\n\033[1;31m# Build Failed (%02d:%02d:%02d)\033[0m\n", hours, mins, secs);
        return 1;
    } else {
        printf("\n\033[1;32m# Build Completed (%02d:%02d:%02d)\033[0m\n", hours, mins, secs);
        return 0;
    }
}
