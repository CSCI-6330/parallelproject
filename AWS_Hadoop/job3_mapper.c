#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define WORD_MAX 256
#define LOAD_FACTOR 0.7

typedef struct {
    char word[WORD_MAX];
    int df;
    int used;
} HashEntry;

static unsigned long hash_str(const char *s) {
    unsigned long h = 5381;
    int c;
    while ((c = *s++)) {
        h = ((h << 5) + h) + c; // h*33 + c
    }
    return h;
}

int main() {
    FILE *df_file = fopen("df.txt", "r");
    if (!df_file) { fprintf(stderr,"Cannot open df.txt\n"); return 1; }

    // Read total document count N
    int N = 0;
    FILE *N_file = fopen("N.txt","r");
    if (!N_file) { fprintf(stderr,"Cannot open N.txt\n"); return 1; }
    fscanf(N_file, "%d", &N);
    fclose(N_file);

    // Count df entries
    char line[1024];
    int df_count = 0;
    while (fgets(line, sizeof(line), df_file)) df_count++;
    rewind(df_file);

    // Create hash table (size = df_count / load_factor)
    int table_size = (int)(df_count / LOAD_FACTOR);
    HashEntry *table = calloc(table_size, sizeof(HashEntry));
    if (!table) { fprintf(stderr,"Hash table alloc failed\n"); return 1; }

    // Insert df entries into hash table
    int inserted = 0;
    while (fgets(line, sizeof(line), df_file)) {
        char w[WORD_MAX];
        int df;
        sscanf(line, "%255s\t%d", w, &df);

        unsigned long h = hash_str(w) % table_size;
        while (table[h].used) {
            h = (h + 1) % table_size;
        }
        strcpy(table[h].word, w);
        table[h].df = df;
        table[h].used = 1;
        inserted++;
    }
    fclose(df_file);

    fprintf(stderr, "Loaded %d DF entries into hash table (size=%d)\n",
            inserted, table_size);

    // Process stdin
    size_t total_bytes = 0;
    long record_count = 0;
    clock_t start = clock();

    while (fgets(line, sizeof(line), stdin)) {
        total_bytes += strlen(line);
        record_count++;

        char word[WORD_MAX];
        int tf;

        if (sscanf(line, "%255[^\t]\t%d", word, &tf) != 2) continue;

        int df = 1;

        unsigned long h = hash_str(word) % table_size;
        while (table[h].used) {
            if (strcmp(table[h].word, word) == 0) {
                df = table[h].df;
                break;
            }
            h = (h + 1) % table_size;
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

    free(table);
    return 0;
}
