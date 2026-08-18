# CS5229 NS3 Programming Assignment

This is the programming assignment for CS5229 (Advanced Computer Networks) at NUS. You'll be working with a distributed ML training simulator — ASTRA-sim on top of NS-3 — to see how network-level design choices (routing, congestion control, queueing) affect how fast a distributed training job actually finishes.

If you haven't already, read the background briefing PDF before you touch any code. It covers the concepts you need (Ring AllReduce, RDMA, ECN, ECMP) and walks through exactly the workload you'll be running in this assignment. 

This README is about getting the environment up and telling you where things are in the repo; the PDF is where the "why" lives.

## Getting Started

We strongly recommend using Docker rather than trying to set everything up natively. Docker saves you from a lot of the pain from setting the dependencies up. You'll need Docker installed.

This should work on Linux, macOS, and Windows. 

One caveat: Docker Desktop can be flaky on Linux and Windows for long-running containers, so if you run into weirdness, try the Docker CLI directly instead of the desktop app. (If you've never used Docker before, spend tem minutes on a basic tutorial first — it'll save you time later.)

### A known issue with End-to-End tests

The end-to-end tests (full ResNet50/VGG16-style training workloads, as opposed to the microbenchmarks) have a known bug somewhere in ASTRA-sim's internals: occasionally a run just stalls, and `fct.txt` stops getting new lines. We haven't been able to track this down — it's upstream, not something in the code you're touching.

**This does not affect the microbenchmark tests, which is what your grade is actually based on.** If you run the end-to-end tests anyway (they're good supporting evidence for your report), here's what to expect and do:

- Under baseline settings, the 32-node end-to-end run should finish in under 20 minutes, and the 128-node one under 30. If you're past 40 minutes, it's almost certainly stuck, not just slow.
- Kill it (`Ctrl+C`), bump the seed (`RANDOM_SEEDS=(1)` → `RANDOM_SEEDS=(2)`), and try again.
- If you're short on time, run a few seeds in parallel and keep whichever finishes.
- For day-to-day development, just stick to the microbenchmarks — they're smaller, faster, and don't have this problem.

### Build and Run

1. Clone the repo, `cd` into it.
2. Build the image:
   ```bash
   docker build -t cs5229-astra-sim .
   ```
3. Start a container, mounting the repo into it:
   ```bash
   docker run -dit --name cs5229-astra-sim --mount type=bind,source=[absolute path to this directory],target=/app/astra-sim cs5229-astra-sim:latest
   ```
4. Get a shell inside it:
   ```bash
   docker exec -it cs5229-astra-sim bash
   ```
   `exit` to leave it. The container keeps running in the background, so you don't need to rebuild anything between sessions — just `docker exec` back in.
5. Everything lives under `/app/astra-sim` inside the container. The scripts you'll actually run are under `build_scripts/astra_ns3/` — see the `README.md` in that folder for how to use them.
6. Output shows up under `extern/network_backend/ns-3/scratch/output/`. If a field in the output isn't self-explanatory, check `extern/network_backend/ns-3/scratch/output_document/` — every output file format is documented there.

One thing that trips people up: **re-running the same experiment overwrites its previous output.** If you want to keep a result around for comparison later, copy it out first.

## The assignment, briefly

The workload you'll be simulating is a "multiple 1D ring" AllReduce: instead of one big collective spanning every host, hosts are split into several independent 4-node rings (one host per pod), and those rings all run concurrently. The background PDF walks through exactly why this pattern was chosen and derives an idealized completion time for it — use that as your sanity check for whatever you measure in simulation.

The physical network is a spine-leaf topology, and there are three network config variants per host-count tier (32/64/128 nodes) sharing the same topology and bandwidth, differing only in which load-balancing mode is active:

- `..._placeholder.txt` — `LB_MODE 0`. A deliberately bad routing policy (always picks the same path). This is not something you're meant to fix; it's there as a "how bad can it get" reference point.
- `..._ecmp_baseline.txt` — `LB_MODE 1`. This is where your ECMP implementation goes. Right now it's shipped as the same broken placeholder as mode 0 — **implementing the real ECMP here is Milestone 1**.
- `..._solution.txt` — `LB_MODE 2`. Reserved for your own custom load-balancing algorithm, if you choose to go this way.

Don't read too much into the word "baseline" showing up in two different config names — `_ecmp_baseline` is deliberately named that way because once you've implemented ECMP, *that* becomes your actual point of comparison, not the placeholder.

## Milestones

**Milestone 1** has two parts. First, get the simulator running and actually look at what it's doing — run the placeholder config, look at `fct.txt` and `qlen.txt`, and compare what you observe against the idealized completion time from the PDF. You'll find actually there's a large gap, and figuring out roughly why (queueing? bad path selection? something else?) is the point of this step. Second, implement real ECMP hashing in `load-balancing-ecmp.cc` (selected via `LB_MODE 1`), and show that it actually improves on the placeholder. But, you will also find that ECMP still shows a performance gap, how would you propose a further improvement?

**Milestone 2** is open-ended, and we'll say more about it separately closer to the time. Broadly: having seen where the network struggles, you pick a problem, propose a fix, and implement it — in load balancing, congestion control, ECN, queue scheduling, or wherever you think the bottleneck actually is. There's no fixed checklist of "implement all four subsystems"; the four editable files below are starting points, not requirements.

### Challenging scenarios

Part of Milestone 2 is that **your solution will also be run against scenarios we do not publish
in advance.** The point is to reward a solution that is genuinely robust rather than one tuned to
the exact setup you developed against.

To keep this fair, here is what will and will not change.

**What stays the same:** a spine-leaf fabric, RoCE-style RDMA with go-back-N recovery, DCQCN as the
default congestion control, ECMP as the default load balancing, and ring-AllReduce collectives driven by ASTRA-sim.

**What may change:** the number of hosts and the shape of the topology (including the number of
equal-cost paths between a pair of ToRs, which will not always be four, and pods that do not all
have the same number of uplinks), per-link speeds, which collectives run and how big they are, and
whether other jobs are sharing the fabric with the one you are measuring. 

The practical consequences for how you write your code:

- **Do not hardcode the path count.** `m_dstIPRouting[dip]` gives you the equal-cost interfaces for
  a destination; use `.size()`, never a literal 4.
- **Do not assume the path set is fixed for the whole run.** Routes are recomputed if a link goes
  down, and your `m_dstIPRouting` is updated in place when that happens. Anything you cache keyed on a port index — per-path counters, flowlet state, round-robin cursors — must be robust when a port disappear from the list.
- **Do not assume every host is running your collective.** In real life, host users may run random traffics that don't belong to the collective workload. Make sure that won't break your system. 

Two of these scenario types are things you can experiment with yourself:

- **Link failure.** `LINK_DOWN <time_us> <nodeA> <nodeB>` in a network config takes a link down part
  way through a run, recomputes all routes and redistributes queue pairs. See
  `scratch/config/config_doc.txt`. Choose a link whose loss does not disconnect anything — e.g. one
  of a ToR's four spine uplinks — otherwise routing has nowhere to send packets and the run aborts. In ordinary ECMP, this won't cause a big mess, but does link failures break your solution?
- **Multi-tenant.** `build_scripts/astra_ns3/challenge_example/build_multijob_batch.sh` runs workloads in
  which several independent jobs share the fabric, differing in how many hosts they span, how big
  their messages are, how many passes they run and when they start. Three 128-host examples ship
  under `inputs/workload/multi_job_challenge/`, plus a small 32-host one to test against — see the
  runtime table below, and start with the small one.

  A multi-job run is described by three files that must agree, and nothing checks them for you:
  the trace directory (`job.<npu>.et`, one per host, with each host's job, message size, pass count
  and start delay baked in), `inputs/comm_group/*.json` (`{group id: [npu ids]}`, the rings), and
  `inputs/job_allocation/*.json` (`{job id: [group ids]}` — group ids, not NPU ids). The last one is metadata: the simulator does not read it. The batch script header explains how they fit together.

  The report is still one line per NPU, so use `results/multijob_report.py <report> <comm_group>
  <job_allocation>` to turn it into per-job completion times. 
  If you want to build your own combinations, `workload_generator/generate_multi_job_workload.sh` takes a job-setup JSON and emits the traces — run it inside the container.

### What you are required to run

The microbenchmark scripts sweep five message sizes per host-count tier. Not all of them are
required, because the largest ones cost hours of simulation time each (see the table below).

**Required, for every host-count tier (32 / 64 / 128 nodes):**

- 32 MB, 64 MB, and 128 MB single pass
- every network config you are claiming a result for — at minimum `placeholder` and
  `ecmp_baseline` for Milestone 1
- **averaged over at least 10 random seeds for your report results** to avoid noise from the randomness in the simulator. But during development, you can run a single seed to save time — just be aware that the results are noisy.

The point of running all three tiers is to show that your solution holds up *across scales*, not
just at one convenient size. A change that helps at 32 nodes and hurts at 128 is a real finding —
but only if you ran both.

**Optional:** the 256MB x 1-pass and 256 MB × 4-pass workloads are optional. 
But it is a good supporting evidence and exposes behaviour that a single collective does not.

### How long the runs take

We measured on an idle old x86 server, one simulation per core, debug build. 
Times are per single run (one workload × one network config × **one seed**). 
Not all workloads are shown, just to give you a sense of scale. 

| Host count | 32 MB | 64 MB | 128 MB | All three, one config, one seed |
|---|---:|---:|---:|---:|
| 32 nodes | 3.9 min | 7.8 min | 15.6 min | **27 min** |
| 64 nodes | 8.8 min | 17.6 min | 34.7 min | **61 min** |
| 128 nodes | 14.9 min | 30.2 min | 60.1 min | **105 min** |

**What the required sweep actually costs.** For example, if we multiply the right-hand column by 3 network configs and 10 seeds:

| Host count | Required sweep (single-core time) |
|---|---:|
| 32 nodes | ~14 hours |
| 64 nodes | ~31 hours |
| 128 nodes | ~53 hours |
| **Total** | **~97 core-hours** |

That is *core*-hours, not wall-clock hours — how long it actually takes depends entirely on how
many runs you have going at once. Running the three batch scripts concurrently (3 cores) leaves the
128-node script alone taking over two days. Do not plan around that; parallelise instead.

**Parallelising.** Each script iterates its seeds serially, so the practical way to use more cores
is to split the seeds across concurrent shells with the `SEEDS` environment variable:

```bash
# 10 concurrent shells, one seed each -- ~10x faster than the default serial sweep
for s in 1 2 3 4 5 6 7 8 9 10; do
  SEEDS="$s" ./build_scripts/astra_ns3/microbenchmarks/build_128_allreduce_batch.sh -r &
done
```

**Multi-tenant workloads** are heavier again, because several jobs are pushing traffic at once and the longest-running job sets the wall time.  
Both 128-host examples were measured on the same server:

| Workload | What is running | Measured |
|---|---|---:|
| `2job_32ring_16_16_32mb_32mb_16pass_200ms` | 2 equal jobs, 64 hosts each, 32 MB × 16 passes, second starts 200 ms late | **4.1 hours** |
| `3job_32ring_16_8_8_32mb16_16mb16_4mb64_200ms_300ms` | 3 jobs of 64/32/32 hosts, 32 MB × 16 / 16 MB × 16 / 4 MB × 64 passes, staggered | **3.1 hours** |

There is also a small 32-host, 2-job workload (`2job_8ring_2x4_32mb_32mb_nodelay`, ~6 minutes,
commented out in the batch script). **Start with that one** — it exercises the whole multi-job path
in minutes, so you can confirm your setup and your post-processing work before committing hours to a 128-host run.

These are for you to experiment with; they are not part of the required sweep, and the scenarios you are actually graded on are not published. Running one of them once per network config is plenty — do not put them in a 10-seed sweep unless you have the cores to spare.

Two things follow from this:

1. **Start the required sweep early.** Multiply by 3 network configs and 10 seeds and it is not
   something you can begin the night before.
2. **Run scripts in parallel.** Each simulation is single-threaded and uses one core. The three
   batch scripts can run concurrently, and if you have cores to spare you can run more. Memory usually is not the constraint — a single run peaks well under 1 GB, but be careful of your solution's affects.

If a microbenchmark run seems stuck, check whether `fct.txt` in its output directory is still
growing before assuming it has hung — the largest runs go for long stretches between flow
completions. 

### Overall workflow

Roughly, for either milestone:

1. Get the environment running and confirm the placeholder config actually executes.
2. Look at the output and figure out where the gap is between what you measured and what you'd expect (the PDF shows you how to compute the idealized estimate). Check queue occupancy, link utilization, ECN marks — whatever's relevant to your hypothesis.
3. Once you're fairly sure you've found a real problem (not just noise), think about which part of the system it belongs to, and go implement a fix in the corresponding file(s) below.
4. Re-run and compare against the placeholder config. Keep the network config and topology identical across runs; only change what you're actually testing, otherwise the comparison isn't fair.
5. Iterate based on what the new results tell you.
6. Write up your findings — plots make a much stronger case than a table of numbers, so budget time for them.

## Codes that you can edit

These are the files that map onto the network mechanisms discussed in the background PDF. You're not restricted to only these — if your idea needs to touch something else, that's fine, as long as you don't break the underlying infrastructure assumptions (link speeds, topology, etc.).

(All paths below are relative to `extern/network_backend/ns-3/`, i.e. `src/point-to-point/model/rdma-hw.*` really means `extern/network_backend/ns-3/src/point-to-point/model/rdma-hw.*`.)

- **Load balancing** — three selectable implementations, picked via `LB_MODE` in the network config, dispatched in `src/point-to-point/model/switch-node.cc`:
  - `LB_MODE 0`, `src/point-to-point/model/load-balancing-placeholder.*` — the deliberately-bad reference. Not yours to edit.
  - `LB_MODE 1`, `src/point-to-point/model/load-balancing-ecmp.*` — **this is Milestone 1.** Implement real ECMP here.
  - `LB_MODE 2`, `src/point-to-point/model/load-balancing-customized.*` — your own load-balancing idea, for Milestone 2.
- **Host-side congestion control**: `src/point-to-point/model/rdma-hw.*` — relevant for Milestone 2 if your problem turns out to be about congestion control rather than routing.
- **Switch-side congestion signaling (ECN)**: `src/point-to-point/model/switch-mmu.*` — same, Milestone 2 territory.
- **Queue scheduling**: `src/network/utils/broadcom-egress-queue.*` (note: this one lives under `src/network/`, not `src/point-to-point/`) — same.

The configuration files live under `extern/network_backend/ns-3/scratch/config/`. Feel free to add your own — just remember to update the corresponding batch script under `build_scripts/astra_ns3/` to point at it.

### The four mode switches

Each of the four editable subsystems is selected by one key in the network config file. All four
follow the same pattern: the config key is parsed in `scratch/common.h`, copied into a `Settings::`
field, and read at a dispatch site that picks between the default implementation and yours. If you
add a new implementation, you wire it in at the dispatch site — you do not need to touch the
config parser.

(Code paths below are relative to `extern/network_backend/ns-3/`.)

| Config key | Values | Dispatch site | Implementations |
|---|---|---|---|
| `LB_MODE` | 0 / 1 / 2 | `src/point-to-point/model/switch-node.cc:122-129` | `load-balancing-{placeholder,ecmp,customized}.cc` |
| `CUSTOMIZED_ECN` | 0 / 1 | `src/point-to-point/model/switch-node.cc:218-220` | `switch-mmu.cc` → `ShouldSendECN` (0) / `ShouldSendECN_Customized` (1) |
| `QUEUE_MGMT_MODE` | 0 / 1 | `src/point-to-point/model/qbb-net-device.cc:337-347` | `broadcom-egress-queue.cc` → `DequeueRR` (0) / `DequeueCustomized` (1) |
| `CC_MODE` | 1 / 2 | `src/point-to-point/model/rdma-hw.cc:217,408` | DCQCN (1) / `cnp_received_customized` + `HandleAckCustomized` (2) |

What the "customized" slots ship as:

- **`CUSTOMIZED_ECN 1`** — currently a byte-for-byte copy of the mode-0 function. Selecting it
  runs, and behaves identically, until you change it.
- **`QUEUE_MGMT_MODE 1`** — **not implemented**. `DoDequeueCustomized` unconditionally asserts, so
  selecting mode 1 crashes the simulator on the first dequeue. That is by design: implement it
  before you select it.
- **`CC_MODE 2`** — a customized-CC skeleton. `CC_MODE 1` (DCQCN) is the working baseline that all
  shipped configs use; leave it there unless congestion control is the thing you are changing.

`CC_MODE` also accepts other upstream values (3 = HPCC, 7 = TIMELY, …) that are documented in
`scratch/config/config_doc.txt`. They are not part of this assignment's baseline — if you use one,
say so explicitly in your report, because it changes the transport underneath every other result.

**All nine shipped configs set `CUSTOMIZED_ECN 0`, `QUEUE_MGMT_MODE 0`, `CC_MODE 1`.** Within a
host-count tier, the three configs differ *only* in `CONFIG_NAME` and `LB_MODE` — verify with
`diff` if you ever doubt it. Keep it that way: turn on exactly one customization at a time, or you
will not be able to attribute a result to a cause.

A note on fairness: if you're comparing two configurations (say, placeholder vs. your ECMP), the *only* thing that should differ between them is the thing you're actually testing. Everything else — topology, bandwidth, buffer sizes, other CC/ECN/scheduling settings — needs to stay identical, or your comparison doesn't mean anything. When you write your report, be explicit about exactly which parameters you changed and why.

### Other things worth knowing

- Network setup lives in `extern/network_backend/ns-3/scratch/common.h`. This is where topology, routing tables, and per-run config values all get wired together.
- The main entry point is `astra-sim/network_frontend/ns3/AstraSimNetwork.cc`.
- Everything under `astra-sim/` is the workload-generation and collective-execution layer from ASTRA-sim itself. Leave it alone — it's the fixed assumption the rest of the assignment is built on.
- `src/point-to-point/model/rdma-*` is the RDMA transport model — worth reading if you want to understand how packets actually move, even if you don't end up editing it.
- `src/point-to-point/model/switch-*`: the high-level switch behavior is in `switch-node.*`, the lower-level buffer/ECN/admission logic is in `switch-mmu.*`.

## Reference

This assignment builds on [ASTRA-sim 2.0](https://astra-sim.github.io/), a distributed ML system simulator developed by Intel, Meta, and Georgia Tech. If you want to go beyond what's needed for the assignment, their site and repo are worth a look.

Thanks to the ASTRA-sim team — this project is what makes it possible to experiment with distributed training network behavior in NS-3 without needing an actual cluster.

Licensed under the MIT License — see [LICENSE](LICENSE). Original authorship and code ownership are preserved; we've modified parts of the upstream code for this assignment, noted in comments where relevant.
