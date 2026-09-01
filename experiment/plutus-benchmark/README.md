# Cross-Implementation Benchmarking Dataset for Plutus Performance

This dataset aims to fulfill the following requirements:
1. **Predictive Power**: Benchmark results should allow to predict the time required for a given implementation to validate all Plutus witnesses on the Cardano mainnet.
2. **Efficient Runtime**: The benchmark should complete quickly to enable rapid experimentation and performance evaluation.
3. **Parallelization Awareness**: It must assess both single-threaded and multi-threaded performance to identify implementation approaches that influence the parallel efficiency of script witness validation.
4. **Sufficient Sample Size**: The dataset should contain enough samples to allow computing reasonable sub-splits for further analysis, such as by Plutus version or by Cardano era.
5. **Implementation independence**: The dataset can be used with any Plutus implementation that supports the evaluation of CBOR-encoded Plutus scripts in the Flat format, the format used for storing Plutus scripts on the Cardano blockchain.

The procedure for creating this dataset is as follows (full reproduction instructions are provided below):
1. **Transaction Sampling**: Randomly without replacement select a sample of 256,000 mainnet transactions with Plutus script witnesses. This sample size is chosen as a balance between speed, sufficient data for analysis, and compatibility with high-end server hardware with up to 256 execution threads. The randomness of the sample allows for generalizable predictions of the validation time of all Plutus script witnesses.
2. **Script Preparation**: For each script witness in the selected transactions, prepare the required arguments and script context data. Save each as a Plutus script in the CBOR-encoded Flat format, with all arguments pre-applied.
3. **File Organization**: For easier debugging, organize all extracted scripts using the following filename pattern: ```<mainnet-epoch>/<transaction-id>-<redeemer-idx>-<script-hash>-<plutus-version>.flat.```

## Data
The latest dataset is available as a two-part archive to keep each downloadable file under 100 MB for problem-free downloads:
 - [dataset.tar.0000.xz](https://raw.githubusercontent.com/r2rationality/turbocardano/main/experiment/plutus-benchmark/20260826/dataset.tar.0000.xz)
 - [dataset.tar.0001.xz](https://raw.githubusercontent.com/r2rationality/turbocardano/main/experiment/plutus-benchmark/20260826/dataset.tar.0001.xz)

 It can be unpacked using the following Linux command, where ```<target-dir>``` is the destination directory after decompression:
 ```(bash)
cat dataset.tar.*.xz | tar -xJvf -
 ```

Previous versions
| Date      | Max Epoch | Files | 
|-----------|-----------|-------|
| 20241119  | 521      | [dataset.tar.001.xz](https://raw.githubusercontent.com/r2rationality/turbocardano/main/experiment/plutus-benchmark//20241119/dataset.tar.001.xz) [dataset.tar.002.xz](https://raw.githubusercontent.com/r2rationality/turbocardano/main/experiment/plutus-benchmark/20241119/dataset.tar.002.xz) |

## Statistics describing the dataset
| Attribute | Value   |
|-----------|---------|
| Script evaluation contexts | 525,231 |
| Unique mainnet scripts | 5,706 |
| Transactions | 256,000 |
| Compressed size | 180 MiB |
| Uncompressed size | 5,228 MiB |

## Statistics describing the Cardano mainnet at the generation time
| Attribute | Value   |
|-----------|---------|
| Mainnet epoch | 650     |
| Total blocks | 13,843,764 |
| Total transactions | 123,239,164 |
| Total plutus redeemers | 60,061,161 |
| Total native-script witnesses | 17,526,717 |
| Total vkey witnesses | 218,196,796 |
| Unique plutus scripts | 150,641 |

# Latest benchmarks of the C++ Plutus implementation
The raw outputs of the benchmarking script are located in [20260826/raw-results.tar.xz](./20260826/raw-results.tar.xz).

## Plutus witness validation throughput

![Plutus witness validation rate](./20260826/chart-rate.png)

## Parallel efficiency of Plutus witness validation

![Plutus witness validation rate](./20260826/chart-efficiency.png)

## Predicted time to validate all mainnet Plutus witnesses

![Predicted time to validate all mainnet Plutus witnesses](./20260826/chart-predicted-time.png)

## Steps to reproduce

Rent a bare metal server with AMD EPYC 9254 CPUs with 24 physical cores at [Vultr](https://www.vultr.com/). Specify Ubuntu Linux 24.04 as the host operating system.

Install Docker:
```
apt update
apt install -y docker.io docker-buildx
```

Clone this repository and make it your working directory:
```
git clone --recursive https://github.com/r2rationality/turbocardano tada
cd tada
git checkout bb34b8ea35d92d5651e638d359f35c04bb02f170
```

Build the test Docker container:
```
docker build -t tada -f Dockerfile.test .
```

Start the test container and use ```turbo``` docker volume for data storage:
```
docker run -it --rm -v turbo:/data --entrypoint=/bin/bash tada
```

**All the following commands shall be executed in the container created by the previous command.**

Download and extract the benchmarking dataset:
```
sudo apt install -y wget xz-utils
wget https://raw.githubusercontent.com/r2rationality/turbocardano/main/experiment/plutus-benchmark/20260826/dataset.tar.0000.xz
wget https://raw.githubusercontent.com/r2rationality/turbocardano/main/experiment/plutus-benchmark/20260826/dataset.tar.0001.xz
cat dataset.tar.*.xz | tar -C /data -xJvf -
```

Run the benchmarking command:
```(bash)
for num_workers in 1 2 4 8 16 24; do
  ./bin/tada plutus-benchmark /data/plutus-bench $num_workers cpp
done
```
The resulting CSV files can be found in ```/data/plutus-bench``` directory.

The source code:
- The benchmarking command is located in [lib/turbo/cli/plutus-benchmark.cpp](../../lib/turbo/cli/plutus-benchmark.cpp)
- The Jupyter notebook for generating the charts is located in [prep-charts.py](./prep-charts.py). It has been converted into plain Python with Jupytext for the ease of version control.

# Dataset creation procedure

The below commands are provided for reference purposes. Normally, the benchmarking dataset is extracted from the provided archive.

```(bash)
./bin/tada sync /data/cardano --max-epoch=650
./bin/tada txwit-prep /data/cardano /data/scriptctx
./bin/tada plutus-extract-scripts /data/scriptctx experiment/plutus-benchmark/20260826/txs.txt /data/plutus-bench
```

The sampling of transactions can be reproduced using the following command:
```
./bin/tada plutus-sample-txs /data/cardano txs.txt --seed=123456 --sample=256000
```

Collect statistics about the total counts of transaction witnesses:
```(bash)
./bin/tada txwit-stat /data/cardano
```

Measure the actual time to validate all Plutus witnesses:
```(bash)
./bin/tada txwit-plutus /data/scriptctx --workers=24
```

The source code:
- The transaction sampling code is located in [lib/turbo/cli/plutus-sample-txs.cpp](../../lib/turbo/cli/plutus-sample-txs.cpp).
- The plutus script extraction code is located in [lib/turbo/cli/plutus-extract-scripts.cpp](../../lib/turbo/cli/plutus-extract-scripts.cpp).
