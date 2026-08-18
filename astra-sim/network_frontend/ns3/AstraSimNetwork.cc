#include <json/json.hpp>
#include "astra-sim/system/AstraNetworkAPI.hh"
#include "astra-sim/system/Sys.hh"
#include "extern/remote_memory_backend/analytical/AnalyticalRemoteMemory.hh"

#include <execinfo.h>
#include <stdio.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include "entry.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"

using namespace std;
using namespace ns3;
using json = nlohmann::json;

class ASTRASimNetwork : public AstraSim::AstraNetworkAPI {
 public:
  bool allow_overlapping;
  ASTRASimNetwork(int rank, bool allow_overlapping) : AstraNetworkAPI(rank) {
    this->allow_overlapping = allow_overlapping;
  }

  ~ASTRASimNetwork() {}

  int sim_finish() {
    for (auto it = node_to_bytes_sent_map.begin();
         it != node_to_bytes_sent_map.end();
         it++) {
      pair<int, int> p = it->first;
      if (p.second == 0) {
        cout << "All data sent from node " << p.first << " is " << it->second
             << "\n";
      } else {
        cout << "All data received by node " << p.first << " is " << it->second
             << "\n";
      }
    }
    exit(0);
    return 0;
  }

  double sim_time_resolution() {
    return 0;
  }

  void handleEvent(int dst, int cnt) {}

  AstraSim::timespec_t sim_get_time() {
    AstraSim::timespec_t timeSpec;
    timeSpec.time_res = AstraSim::NS;
    timeSpec.time_val = Simulator::Now().GetNanoSeconds();
    return timeSpec;
  }

  virtual void sim_schedule(
      AstraSim::timespec_t delta,
      void (*fun_ptr)(void* fun_arg),
      void* fun_arg) {
    Simulator::Schedule(NanoSeconds(delta.time_val), fun_ptr, fun_arg);
    return;
  }

  virtual int sim_send(
      void* buffer,
      uint64_t message_size,
      int type,
      int dst_id,
      int tag,
      AstraSim::sim_request* request,
      void (*msg_handler)(void* fun_arg),
      void* fun_arg) {
    int src_id = rank;

    // Trigger ns3 to schedule RDMA QP event.
    send_flow(src_id, dst_id, message_size, msg_handler, fun_arg, tag, this->allow_overlapping);
    return 0;
  }

  virtual int sim_recv(
      void* buffer,
      uint64_t message_size,
      int type,
      int src_id,
      int tag,
      AstraSim::sim_request* request,
      void (*msg_handler)(void* fun_arg),
      void* fun_arg) {
    int dst_id = rank;
    MsgEvent recv_event =
        MsgEvent(src_id, dst_id, 1, message_size, fun_arg, msg_handler);
    MsgEventKey recv_event_key =
        make_pair(tag, make_pair(recv_event.src_id, recv_event.dst_id));

    if (received_msg_standby_hash.find(recv_event_key) !=
        received_msg_standby_hash.end()) {
      // 1) ns3 has already received some message before sim_recv is called.
      int received_msg_bytes = received_msg_standby_hash[recv_event_key];
      if (received_msg_bytes == message_size) {
        // 1-1) The received message size is same as what we expect. Exit.
        received_msg_standby_hash.erase(recv_event_key);
        recv_event.callHandler();
      } else if (received_msg_bytes > message_size) {
        // 1-2) The node received more than expected.
        // Do trigger the callback handler for this message, but wait for Sys
        // layer to call sim_recv for more messages.
        received_msg_standby_hash[recv_event_key] =
            received_msg_bytes - message_size;
        recv_event.callHandler();
      } else {
        // 1-3) The node received less than what we expected.
        // Reduce the number of bytes we are waiting to receive.
        received_msg_standby_hash.erase(recv_event_key);
        recv_event.remaining_msg_bytes -= received_msg_bytes;
        sim_recv_waiting_hash[recv_event_key] = recv_event;
      }
    } else {
      // 2) ns3 has not yet received anything.
      if (sim_recv_waiting_hash.find(recv_event_key) ==
          sim_recv_waiting_hash.end()) {
        // 2-1) We have not been expecting anything.
        sim_recv_waiting_hash[recv_event_key] = recv_event;
      } else {
        // 2-2) We have already been expecting something.
        // Increment the number of bytes we are waiting to receive.
        int expecting_msg_bytes =
            sim_recv_waiting_hash[recv_event_key].remaining_msg_bytes;
        recv_event.remaining_msg_bytes += expecting_msg_bytes;
        sim_recv_waiting_hash[recv_event_key] = recv_event;
      }
    }
    return 0;
  }
};

// Command line arguments and default values.
string workload_configuration;
string workload_name;
string system_configuration;
string system_name;
string network_configuration;
string network_name;
string memory_configuration;
string comm_group_configuration;
// Multi-job / multi-tenant labelling. comm_group_name, when non-empty, is appended to
// the output directory name so concurrent-job runs land in their own directory.
// job_configuration and job_name are currently labels only: the job allocation file
// (inputs/job_allocation/*.json, mapping job id -> communicator group ids) is not read
// by the simulator. It exists so that post-processing can group the per-NPU report
// lines by job. Job behaviour itself comes entirely from the workload traces.
string comm_group_name;
string job_configuration;
string job_name;
string logical_topology_configuration;
string logical_topology_name;
string report_record_file_path;
int num_queues_per_dim = 1;
double comm_scale = 1;
double injection_scale = 1;
bool rendezvous_protocol = false;
bool allow_overlapping = true;
auto logical_dims = vector<int>();
int num_npus = 1;
auto queues_per_dim = vector<int>();

// TODO: Migrate to yaml
void read_logical_topo_config(
    string network_configuration,
    vector<int>& logical_dims) {
  ifstream inFile;
  inFile.open(network_configuration);
  if (!inFile) {
    cerr << "Unable to open file: " << network_configuration << endl;
    exit(1);
  }

  // Find the size of each dimension.
  json j;
  inFile >> j;
  if (j.contains("logical-dims")) {
    vector<string> logical_dims_str_vec = j["logical-dims"];
    for (auto logical_dims_str : logical_dims_str_vec) {
      logical_dims.push_back(stoi(logical_dims_str));
    }
  }

  // Find the number of all npus.
  stringstream dimstr;
  for (auto num_npus_per_dim : logical_dims) {
    num_npus *= num_npus_per_dim;
    dimstr << num_npus_per_dim << ",";
  }
  cout << "There are " << num_npus << " npus: " << dimstr.str() << "\n";

  queues_per_dim = vector<int>(logical_dims.size(), num_queues_per_dim);
}

// Read command line arguments.
void parse_args(int argc, char* argv[]) {
  CommandLine cmd;
  
  cmd.AddValue(
      "workload-configuration",
      "Workload configuration file path.",
      workload_configuration);

  cmd.AddValue(
      "workload-name",
      "Workload configuration name.",
      workload_name);
  
  cmd.AddValue(
      "system-configuration",
      "System configuration file path",
      system_configuration);
  
  cmd.AddValue(
      "system-name",
      "System configuration name",
      system_name);
  
  cmd.AddValue(
      "network-configuration",
      "Network configuration file path",
      network_configuration);

  cmd.AddValue(
      "network-name",
      "Network configuration name",
      network_name);
  
  cmd.AddValue(
      "remote-memory-configuration",
      "Memory configuration file",
      memory_configuration);
  
  cmd.AddValue(
      "comm-group-configuration",
      "Communicator group configuration file",
      comm_group_configuration);

  cmd.AddValue(
      "comm-group-name",
      "Communicator group name; when set, it is added to the output directory name",
      comm_group_name);

  cmd.AddValue(
      "job-configuration",
      "Job allocation file (job id -> communicator group ids). Label only: read by "
      "post-processing, not by the simulator",
      job_configuration);

  cmd.AddValue(
      "job-name",
      "Job allocation name, used to label report files",
      job_name);

  cmd.AddValue(
      "logical-topology-configuration",
      "Logical topology configuration file",
      logical_topology_configuration);

  cmd.AddValue(
      "topology-name",
      "Topology name (e.g. 4x32, 8x16 etc.)",
      logical_topology_name);

  cmd.AddValue(
      "report-record-file",
      "Report record file path",
      report_record_file_path);

  cmd.AddValue(
      "allow-overlapping",
      "Allow multiple flows from the same source to the same destination",
      allow_overlapping);

  cmd.AddValue(
      "num-queues-per-dim",
      "Number of queues per each dimension",
      num_queues_per_dim);
  cmd.AddValue("comm-scale", "Communication scale", comm_scale);
  cmd.AddValue("injection-scale", "Injection scale", injection_scale);
  cmd.AddValue(
      "rendezvous-protocol",
      "Whether to enable rendezvous protocol",
      rendezvous_protocol);

  cmd.Parse(argc, argv);
}

void CheckAllFinished (vector<AstraSim::Sys*> systems) {
  // for (uint32_t i = 0; i < num_npus; i++) {
  //   std::cout << "Node " << i << ", Pending Events: " << systems[i]->pending_events
  //             << ", In-flight CPU Ops: " << systems[i]->workload->hw_resource->num_in_flight_cpu_ops
  //             << ", In-flight GPU Comp Ops: " << systems[i]->workload->hw_resource->num_in_flight_gpu_comp_ops
  //             << ", In-flight GPU Comm Ops: " << systems[i]->workload->hw_resource->num_in_flight_gpu_comm_ops
  //             << std::endl;
  // }

  for (uint32_t i = 0; i < num_npus; i++) {
    if (!(systems[i]->workload->is_finished)) {
      Simulator::Schedule(MicroSeconds(1000), &CheckAllFinished, systems);
      return;
    }
  }

  Simulator::Stop(Seconds(0));
}

// Main function of the whole simulation.
int main(int argc, char* argv[]) {
  LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
  LogComponentEnable("PacketSink", LOG_LEVEL_INFO);

  cout << "ASTRA-sim + NS3" << endl;


  // Read network config and find logical dims.
  parse_args(argc, argv);
  read_logical_topo_config(logical_topology_configuration, logical_dims);

  // Setup network & System layer.
  vector<ASTRASimNetwork*> networks(num_npus, nullptr);
  vector<AstraSim::Sys*> systems(num_npus, nullptr);
  Analytical::AnalyticalRemoteMemory* mem =
      new Analytical::AnalyticalRemoteMemory(memory_configuration);

  FILE* report_record_file = nullptr;
  if (report_record_file_path != "") {
    report_record_file = fopen(report_record_file_path.c_str(), "w");
    if (report_record_file == nullptr) {
      std::cerr << "Error opening report record file: "
                << report_record_file_path << std::endl;
      exit(1);
    }
  }

  for (int npu_id = 0; npu_id < num_npus; npu_id++) {
    networks[npu_id] = new ASTRASimNetwork(npu_id, allow_overlapping);
    systems[npu_id] = new AstraSim::Sys(
        npu_id,
        workload_configuration,
        comm_group_configuration,
        system_configuration,
        mem,
        networks[npu_id],
        logical_dims,
        queues_per_dim,
        injection_scale,
        comm_scale,
        rendezvous_protocol,
        report_record_file);
  }

  // Initialize ns3 simulation.
  // int setup_ns3_simulation(string network_configuration, string workload_name, string system_name, string network_name, string topo_name) {
  setup_ns3_simulation(network_configuration, workload_name, system_name, network_name, logical_topology_name, allow_overlapping, comm_group_name);
  std::cout<<"------------------------------------------" << std::endl;
  std::cout << "ASTRA-sim + NS3 is ready." << std::endl;
  fflush(stdout);

  // Tell workload layer to schedule first events.
  for (int i = 0; i < num_npus; i++) {
    // Print debug info
    // std::cout << "Fire workload event on node " << i << std::endl;
    systems[i]->workload->fire();
  }
  
  std::cout<<"------------------------------------------" << std::endl;
  std::cout << "Workload Events Fired" << std::endl;
  fflush(stdout);

  // Run the simulation by triggering the ns3 event queue.
  //
  // Now, do the actual simulation.
  //
  std::cout << "------------------------------------------" << std::endl;
  std::cout << "Running Simulation.\n";
  fflush(stdout);

  Simulator::Schedule(MicroSeconds(20), &CheckAllFinished, systems);

  NS_LOG_INFO("Run Simulation.");
  Simulator::Run();
  // Simulator::Stop(Seconds(2000000000));
  Simulator::Destroy();
  return 0;
}
