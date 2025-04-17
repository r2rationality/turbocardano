# Contents
- [About](#about)
- [Features](#features)
- [Requirements](#requirements)
- [Test it yourself](#test-it-yourself)
- [Quality](#quality)
- [Roadmap](#roadmap)
- [Compilation](#compilation)

# About
Turbo is a high-performance C++ implementation of key Cardano Node components, originally designed to optimize full personal wallets (e.g., Daedalus). It focuses on efficient data consumption rather than data production, allowing for additional optimizations. Now, the project is evolving to support a broader range of use cases.

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
> The project is currently preparing for the next release, which will reorganize the networking infrastructure based on [CIP 0150](https://github.com/cardano-foundation/CIPs/pull/993).
> 
> As a result, the Turbo proxies that supported the existing network-based examples have been disabled.
> Because of this, the examples listed below may not function as intended.
> This will be addressed in the next release.
> 
> If you have any questions or encounter issues, please open a GitHub issue.

With the core functionally mostly complete now, the focus has shifted to testing and integration. Key ongoing efforts include:
- **Integration:** [Cardano Improvement Proposal 0150 - Block Data Compression](https://github.com/cardano-foundation/CIPs/tree/master/CIP-0150) – Ensuring seamless integration of block data compression with existing infrastructure.
- **Integration:** [Integration of an Alternative Node Implementation into Cardano Wallet](https://github.com/cardano-foundation/cardano-wallet/discussions/5072) – Enabling fast synchronization and wallet-history reconstruction for client software, such as Daedalus.
- **Verification:** [Implementation-Independent Ledger Conformance Test Suite](https://github.com/IntersectMBO/cardano-ledger/issues/4892) – Validating correctness and conformance with the ledger specification.
- **Performance:** [Cross-Implementation Benchmarking Dataset for Plutus Performance](https://github.com/IntersectMBO/plutus/issues/6626) – Measuring and optimizing Plutus script execution across implementations.
- **Safety:** [Memory Safety Verification for critical C++ Code](./doc/memory-safety.md) – Analyzing and mitigating potential memory safety issues.

# Features
- **Full support for all Cardano eras:** Byron, Shelley, Allegra, Mary, Alonzo, Babbage, and Conway.
- **Efficient blockchain synchronization:**
  - Incremental synchronization of **compressed blockchain data** over the Internet using the Turbo protocol (improves bandwidth efficiency).
  - Incremental synchronization using the **standard Cardano Network protocol** (without compression).
  - Incremental synchronization from a **local Cardano Node** (supports both immutable and volatile files).
- **Parallelized validation mechanisms:**
  - Consensus validation according to Ouroboros Praos/Genesis rules.
  - Parallelized transaction witness validation via the [C++ Plutus Machine](lib/dt/plutus).
  - Consensus-based witness validation ("Turbo validation"), as detailed in [On the Security of Wallet Nodes in the Cardano Blockchain](./doc/2024-sierkov-on-wallet-security.pdf)
- **Optimized blockchain data storage:**
  - Compressed local storage of blockchain data (**~4.5x reduction in size**).
- **Advanced transaction and balance queying mechanisms**
  - Interactive balance and transaction history reconstruction for both stake and payment addresses.
  - Searchable transaction data with fast query capabilities.
- **Standalone Desktop UI:**
  - A **fully local** blockchain explorer with real-time transaction tracking and historical analysis.

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

One can test the software using two methods:
- Building the software from the source code using Docker
- Downloading and installing a prebuilt binary

Each is described in more detail below.

## Command line interface

### Prerequisites
To test the command line interface, you need the following software packages installed:
- [Git](https://git-scm.com/) to get a copy of this repository.
- [Docker](https://www.docker.com/products/docker-desktop/) to launch the software in an isolated environment.

### Commands

Clone this repository and make it your working directory:
```
git clone --depth=1 https://github.com/sierkov/daedalus-turbo.git dt
cd dt
```

Build the test Docker container:
```
docker build -t dt -f Dockerfile.test .
```

Start the test container, with `<cardano-dir>` being the host's directory to store the blockchain data:
```
docker run -it --rm -v <cardano-dir>:/data/cardano dt
```

All the following commands are to be run within the container started by the previous command.

Download, validate, and prepare for querying a copy of the Cardano blockchain from a network of compressing proxies with entry points listed in [etc/mainnet/turbo.json](etc/mainnet/turbo.json):
```
./dt sync-turbo /data/cardano
```

Show information about the local chain:
- the physical tip (most recent valid block);
- the core tip (confirmed by the majority of active stake);
- the immutable tip (2160 blocks behind).
```
./dt tip /data/cardano
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
## Pre-built binaries for Windows and Mac (ARM64)

The latest builds of the DT Explorer application can be found in the Assets section of [the latest GitHub release](https://github.com/sierkov/daedalus-turbo/releases/latest) page.
It shows the new synchronization and history-reconstruction algorithms in a safe and easy-to-test way by working without private keys and directly reconstructing history of any payment and stake address.

### How to install on Windows
- Download and launch the installer from the Assets section of [the latest release](https://github.com/sierkov/daedalus-turbo/releases/latest) page.
- Choose locations for the program files and blockchain data (each will be asked individually). For optimal performance, it's important to select your fastest SSD drive if you have multiple storage devices. 

Windows builds have been tested with Windows 11 (earlier versions may work but have yet to be be tested).

### How to install on Mac
- Download the Mac image from the Assets section of [the latest release](https://github.com/sierkov/daedalus-turbo/releases/latest) page.
- Open the image. This is a development (unsigned) image, so Mac OS will ask you if you trust the developer: [See Apple's explanation and instructions](https://support.apple.com/en-is/guide/mac-help/mh40616/mac).
- Copy dt-explorer app to your Applications folder.
- Both program and blockchain data will be stored in that folder, so when deleted all used space will be recovered.
- Launch the app from the Applications folder. If Mac OS says that the app is damaged, open a terminal and run ```sudo xattr -rc /Applications/dt-explorer.app```.

Mac builds have been tested with Mac OS Sonoma (earlier versions may work but have yet to be be tested).

### How to use
- Synchronization (full or partial) always happens at the app's launch; to catch up, simply restart the app. If you restart before the synchronization is finished, the app will reuse already downloaded data but may reprocess and revalidate some of them.
- History reconstruction happens through a simple search for a transaction, stake, or payment address, either entered explicitly or when clicked as part of blockchain exploration. The easiest starting point for most users would be to search for their own stake address (e.g.: stake1uxw70wgydj63u4faymujuunnu9w2976pfeh89lnqcw03pksulgcrg), as its history will be the most representative of their wallet's history.
- When searching for Cardano addresses starting with "addr1" prefix, the app may ask you if want to explore the payment or stake history. The reason for that is that many cardano addresses contain two keys. If in doubt, select stake history as it will normally discover more transactions. This is necessary so that the app can work without private keys. However, when integrated into Daedalus, the same aglorithms can reconstruct the full wallet history by finding all payment and stake keys generated from a wallet private key.
- Once synchronized, users can turn off their Internet connection and test history reconstruction with new transaction or stake addresses to prove that the app reconstructs all histories interactively and uses only the downloaded blockchain data.

# Spread the word
Many in the Cardano community, including some developers of Daedalus, don't believe that it's possible to make it noticeably faster. This leads to a situation in which the development is not focused on its performance. If you're persuaded by the evidence presented here, share it on social media with those around you. Changing the beliefs of people can be harder than building top-notch technology. So, every single tweet and Facebook post makes a difference. Thank you!

# Supporting network infrastructure
The high-speed delivery of blockchain data depends on a network of compressing proxies (Cardano nodes that can share their local chain with other nodes in a compressed format and which are needed until the support for compressed data transfers becomes part of the regular Cardano network protocol). The current network configuration is sufficient to support up to 100'000 high-speed (with a download speed of 200+ Mbps) full blockchain synchronizations per year and orders of magnitude more incremental ones. The plan how to scale the network capacity
to support a billion clients is presented in the [Scalability of Bulk Synchronization in the Cardano Blockchain](./doc/2023_Sierkov_CardanoBulkSynchronization.pdf) paper.

# Quality
The accuracy of the ledger state reconstruction has been tested by recreating the ledger state from raw blockchain data at the end (the presented method batches updates) of each post-Shelley epoch up to the mainnet's epoch 426, exporting it into the Cardano Node format, and comparing it with the snapshot produced by Cardano Node version 10.1.4. The source code of the tools used for the comparison is located in [lib/dt/cli-test/test-ledger-export.cpp](lib/dt/cli-test/test-ledger-export.cpp).

The indexing and history-reconstruction code has been tested using a sample of ten thousand randomly-selected stake keys. For 100% of those, the reconstructed ADA balance (excluding rewards) precisely matched the stake recorded in the ledger snapshot produced by Cardano Node. The testing was performed with slot number 106012751 at the tip of the blockchain. The code of the test is located in the [lib/dt/cli-test/test-stake-balances.cpp](lib/dt/cli-test/test-stake-balances.cpp) file.

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
- Mac OS Sonoma 14.2.1 with Clang 17.0.6 installed with ```brew install llvm@17```
- Windows 11 with Visual C++ 19.39.33520.0 that comes with Visual Studio 2022 Community Edition
- Windows 11 with GCC 13.2 that comes with MinGW64

## Build the command line version
Verify the presence of the necessary libraries and generate build files in `cmake-build-release` directory for a release build:
```
cmake -B cmake-build-release
```

Build `dt` binary using all available CPU cores (will be available in `cmake-build-release` directory):
```
cmake --build cmake-build-release -j -t dt
```

## Build the Windows installer
1. Download and install [Microsoft Visual Studio Community 2022](https://visualstudio.microsoft.com/vs/community/)
2. In the Visual Studio installer, enable "Desktop development with C++" workload.
3. Download and install [NSIS installer compiler 3.10](https://nsis.sourceforge.io/Download).
4. Download and install [Node.js 22](https://nsis.sourceforge.io/Download)
5. Open a CMD terminal and navigate to the DT source code directory.
6. Set up the necessary Visual Studio environment variables for a command line build:
   ```
   "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
   ```
7. Use vcpkg to install the required packages specified in ```vcpkg.json```:
   ```
   vcpkg install
   ```
8. Configure the build with CMake:
   ```
   cmake -B build-win-release -G Ninja -DCMAKE_BUILD_TYPE=Release --toolchain="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
   ```
9. Build the DT binaries:
   ```
   cmake --build build-win-release --config Release -j -t dt -t ui
   ```
10. Switch to the build directory:
    ```
    cd build-win-release
    ```
11. Build the Windows installer:
    ```
    cpack --config CPackConfig.cmake
    ```
12. The installer will be stored in build-win-release directory.

## Build the Mac Arm64 disk image
1. Open a terminal window and navigate to the directory with DT source code.
2. Install the necessary packages with brew:
   ```
   brew install cmake ninja fmt libsodium llvm@17 secp256k1 spdlog zstd
   brew install boost@1.85
   brew link boost@1.85
   brew install node@22
   brew link node@22
   brewk install botan@2
   brew link botan@2
   ```
3. Prepare cmake build files in cmake-build-release directory (the name is used in build scripts so stay be the same):
   ```
   cmake -B cmake-build-release -G Ninja
   ```
4. Build the Mac binaries
   ```
   cmake --build cmake-build-release -j -t dt
   ```
5. Switch to the UI directory:
   ```
   cd ui
   ```
6. Install the necessary NPM packages:
   ```
   npm i
   ```
7. Build the Mac disk image:
   ```
   npm run pkg-mac
   ```
8. The resulting disk image will be stored in the ui directory.