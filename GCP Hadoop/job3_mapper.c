#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

typedef struct { 
    char word[256]; 
    int df; 
} DFEntry;

int main() {
    FILE *df_file = fopen("df.txt", "r");
    if (!df_file) { fprintf(stderr,"Cannot open df.txt\n"); return 1; }

    // Read total document count
    int N = 0;
    FILE *N_file = fopen("N.txt","r");
    if (!N_file) { fprintf(stderr,"Cannot open N.txt\n"); return 1; }
    fscanf(N_file, "%d", &N);
    fclose(N_file);

    // Count how many lines in df.txt
    char line[1024];
    int df_count = 0;

    while (fgets(line, sizeof(line), df_file)) df_count++;
    rewind(df_file);

    // Allocate exact-size array
    DFEntry *df_entries = malloc(df_count * sizeof(DFEntry));
    if (!df_entries) { fprintf(stderr,"malloc failed\n"); return 1; }

    // Load df entries safely
    int idx = 0;
    while (fgets(line, sizeof(line), df_file)) {
        sscanf(line, "%255s\t%d", df_entries[idx].word, &df_entries[idx].df);
        idx++;
    }
    fclose(df_file);

    size_t total_bytes = 0;
    long record_count = 0;
    clock_t start = clock();

    // Process mapper input
    while (fgets(line, sizeof(line), stdin)) {
        total_bytes += strlen(line);
        record_count++;

        char word[256]; 
        int tf;

        if (sscanf(line, "%255[^\t]\t%d", word, &tf) != 2) continue;

        int df = 1;
        for (int i = 0; i < df_count; i++) {
            if (strcmp(word, df_entries[i].word) == 0) {
                df = df_entries[i].df;
                break;
            }
        }

        double tfidf = tf * log((double)N / (df + 1));
        printf("%s\t%.6f\n", word, tfidf);
    }

    clock_t end = clock();
    double elapsed = (double)(end - start)/CLOCKS_PER_SEC;

    fprintf(stderr,
        "Job3 Mapper processed %.2f MB in %.2f sec => %.2f MB/s, latency %.6f ms/record\n",
        total_bytes/1024.0/1024.0,
        elapsed,
        (total_bytes/1024.0/1024.0)/elapsed,
        (elapsed*1000.0)/record_count
    );

    free(df_entries);
    return 0;
}
