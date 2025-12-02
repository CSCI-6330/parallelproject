#include <stdio.h>
#include <string.h>
#include <time.h>

int main() {
    char line[2048];
    size_t total_bytes = 0;
    long long total_records = 0;

    clock_t start = clock();

    while (fgets(line, sizeof(line), stdin)) {
        total_bytes += strlen(line);
        total_records++;

        char word[1024];
        int tf;

        // parse: word   tf
        char *tab = strchr(line, '\t');
        if (!tab) continue;

        *tab = '\0';
        strcpy(word, line);
        tf = 1;

        // emit word -> 1 (document frequency)
        printf("%s\t1\n", word);
    }

    clock_t end = clock();

    double elapsed = (double)(end - start)/CLOCKS_PER_SEC;
    double mb = total_bytes / 1024.0 / 1024.0;
    double throughput = (elapsed > 0 ? mb / elapsed : 0);
    double latency = (total_records > 0 ? (elapsed / total_records) * 1000.0 : 0);

    fprintf(stderr,
        "\n===== Job2 Mapper Metrics =====\n"
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
