#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main() {
    char line[2048];
    char current_word[1024] = "";
    int count = 0;

    size_t total_bytes = 0;
    long long total_records = 0;

    clock_t start = clock();

    while (fgets(line, sizeof(line), stdin)) {
        total_bytes += strlen(line);
        total_records++;

        // Parse: word \t value
        char word[1024];
        int value;
        char *tab = strchr(line, '\t');
        if (!tab) continue;

        *tab = '\0';
        strcpy(word, line);
        value = atoi(tab + 1);

        // First record or new word
        if (strcmp(current_word, word) != 0) {
            if (current_word[0] != '\0') {
                printf("%s\t%d\n", current_word, count);
            }
            strcpy(current_word, word);
            count = value;
        } else {
            count += value;
        }
    }

    // Output last word
    if (current_word[0] != '\0') {
        printf("%s\t%d\n", current_word, count);
    }

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

    double mb = total_bytes / 1024.0 / 1024.0;
    double throughput = (elapsed > 0 ? mb / elapsed : 0);
    double latency = (total_records > 0 ? (elapsed / total_records) * 1000.0 : 0); // ms/record

    fprintf(stderr,
        "\n===== Job1 Reducer Metrics =====\n"
        "Total Runtime: %.4f sec\n"
        "Total Input: %.2f MB\n"
        "Throughput: %.4f MB/s\n"
        "Total Records: %lld\n"
        "Latency: %.6f ms/record\n"
        "===============================\n",
        elapsed, mb, throughput, total_records, latency
    );

    return 0;
}
