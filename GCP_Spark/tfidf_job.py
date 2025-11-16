# tfidf_job.py
import sys
from pyspark.sql import SparkSession
from pyspark.sql.functions import input_file_name, monotonically_increasing_id
from pyspark.ml import Pipeline
from pyspark.ml.feature import RegexTokenizer, StopWordsRemover, CountVectorizer, IDF

INPUT = sys.argv[1]   # gs://<bucket>/data/
OUTPUT = sys.argv[2]  if len(sys.argv) > 2 else None # gs://<bucket>/outputs/tfidf/

spark = (SparkSession.builder
         .appName("TFIDF-MultiNode")
         .getOrCreate())

# 1) Read text files (recursively through subdirectories; if everything is flat, you can drop the option)
df = (spark.read
      .option("recursiveFileLookup", "true")
      .text(INPUT).withColumnRenamed("value","text")
      .withColumn("source_file", input_file_name())
      .withColumn("doc_id", monotonically_increasing_id()))

# 2) Tokenization + stopwords removal (English)
tokenizer = RegexTokenizer(inputCol="text", outputCol="tokens",
                           pattern="\\W+", minTokenLength=2)
remover = StopWordsRemover(inputCol="tokens", outputCol="filtered")

# 3) TF (could be replaced by HashingTF), and 4) IDF
vectorizer = CountVectorizer(inputCol="filtered", outputCol="tf",
                             vocabSize=500_000, minDF=2)
idf = IDF(inputCol="tf", outputCol="tfidf")

pipeline = Pipeline(stages=[tokenizer, remover, vectorizer, idf])
model = pipeline.fit(df)

res = model.transform(df).select("doc_id", "source_file", "tfidf")

# 5) Write out (for small data: a few tens to ~100 partitions are often enough)
# (res.repartition(30)
#     .write.mode("overwrite").parquet(OUTPUT))

# triger action, no output
res.count()

spark.stop()
