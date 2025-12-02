#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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

        char word[1024];
        int val;

        if (sscanf(line, "%[^\t]\t%d", word, &val) != 2)
            continue;

        // new key
        if (strcmp(current_word, word) != 0) {
            if (current_word[0] != '\0') {
                printf("%s\t%d\n", current_word, count);
            }
            strcpy(current_word, word);
            count = val;
        } else {
            count += val;
        }
    }

    // final output
    if (current_word[0] != '\0') {
        printf("%s\t%d\n", current_word, count);
    }

    clock_t end = clock();

    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    double mb = total_bytes / 1024.0 / 1024.0;
    double throughput = (elapsed > 0 ? mb / elapsed : 0);
    double latency = (total_records > 0 ? (elapsed / total_records) * 1000.0 : 0);

    fprintf(stderr,
        "\n===== Job2 Reducer Metrics =====\n"
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
