# parallelproject

## How to Use `gut_txt.cpp`

1. **Install Dependencies**
    Make sure you have the following installed:

   - GCC or Clang (C++17 or newer)
   - `libcurl` and `pkg-config`
   - `nlohmann-json` header-only library

   Example (Ubuntu/Debian):

   ```
   sudo apt-get install g++ libcurl4-openssl-dev pkg-config nlohmann-json3-dev
   ```

2. **Compile the Program**

   ```
   g++ -std=gnu++17 -O2 gut_txt.cpp \
     -I/usr/local/opt/nlohmann-json/include \    # adjust include path if needed
     $(pkg-config --cflags --libs libcurl) \
     -o gut_txt
   ```

3. **Run the Program**
    Syntax:

   ```
   ./gut_txt [TARGET_MB] [OUTPUT_DIR]
   ```

   - `TARGET_MB` (optional): total size (in MB) of text to download. Default: `10`
   - `OUTPUT_DIR` (optional): directory to save the text files. Default: `data`

   The program will automatically fetch English plain-text books from the Gutendex API, remove Project Gutenberg headers/footers, and save each book as a `.txt` file. It stops when the total downloaded size reaches your target.



### Future Optimization Ideas

- **Parallel Downloads:** Use a thread pool (each thread with its own `CURL*` handle) or the `libcurl multi` interface to download multiple books simultaneously, improving speed.
- **Atomic Counters and Graceful Stop:** Use `std::atomic` for `total_bytes` and `file_count` to safely handle concurrency and stop precisely at the target size.
- **Download data file to the databased we need:** We are using HDFS, aws S3 and google bucket. Add more properties to support different storage system method.



## Submit on Dataproc Serverless

### 1. Prepare Files

Upload files to a GCS bucket:

- Code: `gs://YOUR_BUCKET/code/tfidf_job.py`
- Input: `gs://YOUR_BUCKET/data/`
- Output: `gs://YOUR_BUCKET/outputs/tfidf/`

------

### 2. Create a Batch Job

1. Go to **Console → Dataproc → Batches → Create batch**.

2. Choose region and set **Batch type** to **PySpark**.

3. Main Python file:

   ```
   gs://YOUR_BUCKET/code/tfidf_job.py
   ```

4. Arguments (in order):

   - `gs://YOUR_BUCKET/data/`
   - `gs://YOUR_BUCKET/outputs/tfidf/`

------

### 3. Adjust Spark Properties

Tune performance by changing:

- `spark.executor.instances`
- `spark.executor.cores`
- `spark.executor.memory`
- `spark.sql.shuffle.partitions`

------

### 4. Run and Check Results

Submit the job and monitor it under **Dataproc → Batches**.
 Results will be saved to:

```
gs://YOUR_BUCKET/outputs/tfidf/
```