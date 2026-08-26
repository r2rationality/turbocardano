# Contents
- [About](#about)
- [Features](#features)
- [Requirements](#requirements)
- [Test it yourself](#test-it-yourself)
- [Quality](#quality)
- [Roadmap](#roadmap)
- [Compilation](#compilation)

# About
Turbo is a high-performance C++ implementation of key Cardano Node components,
originally developed to optimize the batch synchronization of full personal wallets (e.g., Daedalus).
It focuses on efficient data consumption rather than data production, allowing for additional optimizations.
Now, the project is evolving to support a broader range of use cases.

The technical approach is based on two key ideas:
- Reducing network bandwidth usage through compression techniques.
- Maximizing parallel computation in blockchain data processing to improve synchronization and validation speeds.

These ideas are further explained in the following research reports:
- [On the Security of Wallet Nodes in the Cardano Blockchain](./doc/2024-sierkov-on-wallet-security.pdf) - Security approach.
- [Parallelized Ouroboros Praos](./doc/2024-sierkov-parallelized-ouroboros-praos.pdf) – Parallelized data validation.
- [Highly Parallel Reconstruction of Wallet History in the Cardano Blockchain](./doc/2023_Sierkov_WalletHistoryReconstruction.pdf) – Wallet history reconstruction.
- [Scalability of Bulk Synchronization in the Cardano Blockchain](./doc/2023_Sierkov_CardanoBulkSynchronization.pdf) – Infrastructure scaling and compression-based network optimization.

# Status

> ⚠️ **Warning**
>
> The project is preparing for its next release, which will reorganize the networking infrastructure in accordance with [CIP 0150](https://github.com/cardano-foundation/CIPs/pull/993).
>
> As part of this transition, the **Turbo proxies** (which previously supported historical network-based examples) have been disabled. This means that:
> - Some older examples and pre-built binaries may not function correctly.
> - You may encounter errors or unexpected behavior with features relying on those proxies.
> 
> If you experience issues or have questions, please [open a GitHub issue](#).

With the core functionally mostly complete now, the focus has shifted to testing and integration. Key ongoing efforts include:
- **Integration:** [Cardano Improvement Proposal 0150 - Block Data Compression](https://github.com/cardano-foundation/CIPs/tree/master/CIP-0150) – Ensuring seamless integration of block data compression with existing infrastructure.
- **Integration:** [Integration of an Alternative Node Implementation into Cardano Wallet](https://github.com/cardano-foundation/cardano-wallet/discussions/5072) – Enabling fast synchronization and wallet-history reconstruction for client software, such as Daedalus.
- **Verification:** [Implementation-Independent Ledger Conformance Test Suite](https://github.com/IntersectMBO/cardano-ledger/issues/4892) – Validating correctness and conformance with the ledger specification.
- **Performance:** [Cross-Implementation Benchmarking Dataset for Plutus Performance](https://github.com/IntersectMBO/plutus/issues/6626) – Measuring and optimizing Plutus script execution across implementations.
- **Safety:** [Memory Safety Verification for critical C++ Code](./doc/memory-safety.md) – Analyzing and mitigating potential memory safety issues.

# Features
- **Support for all Cardano eras:** Byron, Shelley, Allegra, Mary, Alonzo, Babbage, and Conway.
- **Efficient blockchain synchronization:**
  - Incremental synchronization using the **Cardano Network protocol** without compression.
  - Incremental synchronization using the **Cardano Network protocol** with [Cardano Improvement Proposal 0150 - Block Data Compression](https://github.com/cardano-foundation/CIPs/tree/master/CIP-0150).
- **Parallelized validation mechanisms:**
  - Consensus validation according to Ouroboros Praos/Genesis rules.
  - Parallelized transaction witness validation via the [C++ Plutus Machine](lib/turbo/plutus).
  - Consensus-based witness validation ("Turbo validation"), as detailed in [On the Security of Wallet Nodes in the Cardano Blockchain](./doc/2024-sierkov-on-wallet-security.pdf)
- **Optimized blockchain data storage:**
  - Compressed local storage of blockchain data (**~4.5x reduction in size**).
- **Advanced transaction and balance querying mechanisms**
  - Interactive balance and transaction history reconstruction for both stake and payment addresses.
  - Searchable transaction data with fast query capabilities.

## Features deprecated due to lack of funding
- **Standalone Desktop UI**
  - A fully local blockchain explorer with real-time transaction tracking and historical analysis.
  - Pre-built Windows and Mac binaries.

- **Turbo Protocol Synchronization**
  - Incremental synchronization of compressed blockchain data over HTTP protocol (improving bandwidth efficiency).
  - Incremental synchronization from a local Cardano Node.

# Requirements
- **CPU:** A modern processor with at least 8 physical cores (minimum equivalent: Orange Pi 5 Plus or better). The software will not run on weaker CPUs.
- **RAM:**
  - **16–32 GB** depending on the CPU core count (*mores cores require more RAM*).
  - The more cores a CPU has, the more RAM is needed.
- **Storage:** A fast (PCIe gen 4) SSD with at least 120 GB of free space, allocated as follows:
  - **50 GB** – Compressed blockchain data.
  - **50 GB** – Indices and temporary storage for indexing.
  - **20 GB** – Ledger state snapshots.
- **Internet:** A stable **250 Mbps or faster** connection is required for efficient incremental blockchain synchronization.

# Test it yourself

## Command line interface

### Prerequisites
To test the command line interface, you need the following software packages installed:
- [Git](https://git-scm.com/) to get a copy of this repository.
- [Docker](https://www.docker.com/products/docker-desktop/) to launch the software in an isolated environment.

### Commands

Clone this repository and make it your working directory:
```
git clone --depth=1 --recursive https://github.com/r2rationality/turbocardano.git tada
cd tada
```

Build the test Docker container:
```
docker build -t tada -f Dockerfile.test .
```

> **Notes:**
> - In all the following commands:
>   - `turbo` is the id of a docker volume to store blockchain data.
>   - `127.0.0.1` the IP address of a local server that supports [CIP 0150 – Block Data Compression](https://github.com/cardano-foundation/CIPs/tree/master/CIP-0150).
> - The current bootstrap nodes **do not yet support CIP-0150**.  
>   As a result, sync from a bootstrap server will be **four-to-five times slower** than it can be once compression is enabled in the future.
> - An example of how to launch the experimental service supporting **CIP-0150** is provided below. 

Download, validate, and prepare for querying a copy of the Cardano blockchain from Cardano bootstrap nodes:
```
docker run -it --rm -v turbo:/data tada sync /data
```

Show information about the local chain's tip:
```
docker run -it --rm -v <turbo-dir>:/data tada tip /data
```

(Optional) Start the experimental Node server with block data compression enabled, listening on 0.0.0.0:3001:
```
docker run -it --rm -p 3001:3001 -v turbo:/data tada node-api /data --peer-ip=0.0.0.0
```

(Optional) Re-download blockchain data from the experimental server started in the previous command (with compression enabled), where:
- ```127.0.0.1``` to be replaced with the public IP address of the server started in the previous command;
- ```turbo2``` to be replaced with the id of another docker volume to store the re-downloaded blockchain data for comparison.

```
docker run -it --rm -v turbo2:/data sync /data --peer-host=127.0.0.1
```

Reconstruct the latest balance and transaction history of a stake key:
```
docker run -it --rm -v turbo:/data stake-history /data stake1uxw70wgydj63u4faymujuunnu9w2976pfeh89lnqcw03pksulgcrg
```

Reconstruct the latest balance and transaction history of a payment key:
```
docker run -it --rm -v turbo:/data pay-history /data addr1q86j2ywajjgswgg6a6j6rvf0kzhhrqlma7ucx0f2w0v7stuau7usgm94re2n6fhe9ee88c2u5ta5znnwwtlxpsulzrdqv6rmuj
```

Show information about a transaction:
```
docker run -it --rm -v turbo:/data tx-info /data 357D47E9916B7FE949265F23120AEED873B35B97FB76B9410C323DDAB5B96D1A
```

Evaluate a Plutus script and show its result and costs:
```
docker run -it --rm -v turbo:/data plutus-eval ../data/plutus/conformance/example/factorial/factorial.uplc
```

(Optional) Revalidate consensus since genesis for benchmark purposes:
```
docker run -it --rm -v turbo:/data revalidate /data/cardano
```

(Optional) Revalidate transaction witnesses since genesis for benchmark purposes:
```
docker run -it --rm -v turbo:/data txwit-all /data/cardano
```

# Spread the word
Many in the Cardano community, including some Cardano core developers, don't believe that it's possible to make Cardano Node noticeably faster.
This leads to a situation in which the development is not focused on its performance. If you're persuaded by the evidence presented here, share it on social media with those around you. Changing the beliefs of people can be harder than building top-notch technology. So, every single tweet and Facebook post makes a difference. Thank you!

# Compilation
The **recommended build method** uses Docker and is the only approach that is regularly tested.

Building in other environments and with different compilers is **possible** and is **occasionally tested**, though not officially supported.

If you choose to compile the software outside of Docker, refer to the following notes for guidance.

## Necessary software packages
- [CMake](https://cmake.org/), a build system
- [boost](https://www.boost.org/) >= 1.83, a collection of C++ libraries
- [fmt](https://github.com/fmtlib/fmt), a string formatting library
- [libsodium](https://github.com/jedisct1/libsodium) >= 1.0.18, a cryptographic library
- [secp256k1](https://github.com/bitcoin-core/secp256k1) >= 0.2.0, a cryptographic library
- [spdlog](https://github.com/gabime/spdlog) >= 1.9.2, a logging library
- [zstd](https://github.com/facebook/zstd) >= 1.4.8, a compression library

Additionally on Windows:
- [mimalloc](https://github.com/microsoft/mimalloc) >= 3, a memory allocator that works well with multi-threaded workloads

## Tested environments and compilers
- Ubuntu Linux 26.04 with GCC 15.2
- Ubuntu Linux 26.04 with Clang 21
- Windows 11 with Visual C++ that comes with Visual Studio 2026 Community Edition

## Build the command line version
Verify the presence of the necessary libraries and generate build files in `build-release` directory for a release build:
```
cmake -B build-release
```

Build `tada` binary using all available CPU cores (will be available in `build-release` directory):
```
cmake --build build-release -j -t tada
```
