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
  - Parallelized transaction witness validation via the [C++ Plutus Machine](lib/dt/plutus).
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
  - **16 GB** for **8–12 core CPUs**.
  - **32 GB** for **16–24 core CPUs** (*higher core counts require more RAM*).
  - The more cores a CPU has, the more RAM is needed.
- **Storage:** A fast SSD with at least 200 GB of free space, allocated as follows:
  - **70 GB** – Compressed blockchain data & search indices.
  - **30 GB** – Temporary storage for indexing.
  - **100 GB** – Temporary storage for full transaction witness validation.
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
git clone --depth=1 https://github.com/r2rationality/turbocardano.git dt
cd dt
```

Build the test Docker container:
```
docker build -t dt -f Dockerfile.test .
```

Start the test container, with `<turbo-dir>` being the host's directory to store the blockchain data:
```
docker run -it --rm -v <turbo-dir>:/data dt
```

All the following commands are to be run within the container started by the previous command.

Download, validate, and prepare for querying a copy of the Cardano blockchain from Cardano bootstrap nodes:
```
./dt sync /data/cardano --max-slot=150877935
```

> **Notes:**  
> - The current bootstrap nodes **do not yet support** [CIP 0150 – Block Data Compression](https://github.com/cardano-foundation/CIPs/tree/master/CIP-0150).  
>   As a result, downloads will be **several times slower** than they will be once compression is enabled in the future.
> - Cardano network peers may sometimes terminate connections unexpectedly.
>   If your download or synchronization is interrupted, simply re-run the command.
>   You may need to repeat this process several times to complete the operation.

Show information about the local chain's tip:
```
./dt tip /data/cardano
```

Start the experimental Node server with block data compression enabled, listening on 127.0.0.1:3001:
```
DT_LOG=/data/server.log ./dt node-api /data/cardano &> /dev/null &
```

Re-download all data from the local server started by the previous command (with compression enabled):
```
./dt sync /data/cardano2 --peer-host=127.0.0.1
```

Compare the tip:
```
./dt tip /data/cardano2
```

Reconstruct the latest balance and transaction history of a stake key:
```
./dt stake-history /data/cardano stake1uxw70wgydj63u4faymujuunnu9w2976pfeh89lnqcw03pksulgcrg
```

Reconstruct the latest balance and transaction history of a payment key:
```
./dt pay-history /data/cardano addr1q86j2ywajjgswgg6a6j6rvf0kzhhrqlma7ucx0f2w0v7stuau7usgm94re2n6fhe9ee88c2u5ta5znnwwtlxpsulzrdqv6rmuj
```

Show information about a transaction:
```
./dt tx-info /data/cardano 357D47E9916B7FE949265F23120AEED873B35B97FB76B9410C323DDAB5B96D1A
```

Evaluate a Plutus script and show its result and costs:
```
./dt plutus-eval ../data/plutus/conformance/example/factorial/factorial.uplc
```

(Optional) Revalidate consensus since genesis for benchmark purposes:
```
./dt revalidate /data/cardano
```

(Optional) Revalidate transaction witnesses since genesis for benchmark purposes:
```
./dt txwit-all /data/cardano
```

(Optional) Synchronize the local chain from a Cardano Network node (slower since Cardano network protocol lacks compression):
```
./dt sync-p2p /data/cardano
```

# Spread the word
Many in the Cardano community, including some Cardano core developers, don't believe that it's possible to make Cardano Node noticeably faster.
This leads to a situation in which the development is not focused on its performance. If you're persuaded by the evidence presented here, share it on social media with those around you. Changing the beliefs of people can be harder than building top-notch technology. So, every single tweet and Facebook post makes a difference. Thank you!

# Compilation
The **recommended build method** uses Docker and is the only approach that is regularly tested.

Building in other environments and with different compilers is **possible** and is **occasionally tested**, though not officially supported.

If you choose to compile the software outside of Docker, refer to the following notes for guidance.

## Necessary software packages
- [CMake](https://cmake.org/) >= 3.28, a build system
- [boost](https://www.boost.org/) >= 1.83 && <= 1.85, a collection of C++ libraries
- [fmt](https://github.com/fmtlib/fmt) >= 8.1.1, a string formatting library
- [libsodium](https://github.com/jedisct1/libsodium) >= 1.0.18, a cryptographic library
- [secp256k1](https://github.com/bitcoin-core/secp256k1) >= 0.2.0, a cryptographic library
- [spdlog](https://github.com/gabime/spdlog) >= 1.9.2, a logging library
- [zstd](https://github.com/facebook/zstd) >= 1.4.8, a compression library

Additionally on Windows:
- [mimalloc](https://github.com/microsoft/mimalloc) >= 2.1.9, a memory allocator that works well with multi-threaded workloads

## Tested environments and compilers
- Ubuntu Linux 24.04 with GCC 13.2
- Ubuntu Linux 24.04 with Clang 18
- Windows 11 with Visual C++ 19.39.33520.0 that comes with Visual Studio 2022 Community Edition

## Build the command line version
Verify the presence of the necessary libraries and generate build files in `cmake-build-release` directory for a release build:
```
cmake -B cmake-build-release
```

Build `dt` binary using all available CPU cores (will be available in `cmake-build-release` directory):
```
cmake --build cmake-build-release -j -t dt
```
