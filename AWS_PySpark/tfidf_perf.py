#!/usr/bin/env python3
"""
TF–IDF performance benchmark on EMR (PySpark, RDD-based, no numpy)

Usage example (on EMR master node or as an EMR step):

  spark-submit tfidf_perf.py \
      --input s3://your-bucket/your-prefix/*.txt \
      --num-features 1000000 \
      --min-token-length 2 \
      --partitions 0

Notes:
- Each TEXT FILE in S3 is treated as a single document.
- This version does NOT use pyspark.ml, so it does not require numpy.
- The --num-features option is accepted for compatibility but unused.
"""

import argparse
import time
import re
import math
from collections import Counter

from pyspark.sql import SparkSession


def fmt_secs(s):
    if s < 1:
        return f"{s*1000:.2f} ms"
    elif s < 60:
        return f"{s:.3f} s"
    else:
        m = int(s // 60)
        r = s % 60
        return f"{m} min {r:.1f} s"


def main():
    parser = argparse.ArgumentParser(description="TF–IDF performance benchmark (RDD-based)")
    parser.add_argument(
        "--input", required=True,
        help="S3 path to txt files, e.g. s3://bucket/prefix/*.txt"
    )
    parser.add_argument(
        "--num-features", type=int, default=1_000_000,
        help="Unused (kept for CLI compatibility with previous version)."
    )
    parser.add_argument(
        "--min-token-length", type=int, default=2,
        help="Drop tokens shorter than this length (default: 2)"
    )
    parser.add_argument(
        "--partitions", type=int, default=0,
        help="Number of partitions for the documents. 0 = let Spark decide (default: 0)"
    )

    args = parser.parse_args()

    spark = (
        SparkSession.builder
        .appName("TFIDF_Performance_Benchmark_RDD")
        .getOrCreate()
    )
    sc = spark.sparkContext
    sc.setLogLevel("WARN")

    print("=== TF–IDF Performance Benchmark (RDD-based) ===")
    print(f"Input path        : {args.input}")
    print(f"minTokenLength    : {args.min_token_length}")
    print(f"partitions arg    : {args.partitions}")
    print("(Note: num-features argument is unused in this RDD-only version.)")
    print()

    t0 = time.perf_counter()

    # ----------------------------------------------------------------------
    # STEP 1: Read text files from S3 (each file is one document)
    # ----------------------------------------------------------------------
    t_read_start = time.perf_counter()

    # (path, text) pairs
    rdd_docs = sc.wholeTextFiles(args.input)

    # Control number of partitions if requested
    if args.partitions > 0:
        rdd_docs = rdd_docs.repartition(args.partitions)
    else:
        rdd_docs = rdd_docs.repartition(sc.defaultParallelism)

    # Cache in memory so later stages don't re-read from S3
    rdd_docs = rdd_docs.cache()

    # Force read and compute doc/char stats
    n_docs = rdd_docs.count()
    total_chars = rdd_docs.map(lambda x: len(x[1])).sum()
    total_mb = total_chars / (1024 * 1024)

    if n_docs == 0:
        raise RuntimeError(f"No documents found at input path: {args.input}")

    t_read_end = time.perf_counter()
    t_read = t_read_end - t_read_start

    print("STEP 1: Read & cache documents")
    print(f"  Documents        : {n_docs}")
    print(f"  Total chars (≈B) : {total_chars}")
    print(f"  Approx size (MB) : {total_mb:.2f}")
    print(f"  Time             : {fmt_secs(t_read)}")
    if t_read > 0:
        print(f"  Throughput       : {total_mb / t_read:.2f} MB/s")
        print(f"  Docs/sec         : {n_docs / t_read:.2f}")
    print()

    # ----------------------------------------------------------------------
    # STEP 2: Tokenization (RDD-only, regex-based)
    # ----------------------------------------------------------------------
    token_pattern = re.compile(r"\W+")
    min_len = args.min_token_length

    def tokenize(text):
        tokens = [
            t for t in token_pattern.split(text.lower())
            if len(t) >= min_len and t != ""
        ]
        return tokens

    t_tok_start = time.perf_counter()

    # (doc_id, tokens)
    rdd_tokens = rdd_docs.map(lambda x: (x[0], tokenize(x[1]))).cache()

    total_tokens = rdd_tokens.map(lambda x: len(x[1])).sum()
    avg_tokens_per_doc = total_tokens / n_docs if n_docs > 0 else 0.0

    t_tok_end = time.perf_counter()
    t_tok = t_tok_end - t_tok_start

    print("STEP 2: Tokenization")
    print(f"  Total tokens       : {total_tokens}")
    print(f"  Avg tokens per doc : {avg_tokens_per_doc:.2f}")
    print(f"  Time               : {fmt_secs(t_tok)}")
    if t_tok > 0:
        print(f"  Tokens/sec         : {total_tokens / t_tok:.2f}")
        print(f"  Docs/sec           : {n_docs / t_tok:.2f}")
    print()

    # ----------------------------------------------------------------------
    # STEP 3: Term Frequency (TF) per document
    # ----------------------------------------------------------------------
    t_tf_start = time.perf_counter()

    # (doc_id, Counter(term -> count))
    rdd_tf = rdd_tokens.map(lambda x: (x[0], Counter(x[1]))).cache()

    # Force computation
    tf_docs = rdd_tf.count()

    t_tf_end = time.perf_counter()
    t_tf = t_tf_end - t_tf_start

    print("STEP 3: Term Frequency (TF)")
    print(f"  Docs with TF       : {tf_docs}")
    print(f"  Time               : {fmt_secs(t_tf)}")
    if t_tf > 0:
        print(f"  Docs/sec           : {n_docs / t_tf:.2f}")
    print()

    # ----------------------------------------------------------------------
    # STEP 4: IDF computation
    #         idf(term) = log((N + 1) / (df + 1)) + 1  (smoothed)
    # ----------------------------------------------------------------------
    t_idf_start = time.perf_counter()

    N = n_docs

    # For each doc, we only need unique terms for document frequency
    # (term, 1) for each term that appears at least once in a doc
    term_doc_counts = (
        rdd_tokens
        .flatMap(lambda x: [(term, 1) for term in set(x[1])])
        .reduceByKey(lambda a, b: a + b)
    )

    # (term, idf)
    idf_rdd = term_doc_counts.mapValues(
        lambda df: math.log((N + 1.0) / (df + 1.0)) + 1.0
    )

    # Collect IDF to driver and broadcast
    idf_dict = idf_rdd.collectAsMap()
    vocab_size = len(idf_dict)

    idf_bc = sc.broadcast(idf_dict)

    t_idf_end = time.perf_counter()
    t_idf_fit = t_idf_end - t_idf_start

    print("STEP 4: IDF computation")
    print(f"  Vocabulary size     : {vocab_size}")
    print(f"  Time                : {fmt_secs(t_idf_fit)}")
    if t_idf_fit > 0:
        print(f"  Terms/sec (idf)     : {vocab_size / t_idf_fit:.2f}")
    print()

    # ----------------------------------------------------------------------
    # STEP 5: TF–IDF per document
    # ----------------------------------------------------------------------
    t_tfidf_start = time.perf_counter()

    def tfidf_for_doc(doc_id, tf_counter, idf_map):
        # Return (doc_id, {term: tfidf})
        tfidf = {
            term: tf_val * idf_map.get(term, 0.0)
            for term, tf_val in tf_counter.items()
        }
        return (doc_id, tfidf)

    idf_map = idf_bc.value

    rdd_tfidf = (
        rdd_tf
        .map(lambda x: tfidf_for_doc(x[0], x[1], idf_map))
        .cache()
    )

    tfidf_docs = rdd_tfidf.count()

    t_tfidf_end = time.perf_counter()
    t_tfidf = t_tfidf_end - t_tfidf_start

    print("STEP 5: TF–IDF computation")
    print(f"  Docs with TF–IDF    : {tfidf_docs}")
    print(f"  Time                : {fmt_secs(t_tfidf)}")
    if t_tfidf > 0:
        print(f"  Docs/sec            : {n_docs / t_tfidf:.2f}")
    print()

    # ----------------------------------------------------------------------
    # OVERALL SUMMARY
    # ----------------------------------------------------------------------
    t_total = time.perf_counter() - t0

    print("=== SUMMARY ===")
    print(f"Total runtime          : {fmt_secs(t_total)}")
    print(f"  Read/cache           : {fmt_secs(t_read)}")
    print(f"  Tokenization         : {fmt_secs(t_tok)}")
    print(f"  TF                   : {fmt_secs(t_tf)}")
    print(f"  IDF                  : {fmt_secs(t_idf_fit)}")
    print(f"  TF–IDF               : {fmt_secs(t_tfidf)}")

    if t_total > 0:
        print(f"\nEnd-to-end throughput  : {total_mb / t_total:.2f} MB/s")
        print(f"End-to-end docs/sec    : {n_docs / t_total:.2f}")
        print(f"Avg latency per doc    : {t_total / n_docs:.6f} s/doc")

    # Optional: inspect some cluster config
    conf = sc.getConf()
    exec_instances = conf.get("spark.executor.instances", "unknown")
    exec_cores = conf.get("spark.executor.cores", "unknown")
    exec_mem = conf.get("spark.executor.memory", "unknown")

    print("\n=== Cluster (Spark) config snapshot ===")
    print(f"spark.executor.instances : {exec_instances}")
    print(f"spark.executor.cores     : {exec_cores}")
    print(f"spark.executor.memory    : {exec_mem}")

    spark.stop()


if __name__ == "__main__":
    main()
