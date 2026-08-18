#!/usr/bin/env python3
"""
Group a multi-job run's per-NPU report lines into per-job completion times.

The simulator writes one "sys[N] finished, <ticks> cycles" line per NPU and knows
nothing about jobs. Job membership comes from two JSON files:

    comm_group     {group_id: [npu ids]}      -- which hosts form each ring
    job_allocation {job_id:  [group ids]}     -- which rings belong to each job

A job's completion time (makespan) is the latest finish tick over its own NPUs.
Ticks are nanoseconds.

Usage:
    multijob_report.py <report.txt> <comm_group.json> <job_allocation.json>

Example:
    python3 results/multijob_report.py \\
        extern/network_backend/ns-3/scratch/output/report_mj_....txt \\
        inputs/comm_group/128host_32ring.json \\
        inputs/job_allocation/2_job_32_group_16_16.json
"""
import json
import re
import sys


def main(report_path, comm_group_path, job_alloc_path):
    comm_group = json.load(open(comm_group_path))
    job_alloc = json.load(open(job_alloc_path))

    ticks = {}
    for line in open(report_path):
        m = re.match(r"sys\[(\d+)\] finished, (\d+) cycles", line.strip())
        if m:
            ticks[int(m.group(1))] = int(m.group(2))

    missing_groups = sorted(
        {str(g) for groups in job_alloc.values() for g in groups} - set(comm_group)
    )
    if missing_groups:
        sys.exit(
            f"ERROR: job allocation references group(s) {missing_groups} that are not in "
            f"{comm_group_path}. The comm-group and job-allocation files do not match."
        )

    print(f"report        : {report_path}")
    print(f"comm group    : {comm_group_path}  ({len(comm_group)} groups)")
    print(f"job allocation: {job_alloc_path}  ({len(job_alloc)} jobs)")
    print(f"NPUs reported : {len(ticks)}\n")

    print(f"{'job':>5}  {'groups':>7}  {'NPUs':>5}  {'first done':>12}  {'makespan':>12}")
    print("-" * 52)

    covered = set()
    for job, groups in sorted(job_alloc.items(), key=lambda kv: int(kv[0])):
        npus = [n for g in groups for n in comm_group[str(g)]]
        covered.update(npus)
        have = [ticks[n] for n in npus if n in ticks]
        if not have:
            print(f"{job:>5}  {len(groups):>7}  {len(npus):>5}  {'NO DATA':>12}  {'NO DATA':>12}")
            continue
        print(
            f"{job:>5}  {len(groups):>7}  {len(npus):>5}  "
            f"{min(have)/1e6:>10.3f} ms  {max(have)/1e6:>10.3f} ms"
        )

    stray = sorted(set(ticks) - covered)
    if stray:
        print(f"\nWARNING: {len(stray)} NPU(s) finished but belong to no job: {stray[:16]}"
              f"{' ...' if len(stray) > 16 else ''}")
    absent = sorted(covered - set(ticks))
    if absent:
        print(f"\nWARNING: {len(absent)} NPU(s) in the job allocation never reported: "
              f"{absent[:16]}{' ...' if len(absent) > 16 else ''} -- the run is incomplete.")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
