## The running scripts

This folder contains the scripts to run the tests.
They are designed to be able to run batched tests with different settings.

- under the `end_to_end_tests` folder, the scripts run simulated ML training jobs
    - For Resnet50 workload under 32 nodes or 128 nodes: `build_32_end_to_end_resnet50_batch.sh` and `build_128_end_to_end_resnet50_batch.sh`
    - For VGG16 workload under 32 nodes or 128 nodes: `build_32_end_to_end_vgg16_batch.sh` and `build_128_end_to_end_vgg16_batch.sh`
- under the `microbenchmarks` folder, the scripts run individual operations 
    - Under 32 nodes: `build_32_allreduce_batch.sh`
    - Under 64 nodes: `build_64_allreduce_batch.sh`
    - Under 128 nodes: `build_128_allreduce_batch.sh`
    - These run a "multiple 1D ring" AllReduce: instead of one hierarchical collective across all
      hosts, hosts are split by ToR pod position into several independent rings (e.g. for 32 hosts,
      hosts `{0,8,16,24}` form one ring, `{1,9,17,25}` another, etc., each ring crossing all pods),
      so multiple ring-AllReduces run concurrently. Which pattern is used is driven by the workload
      trace and `--comm-group-configuration` (see `inputs/comm_group/`), not the network/topology
      config.
- there is a `build_onesend.sh` script, which is for debugging purpose, just in case you need it

For the detailed workload settings, please refer to the comments in each script, you can decide what workload/settings you want to run by modifying the corresponding arrays in the scripts.

### For evaluation
During evaluation, the microbenchmark tests are more important, because they can show the performance of individual operations, which can help identify the bottlenecks.

For the end-to-end tests, they are good to have, you can include them in your report to make it more convincing, but for this project, we will not use them for performance grading part. 

During our validation of performance, we will only run the microbenchmark tests, please make sure they are all working correctly with your solution.

### How to use the scripts

To run a script, you can just run it in the terminal, for example:
(Assume you are under `/app/astra-sim`)
```bash
# To compile:
./build_scripts/astra_ns3/end_to_end_tests/build_32_end_to_end_resnet50_batch.sh -c
# or
./build_scripts/astra_ns3/end_to_end_tests/build_32_end_to_end_resnet50_batch.sh --compile

# To run:
./build_scripts/astra_ns3/end_to_end_tests/build_32_end_to_end_resnet50_batch.sh -r
# or
./build_scripts/astra_ns3/end_to_end_tests/build_32_end_to_end_resnet50_batch.sh --run

# To clean the compiled files:
./build_scripts/astra_ns3/end_to_end_tests/build_32_end_to_end_resnet50_batch.sh --clean
# Clean will remove the compiled files, but will not remove your output records. 

# If you repeatedly run the same workload, the output will be overwritten by latest run. 
# Please be careful, backup your results if needed.
```

The scripts are designed to run the tests one by one.

One running of a script will only take one CPU core.
Different scripts can run concurrently to use up available CPU cores, if you need to save time. 

#### Tricks for debugging

In case you encounter segmentation fault which usually does not give you a stack trace, you can try to run the code under gdb to get more information by changing the corresponding line in the script to:

```bash
gdb -ex=r -ex=bt --batch \
    --args "${NS3_DIR}/build/scratch/ns3.42-AstraSimNetwork-debug" \
    --config="${network}" \
    ...
```