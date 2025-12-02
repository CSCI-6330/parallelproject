#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

int main() {
    char line[8192];
    size_t total_bytes = 0;

    /* --- metrics --- */
    clock_t start = clock();
    long long line_count = 0;
    double total_line_latency = 0.0;

    while (fgets(line, sizeof(line), stdin)) {

        size_t len = strlen(line);
        total_bytes += len;

        clock_t line_start = clock();

        /* tokenize */
        char *token = strtok(line, " \t\n\r");
        while (token) {
            for (int i = 0; token[i]; i++) token[i] = tolower(token[i]);
            printf("%s\t1\n", token);
            token = strtok(NULL, " \t\n\r");
        }

        clock_t line_end = clock();
        double line_latency = (double)(line_end - line_start) / CLOCKS_PER_SEC;
        total_line_latency += line_latency;
        line_count++;
    }

    clock_t end = clock();
    double total_time = (double)(end - start) / CLOCKS_PER_SEC;

    /* throughput */
    double mb = total_bytes / 1024.0 / 1024.0;
    double throughput = mb / total_time;

    /* average latency */
    double avg_latency = (line_count > 0) ? total_line_latency / line_count : 0.0;

    fprintf(stderr,
        "===== Job1 Mapper Metrics =====\n"
        "Total Running Time: %.6f sec\n"
        "Processed: %.2f MB\n"
        "Throughput: %.2f MB/s\n"
        "Lines Processed: %lld\n"
        "Average Line Latency: %.9f sec\n",
        total_time,
        mb,
        throughput,
        line_count,
        avg_latency
    );

    return 0;
}
