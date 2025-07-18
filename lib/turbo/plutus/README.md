# C++ Plutus Machine

## Objectives
The intended use case is the evaluation of on-chain script witnesses during chain synchronization. The source code includes:
- an implementation of the Plutus machine;
- a UPLC (Untyped Plutus) parser;
- a decoder of the Flat format used for storing Plutus scripts on-chain;
- an implementation of Plutus builtins up to batch 4 (Plutus V3);
- an implementation of Plutus cost models and the related costing functions.

## Quality
The quality of the code has been evaluated using a copy of [the official Plutus conformance tests ](https://github.com/IntersectMBO/plutus/tree/master/plutus-conformance/test-cases/uplc/evaluation) excluding tests of builtins that were not part of [the Plutus core spec](https://plutus.cardano.intersectmbo.org/resources/plutus-core-spec.pdf) as of September, 2024. All 487 test cases have been successfully passed. The copied test cases are located in [data/plutus/conformance](/data/plutus/conformance) subdirectory of this repository and the code of the test is located in [lib/dt/plutus/machine.test.cpp](/lib/dt/plutus/machine.test.cpp).

## Test it
Clone this repository and build and start a Docker container with the test environment:
```
git clone https://github.com/sierkov/daedalus-turbo.git dt
cd dt
docker build -t dt -f Dockerfile.test .
docker run -it --rm dt
```

Within the terminal window opened by the previous command, run the following command to evaluate an example Plutus script:
```
./dt plutus-eval ../data/plutus/conformance/example/factorial/factorial.uplc
```

```../data/plutus/conformance/``` directory within the docker image comes with 487 example Plutus scripts that were used to test the implementation.

To test your own scripts, you can use the following command to start the test container; Replace ```<script-dir>``` with a directory with your scripts on the host machine:
```
docker run -it --rm -v <script-dir>:/script dt
```

To evaluate a Plutus script in the binary on-chain format, use the following command:
```./dt plutus-eval --format=flat /script/my-script.bin```

## Next steps
Test and optimize the implementation on all scripts stored on the Cardano mainnet.