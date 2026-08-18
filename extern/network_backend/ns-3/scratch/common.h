/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#undef PGO_TRAINING
#define PATH_TO_PGO_CONFIG "path_to_pgo_config"

#include <fstream>
#include <iostream>
#include <time.h>
#include <unordered_map>
#include <filesystem>
#include <regex>

#include <ns3/assert.h>
#include <ns3/rdma-client-helper.h>
#include <ns3/rdma-client.h>
#include <ns3/rdma-driver.h>
#include "ns3/rdma-hw.h"
#include <ns3/rdma.h>
#include <ns3/sim-setting.h>
#include <ns3/switch-node.h>
#include "ns3/settings.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/error-model.h"

#include "ns3/qbb-helper.h"
#include "ns3/qbb-net-device.h"

#include "ns3/global-route-manager.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/packet.h"
#include "ns3/point-to-point-helper.h"

using namespace ns3;
using namespace std;
namespace fs = std::filesystem;

NS_LOG_COMPONENT_DEFINE("GENERIC_SIMULATION");

/*-----------------------------------*/
// 
// This file contains the detailed network setup steps,
// It is a long file, so it may take a while to read through
// Roughly, it contains these parts:
//     1. Define the nodes: switches, hosts, links, etc.
//     2. Set up the network topology, and routing
//     3. Set up the monitoring files
//     4. Set up the load balancing
//
// Hopefully, you don't need to modify this file,
// but if you need to, don't hesitate to do so.
//
// Jin Yuze, 2025-Aug Edition
//
/*-----------------------------------*/


/************************************************
 * Configuration parameters
 ***********************************************/


/*------Load balancing parameters-----*/
// All these parameters are set in the configuration file, here is just the default value
// mode for load balancing, set in config file
uint32_t lb_mode = 0; //
uint32_t ecn_mode = 0; //
uint32_t queue_mgmt_mode = 0; // mode for switch egress queue scheduling, set in config file

bool record_every_ecn = false; // whether to record per-flow ECN marking

/*------------------------ simulation variables -----------------------------*/
uint64_t one_hop_delay = 1000;  // nanoseconds
uint32_t cc_mode = 1; // mode for congestion control on NICs, 1: DCQCN
bool enable_qcn = true;
bool enable_pfc = false;
bool use_dynamic_pfc_threshold = true;

uint32_t packet_payload_size = 1000;
uint32_t l2_chunk_size = 0;
uint32_t l2_ack_interval = 0;
// double flowgen_start_time = 2.0, flowgen_stop_time = 2.5, simulator_extra_time = 0.1;
double pause_time = 5;
double simulator_stop_time = 3.01;

// Add the monitoring files
std::string output_dir = "output";
std::string config_name;
std::string data_rate, link_delay, topology_file, flow_file, trace_file, trace_output_file;
std::string ecn_count_file = "ecn_count.txt";
std::string fct_output_file = "fct.txt";
std::string flow_status_record_file = "flow_status_record.txt";
std::string uplink_mon_file = "uplink.txt";
std::string conn_mon_file = "conn.txt";
std::string flow_rate_mon_file = "flow_rate.txt";
std::string per_flow_ecn_record_file = "per_flow_ecn_record.txt";
std::string pfc_output_file = "pfc.txt";
std::string qlen_mon_file = "qlen.txt";
std::string rdma_recovery_record_file = "rdma_recovery_record.txt";
std::string switch_workload_statistics_file = "switch_workload_statistics.txt";
std::string per_host_finish_time_record_file = "per_host_finish_time_record.txt";

double alpha_resume_interval = 55, rp_timer, ewma_gain = 1 / 16;
double rate_decrease_interval = 4;
uint32_t fast_recovery_times = 5;
std::string rate_ai, rate_hai, min_rate = "100Mb/s";
std::string dctcp_rate_ai = "1000Mb/s";

bool clamp_target_rate = false, l2_back_to_zero = false;
double error_rate_per_link = 0.0;
uint32_t has_win = 1;
uint32_t global_t = 1;
uint32_t mi_thresh = 5;
bool var_win = false, fast_react = true;
bool multi_rate = true;
bool sample_feedback = false;
double pint_log_base = 1.05;
double pint_prob = 1.0;
double u_target = 0.95;
uint32_t int_multi = 1;
bool rate_bound = true;
int nic_total_pause_time = 0; // slightly less than finish time without inefficiency in us

// for uplink/Downlink monitoring at TOR switches (load balance performance)
uint64_t ToRMonitoringStart = 0; // ns
uint64_t ToRMonitoringEnd = 300000000000; // ns (300s)
uint32_t ToRMonitoringInterval = 10000; // 10micro s
uint64_t PktDropPrintInterval = 100000000; // 100ms
std::map<uint32_t, std::vector<uint32_t>> torId2UplinkIf;
std::map<uint32_t, std::vector<uint32_t>> torId2DownlinkIf;

std::string routing_plan_folder = "";
bool enable_routing_plan = false;
bool enable_dynamic_rerouting = false;

FILE *uplink_output = NULL;
FILE *conn_output = NULL;
FILE *flow_rate_output = NULL;

uint32_t ack_high_prio = 0;
uint64_t link_down_time = 0;
uint32_t link_down_A = 0, link_down_B = 0;

uint32_t enable_trace = 1;
uint32_t buffer_size = 16;

uint32_t qlen_dump_interval = 100000, qlen_mon_interval = 10000;
uint64_t qlen_mon_start = 0, qlen_mon_end = 300000000000;

unordered_map<uint64_t, uint32_t> rate2kmax, rate2kmin;
unordered_map<uint64_t, double> rate2pmax;

/************************************************
 * Runtime varibles
 ***********************************************/
struct Interface {
    uint32_t idx;
    bool up;
    uint64_t delay;
    uint64_t bw;

    Interface()
        : idx(0),
          up(false)
    {
    }
};

std::ifstream topof, flowf, tracef;

// a list of nodes, each node is a switch or a host, has a unique id (index)
NodeContainer n; 

// a list of the ipv4 addresses of the nodes
std::vector<Ipv4Address> serverAddress;

uint64_t nic_rate;
uint64_t maxRtt, maxBdp;

// maintain port number for each host pair
std::unordered_map<uint32_t, unordered_map<uint32_t, uint16_t>> portNumber;

// the map between hosts' ID to the corresponding ToR switch 
unordered_map<uint32_t, Ptr<SwitchNode>> idxNodeToR; // Id -> Ptr

// Mapping: <node, <dest, Interface>>
map<Ptr<Node>, map<Ptr<Node>, Interface>> nbr2if;

// Mapping: from each node, to any destination host, what next hops (switch) can choose
// <node, <dest, <nexthop0, ...> > >
map<Ptr<Node>, map<Ptr<Node>, vector<Ptr<Node>>>> nextHop;

// routing information: from host to host, the pairwise: delay, Tx delay, bandwidth, BDP, RTT
map<Ptr<Node>, map<Ptr<Node>, uint64_t>> pairDelay;
map<Ptr<Node>, map<Ptr<Node>, uint64_t>> pairTxDelay;
map<uint32_t, map<uint32_t, uint64_t>> pairBw;
map<Ptr<Node>, map<Ptr<Node>, uint64_t>> pairBdp;
map<uint32_t, map<uint32_t, uint64_t>> pairRtt;

struct FlowInput {
    uint32_t src, dst, pg, maxPacketCount, port, dport;
    double start_time;
    uint32_t idx;
};

FlowInput flow_input = {0};
uint32_t flow_num;

/************************************************
 * Some often used functions
 ***********************************************/

// e.g. id = 5, return 0x0b000501 (10.0.5.1)
// e.g. id = 345, return 0x0b015901 (10.1.89.1)
Ipv4Address node_id_to_ip(uint32_t id) {
    return Ipv4Address(0x0b000001 + ((id / 256) * 0x00010000) + ((id % 256) * 0x00000100));
}

// e.g. ip = 0x0b000005, return 5
// e.g. ip = 0x0b000159, return 345
uint32_t ip_to_node_id(Ipv4Address ip) {
    return (ip.Get() >> 8) & 0xffff;
}

// To constantly report the packet drop count
void print_packet_drop() {
    std::cout << "Total packet drop count: ";
    std::cout << "   At: " << Simulator::Now().GetNanoSeconds();
    std::cout << "   Ingress: " << Settings::dropped_pkt_sw_ingress;
    std::cout << "   Egress: " << Settings::dropped_pkt_sw_egress <<std::endl;
    Simulator::Schedule(
            NanoSeconds(PktDropPrintInterval), 
            &print_packet_drop);  // every 10us
}

/**
 * @brief TOR Switch monitoring
 */
void periodic_monitoring(FILE *fout_uplink, FILE *fout_conn, uint32_t *lb_mode) {

    uint32_t lb_mode_val = *lb_mode;
    uint64_t now = Simulator::Now().GetNanoSeconds();

    // For TOR Switches
    for (const auto &tor2If : torId2UplinkIf) {

        Ptr<Node> node = n.Get(tor2If.first);    // tor id
        auto swNode = DynamicCast<SwitchNode>(node);
        assert(swNode->m_isToR == true);  // sanity check
    }

    // Schedule the next monitoring
    if (Simulator::Now() < NanoSeconds(ToRMonitoringEnd)) {
        Simulator::Schedule( NanoSeconds(ToRMonitoringInterval), &periodic_monitoring, 
            fout_uplink, fout_conn, lb_mode);
    }
    return;
}

void record_ecn(FILE* ecn_count_output, NodeContainer* n) {
    uint64_t now = Simulator::Now().GetNanoSeconds();
    if (Settings::dropped_pkt_sw_egress > 0 || Settings::dropped_pkt_sw_ingress > 0 || Settings::ecn_count > 0 || Settings::pause_count > 0) {
        fprintf(
            ecn_count_output, 
            "%lu,%u,%u,%u,%u\n", 
            now, 
            Settings::dropped_pkt_sw_egress, 
            Settings::dropped_pkt_sw_ingress, 
            Settings::ecn_count, 
            Settings::pause_count);

        fflush(ecn_count_output);

        Settings::dropped_pkt_sw_egress = 0;
        Settings::dropped_pkt_sw_ingress = 0;
        Settings::ecn_count = 0;
        Settings::pause_count = 0;
    }
    Simulator::Schedule(NanoSeconds(1000000), &record_ecn, ecn_count_output, n);  // every 1ms
}

void get_pfc(FILE* fout, Ptr<QbbNetDevice> dev, uint32_t type) {
    fprintf(fout,
            "%lu %u %u %u %u\n",
            Simulator::Now().GetTimeStep(),
            dev->GetNode()->GetId(),
            dev->GetNode()->GetNodeType(),
            dev->GetIfIndex(),
            type);
}

struct QlenDistribution {
    vector<uint32_t> cnt; // cnt[i] is the number of times that the queue len is i KB

    void add(uint32_t qlen) {
        uint32_t kb = qlen / 1000;
        if (cnt.size() < kb + 1){
            cnt.resize(kb + 1);
        }
        cnt[kb]++;
    }
};

map<uint32_t, map<uint32_t, uint32_t>> queue_result;

void record_switch_workload(FILE* fout, NodeContainer* n) {
    uint64_t now = Simulator::Now().GetNanoSeconds();

    for (uint32_t i = 0; i < n->GetN(); i++) {

        if (n->Get(i)->GetNodeType() == 1) {
            Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(n->Get(i));
            
            uint64_t workload = Settings::switch_workload_statistics[i];
            uint64_t ecn_count = Settings::ecn_count_per_switch[i];
            if (workload == 0 && ecn_count == 0) {
                continue; // no workload to record
            }
            fprintf(fout, "%lu, %u, %lu, %lu", now, i, workload, ecn_count);
            for (uint32_t j = 1; j < sw->GetNDevices(); j++) {
                uint64_t ingress = Settings::switch_workload_statistics_per_port_ingress[i][j];
                uint64_t egress = Settings::switch_workload_statistics_per_port_egress[i][j];
                uint64_t ecn_count = Settings::ecn_count_per_switch_per_port[i][j];
                fprintf(fout, "| %d %lu %lu %lu", j, ingress, egress, ecn_count);
            }
            fprintf(fout, "\n");
            for (uint32_t j = 1; j < sw->GetNDevices(); j++) {
                Settings::switch_workload_statistics_per_port_ingress[i][j] = 0;
                Settings::switch_workload_statistics_per_port_egress[i][j] = 0;
                Settings::ecn_count_per_switch_per_port[i][j] = 0;
            }
            Settings::switch_workload_statistics[i] = 0;
            Settings::ecn_count_per_switch[i] = 0;
        }

    }

    fflush(fout);

    Simulator::Schedule(NanoSeconds(100000), &record_switch_workload, fout, n);  // every 100us
}

void monitor_buffer(FILE* qlen_output, NodeContainer* n) {
    for (uint32_t i = 0; i < n->GetN(); i++) {
        if (n->Get(i)->GetNodeType() == 1) { // is switch
            Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(n->Get(i));
            if (queue_result.find(i) == queue_result.end())
                queue_result[i];
            int test = 0;
            for (uint32_t j = 1; j < sw->GetNDevices(); j++) {
                uint32_t size = 0;
                for (uint32_t k = 0; k < SwitchMmu::qCnt; k++)
                    size += sw->m_mmu->egress_bytes[j][k];
                if (size >= 100) {
                    queue_result[i][j] = size;
                    if (test == 0) {
                        test = 1;
                        fprintf(qlen_output, "time %lu %u ", Simulator::Now().GetTimeStep(), i);
                    }
                    if (j < sw->GetNDevices() - 1) {
                        test = 2;
                        fprintf(qlen_output, "j %u %u ", j, size);
                    } else if (j == sw->GetNDevices() - 1) {
                        fprintf(qlen_output, "j %u %u\n", j, size);
                        test = 3;
                    }
                }
                if (j == sw->GetNDevices() - 1 && test == 2) {
                    fprintf(qlen_output, "\n");
                }
            }
            fflush(qlen_output);
        }
    }

    fflush(qlen_output);

    Simulator::Schedule(NanoSeconds(qlen_mon_interval), &monitor_buffer, qlen_output, n);
}

void CalculateRoute(Ptr<Node> host) {
    // queue for the BFS.
    vector<Ptr<Node>> q;
    // Distance from the host to each node.
    map<Ptr<Node>, int> dis;
    map<Ptr<Node>, uint64_t> delay;
    map<Ptr<Node>, uint64_t> txDelay;
    map<Ptr<Node>, uint64_t> bw;

    // init BFS.
    q.push_back(host);
    dis[host] = 0;
    delay[host] = 0;
    txDelay[host] = 0;
    bw[host] = 0xfffffffffffffffflu;
    
    // BFS.
    for (int i = 0; i < (int)q.size(); i++) {
        Ptr<Node> now = q[i];
        int d = dis[now];
        
        for (auto it = nbr2if[now].begin(); it != nbr2if[now].end(); it++) {
            
            // skip down link
            if (!it->second.up) {
                continue;
            }
            
            Ptr<Node> next = it->first;
            if (dis.find(next) == dis.end()) {
                dis[next] = d + 1;
                delay[next] = delay[now] + it->second.delay;
                txDelay[next] = txDelay[now] + packet_payload_size * 1000000000lu * 8 / it->second.bw;
                bw[next] = std::min(bw[now], it->second.bw);
                if (next->GetNodeType() == 1)
                    q.push_back(next);
            }

            if (d + 1 == dis[next]) {
                nextHop[next][host].push_back(now);
            }
        }
    }
    
    for (auto it : delay) {
        // std::cout << "pairDelay first "<< it.first->GetId() << " host " <<
        // host->GetId() << " delay " << it.second << std::endl;
        pairDelay[it.first][host] = it.second;
    }
    
    for (auto it : txDelay) {
        pairTxDelay[it.first][host] = it.second;
    }
    
    for (auto it : bw) {
        // std::cout << "pairBw first "<< it.first->GetId() << " host " <<
        // host->GetId() << " bw " << it.second << std::endl;
        pairBw[it.first->GetId()][host->GetId()] = it.second;
    }
}

void CalculateRoutes(NodeContainer& n) {
    for (int i = 0; i < (int)n.GetN(); i++) {
        Ptr<Node> node = n.Get(i);
        // For every host, calculate the route to every other node.
        // Using BFS to detect all the nodes that can be reached from the host.
        if (node->GetNodeType() == 0) {
            CalculateRoute(node);
        }
    }
}

void SetRoutingEntries() {
    
    for (auto i = nextHop.begin(); i != nextHop.end(); i++) {
        Ptr<Node> node = i->first;
        auto& table = i->second;
        
        for (auto j = table.begin(); j != table.end(); j++) {
            Ptr<Node> dst = j->first;
            Ipv4Address dstAddr = dst->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
            vector<Ptr<Node>> nexts = j->second;

            for (int k = 0; k < (int)nexts.size(); k++) {
                Ptr<Node> next = nexts[k];
                uint32_t interface = nbr2if[node][next].idx;

                if (node->GetNodeType() == 1) {
                    DynamicCast<SwitchNode>(node)->AddTableEntry(dstAddr, interface);
                } else {
                    node->GetObject<RdmaDriver>()->m_rdma->AddTableEntry(dstAddr, interface);
                }
            }
        }
    }
}

// take down the link between a and b, and redo the routing
void TakeDownLink(NodeContainer n, Ptr<Node> a, Ptr<Node> b) {
    if (!nbr2if[a][b].up){
        return;
    }
    // take down link between a and b
    nbr2if[a][b].up = nbr2if[b][a].up = false;
    nextHop.clear();
    CalculateRoutes(n);
    // clear routing tables
    for (uint32_t i = 0; i < n.GetN(); i++) {
        if (n.Get(i)->GetNodeType() == 1) {
            DynamicCast<SwitchNode>(n.Get(i))->ClearTable();
        } else {
            n.Get(i)->GetObject<RdmaDriver>()->m_rdma->ClearTable();
        }
    }
    DynamicCast<QbbNetDevice>(a->GetDevice(nbr2if[a][b].idx))->TakeDown();
    DynamicCast<QbbNetDevice>(b->GetDevice(nbr2if[b][a].idx))->TakeDown();
    // reset routing table
    SetRoutingEntries();

    // redistribute qp on each host
    for (uint32_t i = 0; i < n.GetN(); i++) {
        if (n.Get(i)->GetNodeType() == 0) {
            n.Get(i)->GetObject<RdmaDriver>()->m_rdma->RedistributeQp();
        }
    }
}

uint64_t get_nic_rate(NodeContainer& n) {
    for (uint32_t i = 0; i < n.GetN(); i++)
        if (n.Get(i)->GetNodeType() == 0)
            return DynamicCast<QbbNetDevice>(n.Get(i)->GetDevice(1))->GetDataRate().GetBitRate();
}

/************************************************
 * The function to read the configuration file
 ***********************************************/
bool ReadConf(string network_configuration) {
    // Read the configuration file
    std::ifstream conf;
    conf.open(network_configuration);
    if (!conf.is_open()) {
        std::cout << "Error: cannot find network config file: " << network_configuration << std::endl;
        fflush(stdout);
        return false;
    }

    while (!conf.eof()) {
        std::string key;
        conf >> key;
        
        if (key.compare("CONFIG_NAME") == 0) {
            std::string v;
            conf >> v;
            config_name = v;
        }
        else if (key.compare("RECORD_EVERY_ECN") == 0) {
            uint32_t v;
            conf >> v;
            record_every_ecn = v;
        }
        else if (key.compare("CUSTOMIZED_ECN") == 0) {
            uint32_t v;
            conf >> v;
            ecn_mode = v;
        }
        else if (key.compare("OUTPUT_FOLDER") == 0) {
            std::string v;
            conf >> v;
            output_dir = v;
        }else if (key.compare("PER_HOST_FINISH_TIME_RECORD_FILE") == 0) {
            std::string v;
            conf >> v;
            per_host_finish_time_record_file = v;
        }
        else if (key.compare("PER_FLOW_ECN_RECORD_FILE") == 0) {
            std::string v;
            conf >> v;
            per_flow_ecn_record_file = v;
        }
        else if (key.compare("RDMA_RECOVERY_RECORD_FILE") == 0) {
            std::string v;
            conf >> v;
            rdma_recovery_record_file = v;
        }
        else if (key.compare("FLOW_STATUS_RECORD_FILE") == 0) {
            std::string v;
            conf >> v;
            flow_status_record_file = v;
        }
        else if (key.compare("SWITCH_WORKLOAD_STATISTICS_FILE") == 0) {
            std::string v;
            conf >> v;
            switch_workload_statistics_file = v;
        }
        else if (key.compare("ECN_COUNT_FILE") == 0) {
            std::string v;
            conf >> v;
            ecn_count_file = v;
        }
        else if (key.compare("ENABLE_QCN") == 0)
        {
            uint32_t v;
            conf >> v;
            enable_qcn = v;
        }
        else if (key.compare("ENABLE_PFC") == 0)
        {
            uint32_t v;
            conf >> v;
            enable_pfc = v;
        }
        else if (key.compare("USE_DYNAMIC_PFC_THRESHOLD") == 0)
        {
            uint32_t v;
            conf >> v;
            use_dynamic_pfc_threshold = v;
        }
        else if (key.compare("CLAMP_TARGET_RATE") == 0)
        {
            uint32_t v;
            conf >> v;
            clamp_target_rate = v;
        }
        else if (key.compare("PAUSE_TIME") == 0)
        {
            double v;
            conf >> v;
            pause_time = v;
        }
        else if (key.compare("DATA_RATE") == 0)
        {
            std::string v;
            conf >> v;
            data_rate = v;
        }
        else if (key.compare("LINK_DELAY") == 0)
        {
            std::string v;
            conf >> v;
            link_delay = v;
        }
        else if (key.compare("PACKET_PAYLOAD_SIZE") == 0)
        {
            uint32_t v;
            conf >> v;
            packet_payload_size = v;
        }
        else if (key.compare("L2_CHUNK_SIZE") == 0)
        {
            uint32_t v;
            conf >> v;
            l2_chunk_size = v;
        }
        else if (key.compare("L2_ACK_INTERVAL") == 0)
        {
            uint32_t v;
            conf >> v;
            l2_ack_interval = v;
        }
        else if (key.compare("L2_BACK_TO_ZERO") == 0)
        {
            uint32_t v;
            conf >> v;
            l2_back_to_zero = v;
        }
        else if (key.compare("TOPOLOGY_FILE") == 0)
        {
            std::string v;
            conf >> v;
            topology_file = v;
        }
        else if (key.compare("FLOW_FILE") == 0)
        {
            std::string v;
            conf >> v;
            flow_file = v;
        }
        else if (key.compare("TRACE_FILE") == 0)
        {
            std::string v;
            conf >> v;
            trace_file = v;
        }
        else if (key.compare("TRACE_OUTPUT_FILE") == 0)
        {
            std::string v;
            conf >> v;
            trace_output_file = v;
        }
        else if (key.compare("SIMULATOR_STOP_TIME") == 0)
        {
            double v;
            conf >> v;
            simulator_stop_time = v;
        }
        else if (key.compare("ALPHA_RESUME_INTERVAL") == 0)
        {
            double v;
            conf >> v;
            alpha_resume_interval = v;
        }
        else if (key.compare("RP_TIMER") == 0)
        {
            double v;
            conf >> v;
            rp_timer = v;
        }
        else if (key.compare("EWMA_GAIN") == 0)
        {
            double v;
            conf >> v;
            ewma_gain = v;
        }
        else if (key.compare("FAST_RECOVERY_TIMES") == 0)
        {
            uint32_t v;
            conf >> v;
            fast_recovery_times = v;
        }
        else if (key.compare("RATE_AI") == 0)
        {
            std::string v;
            conf >> v;
            rate_ai = v;
        }
        else if (key.compare("RATE_HAI") == 0)
        {
            std::string v;
            conf >> v;
            rate_hai = v;
        }
        else if (key.compare("ERROR_RATE_PER_LINK") == 0)
        {
            double v;
            conf >> v;
            error_rate_per_link = v;
        }
        else if (key.compare("CC_MODE") == 0)
        {
            conf >> cc_mode;
        }
        else if (key.compare("LB_MODE") == 0)
        {
            conf >> lb_mode;
        }
        else if (key.compare("QUEUE_MGMT_MODE") == 0)
        {
            conf >> queue_mgmt_mode;
        }
        else if (key.compare("RATE_DECREASE_INTERVAL") == 0)
        {
            double v;
            conf >> v;
            rate_decrease_interval = v;
        }
        else if (key.compare("MIN_RATE") == 0)
        {
            conf >> min_rate;
        }
        else if (key.compare("FCT_OUTPUT_FILE") == 0)
        {
            conf >> fct_output_file;
        }
        else if (key.compare("FLOW_RATE_MON_FILE") == 0)
        {
            conf >> flow_rate_mon_file;
        }
        else if (key.compare("HAS_WIN") == 0)
        {
            conf >> has_win;
        }
        else if (key.compare("GLOBAL_T") == 0)
        {
            conf >> global_t;
        }
        else if (key.compare("MI_THRESH") == 0)
        {
            conf >> mi_thresh;
        }
        else if (key.compare("VAR_WIN") == 0)
        {
            uint32_t v;
            conf >> v;
            var_win = v;
        }
        else if (key.compare("FAST_REACT") == 0)
        {
            uint32_t v;
            conf >> v;
            fast_react = v;
        }
        else if (key.compare("U_TARGET") == 0)
        {
            conf >> u_target;
        }
        else if (key.compare("INT_MULTI") == 0)
        {
            conf >> int_multi;
        }
        else if (key.compare("RATE_BOUND") == 0)
        {
            uint32_t v;
            conf >> v;
            rate_bound = v;
        }
        else if (key.compare("ACK_HIGH_PRIO") == 0)
        {
            conf >> ack_high_prio;
        }
        else if (key.compare("DCTCP_RATE_AI") == 0)
        {
            conf >> dctcp_rate_ai;
        }
        else if (key.compare("NIC_TOTAL_PAUSE_TIME") == 0)
        {
            conf >> nic_total_pause_time;
        }
        else if (key.compare("PFC_OUTPUT_FILE") == 0)
        {
            conf >> pfc_output_file;
        }
        else if (key.compare("LINK_DOWN") == 0)
        {
            conf >> link_down_time >> link_down_A >> link_down_B;
        }
        else if (key.compare("ENABLE_TRACE") == 0)
        {
            conf >> enable_trace;
        }
        else if (key.compare("KMAX_MAP") == 0)
        {
            int n_k;
            conf >> n_k;
            for (int i = 0; i < n_k; i++)
            {
                uint64_t rate;
                uint32_t k;
                conf >> rate >> k;
                rate2kmax[rate] = k;
            }
        }
        else if (key.compare("KMIN_MAP") == 0)
        {
            int n_k;
            conf >> n_k;
            for (int i = 0; i < n_k; i++)
            {
                uint64_t rate;
                uint32_t k;
                conf >> rate >> k;
                rate2kmin[rate] = k;
            }
        }
        else if (key.compare("PMAX_MAP") == 0)
        {
            int n_k;
            conf >> n_k;
            for (int i = 0; i < n_k; i++)
            {
                uint64_t rate;
                double p;
                conf >> rate >> p;
                rate2pmax[rate] = p;
            }
        }
        else if (key.compare("BUFFER_SIZE") == 0)
        {
            conf >> buffer_size;
        }
        else if (key.compare("QLEN_MON_FILE") == 0)
        {
            conf >> qlen_mon_file;
        }
        else if (key.compare("QLEN_MON_START") == 0)
        {
            conf >> qlen_mon_start;
        }
        else if (key.compare("QLEN_MON_END") == 0)
        {
            conf >> qlen_mon_end;
        }
        else if (key.compare("MULTI_RATE") == 0)
        {
            int v;
            conf >> v;
            multi_rate = v;
        }
        else if (key.compare("SAMPLE_FEEDBACK") == 0)
        {
            int v;
            conf >> v;
            sample_feedback = v;
        } else if (key.compare("PINT_LOG_BASE") == 0) {
            conf >> pint_log_base;
        } else if (key.compare("PINT_PROB") == 0) {
            conf >> pint_prob;
        } else if (key.compare("UPLINK_MON_FILE") == 0) {
            conf >> uplink_mon_file;
        } else if (key.compare("CONN_MON_FILE") == 0) {
            conf >> conn_mon_file;
        } else if (key.compare("QLEN_MON_START") == 0) {
            conf >> qlen_mon_start;
        }
        fflush(stdout);
    }
    conf.close();
    return true;
}

void SetConfig() {
    bool dynamicth = use_dynamic_pfc_threshold;

    // DCQCN-related settings, used QBB devices
    Config::SetDefault("ns3::QbbNetDevice::PauseTime", UintegerValue(pause_time));
    Config::SetDefault("ns3::QbbNetDevice::QcnEnabled", BooleanValue(enable_pfc));
    Config::SetDefault("ns3::QbbNetDevice::DynamicThreshold", BooleanValue(dynamicth));

    if (cc_mode != 1 && lb_mode == 9) {
        std::cout << "Currently, ConWeave supports only DCQCN congestion control for RDMA. \nIf "
                     "you want to extend, the reordering delay at DstTor must be considered."
                  << std::endl;
        exit(1);
    }

    // set int_multi
    IntHop::multi = int_multi;
    // IntHeader::mode
    if (cc_mode == 7) // timely, use ts
        IntHeader::mode = IntHeader::TS;
    else if (cc_mode == 3) // hpcc, use int
        IntHeader::mode = IntHeader::NORMAL;
    else if (cc_mode == 10) // hpcc-pint
        IntHeader::mode = IntHeader::PINT;
    else // others, no extra header
        IntHeader::mode = IntHeader::NONE;

    // Set Pint
    if (cc_mode == 10)
    {
        Pint::set_log_base(pint_log_base);
        IntHeader::pint_bytes = Pint::get_n_bytes();
        printf("PINT bits: %d bytes: %d\n", Pint::get_n_bits(), Pint::get_n_bytes());
    }
}

/************************************************
 * Output file setup
 ***********************************************/
void SetupOutPuts(std::string workload_name, std::string system_name, std::string network_name, std::string topo_name, bool allow_overlapping, std::string comm_group_name = "") {
    // Just make sure the output folder is attached to the head of each out put file path
    uint32_t random_seed = RngSeedManager::GetSeed();
    
    if (output_dir != "") {
        // Align with report file: ${workload_name}_${system_name}_${topo_name}_${network_name}_${seed}.txt
        std::string blocking = allow_overlapping ? "" : "_blocking";
        // comm_group_name is only inserted when explicitly given, so single-job runs keep
        // exactly the output paths they had before multi-job support was added.
        std::string comm_group_part = comm_group_name.empty() ? "" : ("_" + comm_group_name);
        output_dir = output_dir + "/" + workload_name + "_" + system_name + "_" + topo_name + "_" + network_name + blocking + comm_group_part + "_" + std::to_string(random_seed);
        std::cout << "Output folder: " << output_dir << std::endl;
        // Create the output directory if it does not exist
        if (!fs::exists(output_dir)) {
            fs::create_directories(output_dir);
        }
        // Create the output files
        data_rate = output_dir + "/" + data_rate;
        link_delay = output_dir + "/" + link_delay;
        topology_file = output_dir + "/" + topology_file;
        flow_file = output_dir + "/" + flow_file;
        trace_file = output_dir + "/" + trace_file;
        trace_output_file = output_dir + "/" + trace_output_file;
        ecn_count_file = output_dir + "/" + ecn_count_file;
        fct_output_file = output_dir + "/" + fct_output_file;
        flow_status_record_file = output_dir + "/" + flow_status_record_file;
        uplink_mon_file = output_dir + "/" + uplink_mon_file;
        conn_mon_file = output_dir + "/" + conn_mon_file;
        flow_rate_mon_file = output_dir + "/" + flow_rate_mon_file;
        per_flow_ecn_record_file = output_dir + "/" + per_flow_ecn_record_file;
        pfc_output_file = output_dir + "/" + pfc_output_file;
        qlen_mon_file = output_dir + "/" + qlen_mon_file;
        rdma_recovery_record_file = output_dir + "/" + rdma_recovery_record_file;
        switch_workload_statistics_file = output_dir + "/" + switch_workload_statistics_file;
    }

}

/************************************************
 * The major function to setup the network
 ***********************************************/
void SetupNetwork(void (*qp_finish)(FILE*, Ptr<RdmaQueuePair>), std::string workload_name, std::string system_name, std::string network_name, std::string topo_name, bool allow_overlapping, std::string comm_group_name = "") {
    
    std::cout << "Start setting up network:" << std::endl;

    topof.open(topology_file.c_str());
    flowf.open(flow_file.c_str());
    tracef.open(trace_file.c_str());

    SetupOutPuts(workload_name, system_name, network_name, topo_name, allow_overlapping, comm_group_name);

    std::cout << "Start Creating nodes: ";

    uint32_t node_num, switch_num, link_num, trace_num;
    topof >> node_num >> switch_num >> link_num;
    flowf >> flow_num;
    tracef >> trace_num;
    std::cout << "Node num: " << node_num << " Switch num: " << switch_num << " Link num: " << link_num << std::endl;
    std::cout << "LB mode: " << lb_mode << std::endl;
    std::cout << "Packet payload size: " << packet_payload_size << std::endl;

    /*-------Sync the parameters to Settings for later usage-------*/
    Settings::node_num = node_num;
    Settings::host_num = node_num - switch_num;
    Settings::switch_num = switch_num;
    Settings::lb_mode = lb_mode;
    Settings::ecn_mode = ecn_mode;
    Settings::queue_mgmt_mode = queue_mgmt_mode;
    Settings::packet_payload = packet_payload_size;
    Settings::record_every_ecn = record_every_ecn;
    
    // A list of node types, index is the node index, 
    // value is the node type, 1 for switch, 0 for host
    // first few nodes are hosts, rest are switches
    std::vector<uint32_t> node_type(node_num, 0);
    for (uint32_t i = 0; i < switch_num; i++) {
        uint32_t sid;
        topof >> sid;
        node_type[sid] = 1;
    }

    // create the nodes:
    for (uint32_t i = 0; i < node_num; i++) {
        if (node_type[i] == 0){ // for host, just node
            n.Add(CreateObject<Node>());
        } else { // for switch, create switch node
            Ptr<SwitchNode> sw = CreateObject<SwitchNode>();
            n.Add(sw);
            sw->SetAttribute("EcnEnabled", BooleanValue(enable_qcn));
            sw->SetAttribute("PFCEnabled", BooleanValue(enable_pfc));
        }
    }

    // Install the Network Stack on the nodes
    InternetStackHelper internet;
    internet.Install(n);

    std::cout << "Finish Creating nodes, configure network stack" << std::endl;
    NS_LOG_INFO("Finish Creating nodes, Configure Network Stack");

    // Assign default IP to each server, based on the node id    
    for (uint32_t i = 0; i < node_num; i++) {
        if (n.Get(i)->GetNodeType() == 0) { // For a server
            serverAddress.resize(i + 1); // std::vector<Ipv4Address> serverAddress;
            serverAddress[i] = node_id_to_ip(i);
        }
    }

    NS_LOG_INFO("Assign IP addresses to workers (Default IP)");
    std::cout << "Assign IP addresses to workers (Default IP)" << std::endl;


    // Create the channels
    NS_LOG_INFO("Start Creating Links.");
    std::cout << "Start Creating Links." << std::endl;

    Ptr<RateErrorModel> rem = CreateObject<RateErrorModel>();
    Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
    rem->SetRandomVariable(uv);
    uv->SetStream(50);
    rem->SetAttribute("ErrorRate", DoubleValue(error_rate_per_link)); // Link Error rate
    rem->SetAttribute("ErrorUnit", StringValue("ERROR_UNIT_PACKET"));

    FILE* pfc_file = fopen(pfc_output_file.c_str(), "w");

    QbbHelper qbb;
    Ipv4AddressHelper ipv4;
    std::vector<std::pair<uint32_t, uint32_t>> link_pairs; // src, dst link pairs
    for (uint32_t i = 0; i < link_num; i++) {
        
        uint32_t src, dst;
        std::string data_rate, link_delay;
        double error_rate;

        topof >> src >> dst >> data_rate >> link_delay >> error_rate;

        link_pairs.push_back(std::make_pair(src, dst));

        Ptr<Node> snode = n.Get(src), dnode = n.Get(dst);

        qbb.SetDeviceAttribute("DataRate", StringValue(data_rate));
        qbb.SetChannelAttribute("Delay", StringValue(link_delay));

        if (error_rate > 0) {
            Ptr<RateErrorModel> rem = CreateObject<RateErrorModel>();
            Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
            rem->SetRandomVariable(uv);
            uv->SetStream(50);
            rem->SetAttribute("ErrorRate", DoubleValue(error_rate));
            rem->SetAttribute("ErrorUnit", StringValue("ERROR_UNIT_PACKET"));
            qbb.SetDeviceAttribute("ReceiveErrorModel", PointerValue(rem));
        } else {
            qbb.SetDeviceAttribute("ReceiveErrorModel", PointerValue(rem));
        }

        fflush(stdout);

        // Assign IP addresses to each host interface, this is different from the default IP
        // Note: this should be before the automatic assignment below
        // The IP address on each interface is the same
        // (ipv4.Assign(d)), because we want our IP to be the primary IP (first in
        // the IP address list), so that the global routing is based on our IP
        NetDeviceContainer d = qbb.Install(snode, dnode);
        if (snode->GetNodeType() == 0) {
            Ptr<Ipv4> ipv4 = snode->GetObject<Ipv4>();
            ipv4->AddInterface(d.Get(0));
            ipv4->AddAddress(1, Ipv4InterfaceAddress(serverAddress[src], Ipv4Mask(0xff000000)));
        }
        if (dnode->GetNodeType() == 0) {
            Ptr<Ipv4> ipv4 = dnode->GetObject<Ipv4>();
            ipv4->AddInterface(d.Get(1));
            ipv4->AddAddress(1, Ipv4InterfaceAddress(serverAddress[dst], Ipv4Mask(0xff000000)));
        }

        // used to create a graph of the topology
        // Each interface contains config information: from src to dst, 
        // 1. Interface index
        // 2. Whether the link is up
        // 3. Delay of the link
        // 4. Bandwidth of the link
        // From src to dst:
        nbr2if[snode][dnode].idx = DynamicCast<QbbNetDevice>(d.Get(0))->GetIfIndex();
        nbr2if[snode][dnode].up = true;
        nbr2if[snode][dnode].delay =
            DynamicCast<QbbChannel>(DynamicCast<QbbNetDevice>(d.Get(0))->GetChannel())
                ->GetDelay()
                .GetTimeStep();
        nbr2if[snode][dnode].bw = DynamicCast<QbbNetDevice>(d.Get(0))->GetDataRate().GetBitRate();
        // From dst to src:
        nbr2if[dnode][snode].idx = DynamicCast<QbbNetDevice>(d.Get(1))->GetIfIndex();
        nbr2if[dnode][snode].up = true;
        nbr2if[dnode][snode].delay =
            DynamicCast<QbbChannel>(DynamicCast<QbbNetDevice>(d.Get(1))->GetChannel())
                ->GetDelay()
                .GetTimeStep();
        nbr2if[dnode][snode].bw = DynamicCast<QbbNetDevice>(d.Get(1))->GetDataRate().GetBitRate();

        // This is just to set up the connectivity between host and switch. The IP addresses are useless
        if (snode->GetNodeType() == 0 || dnode->GetNodeType() == 0) {
            char ipstring[16];
            sprintf(ipstring, "10.%d.%d.0", i / 254 + 1, i % 254 + 1);
            ipv4.SetBase(ipstring, "255.255.255.0");
            ipv4.Assign(d);
        }
        
        // setup PFC trace
        DynamicCast<QbbNetDevice>(d.Get(0))->TraceConnectWithoutContext("QbbPfc",
            MakeBoundCallback(&get_pfc, pfc_file, DynamicCast<QbbNetDevice>(d.Get(0))));
        DynamicCast<QbbNetDevice>(d.Get(1))->TraceConnectWithoutContext("QbbPfc",
            MakeBoundCallback(&get_pfc, pfc_file, DynamicCast<QbbNetDevice>(d.Get(1))));
    }

    NS_LOG_INFO("Finish Creating Links.");
    std::cout << "Finish Creating Links." << std::endl;

    NS_LOG_INFO("IP - NodeID mapping");
    std::cout << "Recording IP - NodeID mapping" << std::endl;
    /* Recording the host IP address <-> NodeID pairs */
    Ipv4Address empty_ip;
    for (uint32_t i = 0; i < node_num; ++i) {
        if (n.Get(i)->GetNodeType() == 0) { // is server
            if (serverAddress[i].IsEqual(empty_ip)) {
                printf("XXX ERROR %d\n", i);
                printf("size of serverAddress: %lu", serverAddress.size());
                NS_FATAL_ERROR("An end-host belongs to no link");
            }
        }
        Settings::hostId2IpMap[i] = serverAddress[i].Get();
        Settings::hostIp2IdMap[serverAddress[i].Get()] = i;
    }

    NS_LOG_INFO("Start Setting switches.");
    std::cout << "Start Setting switches." << std::endl;
    nic_rate = get_nic_rate(n);
    // config switch
    for (uint32_t i = 0; i < node_num; i++) {
        if (n.Get(i)->GetNodeType() == 1) { // is switch

            Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(n.Get(i));
            uint32_t shift = 3; // by default 1/8

            // for each port on the switch
            // set ecn, pfc, pfc alpha, pfc headroom
            for (uint32_t j = 1; j < sw->GetNDevices(); j++) {
                Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(sw->GetDevice(j));

                // set ecn thresholds 
                uint64_t rate = dev->GetDataRate().GetBitRate();
                NS_ASSERT_MSG(rate2kmin.find(rate) != rate2kmin.end(), "must set kmin for each link speed");
                NS_ASSERT_MSG(rate2kmax.find(rate) != rate2kmax.end(), "must set kmax for each link speed");
                NS_ASSERT_MSG(rate2pmax.find(rate) != rate2pmax.end(), "must set pmax for each link speed");
                sw->m_mmu->ConfigEcn(j, rate2kmin[rate], rate2kmax[rate], rate2pmax[rate]);

                // set pfc
                uint64_t delay = DynamicCast<QbbChannel>(dev->GetChannel())->GetDelay().GetTimeStep();
                uint32_t headroom = rate * delay / 8 / 1000000000 * 3;
                sw->m_mmu->ConfigHdrm(j, headroom);
                // std::cout << "headroom: " << headroom << std::endl;
                
                // set pfc alpha, proportional to link bw
                sw->m_mmu->pfc_a_shift[j] = shift;
                while (rate > nic_rate && sw->m_mmu->pfc_a_shift[j] > 0) {
                    sw->m_mmu->pfc_a_shift[j]--;
                    rate /= 2;
                }
            }

            sw->m_mmu->ConfigNPort(sw->GetNDevices() - 1);
            sw->m_mmu->ConfigBufferSize(buffer_size * 1024 * 1024);
            sw->m_mmu->node_id = sw->GetId();
        }
    }
    NS_LOG_INFO("Finish Setting switches.");
    std::cout << "Finish Setting switches." << std::endl;

    FILE* fct_output = fopen(fct_output_file.c_str(), "w");
    NS_LOG_INFO("NOTE: RDMA is enabled.");
    std::cout << "RDMA is enabled, setting up" << std::endl;
    
    // install RDMA driver
    for (uint32_t i = 0; i < node_num; i++) {
        if (n.Get(i)->GetNodeType() == 0) { // is server
            // create RdmaHw
            Ptr<RdmaHw> rdmaHw = CreateObject<RdmaHw>();
            rdmaHw->SetAttribute("ClampTargetRate", BooleanValue(clamp_target_rate));
            rdmaHw->SetAttribute("AlphaResumInterval", DoubleValue(alpha_resume_interval));
            rdmaHw->SetAttribute("RPTimer", DoubleValue(rp_timer));
            rdmaHw->SetAttribute("FastRecoveryTimes", UintegerValue(fast_recovery_times));
            rdmaHw->SetAttribute("EwmaGain", DoubleValue(ewma_gain));
            rdmaHw->SetAttribute("RateAI", DataRateValue(DataRate(rate_ai)));
            rdmaHw->SetAttribute("RateHAI", DataRateValue(DataRate(rate_hai)));
            rdmaHw->SetAttribute("L2BackToZero", BooleanValue(l2_back_to_zero));
            rdmaHw->SetAttribute("L2ChunkSize", UintegerValue(l2_chunk_size));
            rdmaHw->SetAttribute("L2AckInterval", UintegerValue(l2_ack_interval));
            rdmaHw->SetAttribute("CcMode", UintegerValue(cc_mode));
            rdmaHw->SetAttribute("RateDecreaseInterval", DoubleValue(rate_decrease_interval));
            rdmaHw->SetAttribute("MinRate", DataRateValue(DataRate(min_rate)));
            rdmaHw->SetAttribute("Mtu", UintegerValue(packet_payload_size));
            // rdmaHw->SetAttribute("MiThresh", UintegerValue(mi_thresh));
            rdmaHw->SetAttribute("VarWin", BooleanValue(var_win));
            rdmaHw->SetAttribute("FastReact", BooleanValue(fast_react));
            // rdmaHw->SetAttribute("MultiRate", BooleanValue(multi_rate));
            // rdmaHw->SetAttribute("SampleFeedback", BooleanValue(sample_feedback));
            // rdmaHw->SetAttribute("TargetUtil", DoubleValue(u_target));
            rdmaHw->SetAttribute("RateBound", BooleanValue(rate_bound));
            // rdmaHw->SetAttribute("DctcpRateAI", DataRateValue(DataRate(dctcp_rate_ai)));
            // rdmaHw->SetPintSmplThresh(pint_prob);
            rdmaHw->SetAttribute("TotalPauseTimes", UintegerValue(nic_total_pause_time));
            // create and install RdmaDriver
            Ptr<RdmaDriver> rdma = CreateObject<RdmaDriver>();
            Ptr<Node> node = n.Get(i);
            rdma->SetNode(node);
            rdma->SetRdmaHw(rdmaHw);

            node->AggregateObject(rdma);
            rdma->Init();
            rdma->TraceConnectWithoutContext("QpComplete", MakeBoundCallback(qp_finish, fct_output));
        }
    }

    if (ack_high_prio) {
        RdmaEgressQueue::ack_q_idx = 0;
    } else {
        RdmaEgressQueue::ack_q_idx = 3;
    }

    NS_LOG_INFO("Finish setting RDMA.");
    std::cout << "Finish setting RDMA" << std::endl;

    // setup routing
    NS_LOG_INFO("Start setting routing.");
    std::cout << "Start setting routing." << std::endl;
    CalculateRoutes(n);
    SetRoutingEntries();
    NS_LOG_INFO("Finish setting routing.");
    std::cout << "Finish setting routing." << std::endl;

    // NOTE: the load balancers' copies of the routing table (m_dstIPRouting) used to be
    // filled in here, once. They are now kept in sync by SwitchNode::AddTableEntry /
    // ClearTable as the table itself is built, so they also stay correct when routes are
    // recomputed at run time (e.g. after a LINK_DOWN event). Each load balancer is told
    // which switch it lives on further below, once ToR switches have been tagged.

    // The routing information generated from the above routing calculation
    // is stored in the following data structures:
    // 1. pairDelay
    // 2. pairTxDelay
    // 3. pairBw

    // get BDP and delay
    // 4. pairBdp
    // 5. pairRtt
    // And to know all the available dst nodes from a src node, just go through something like pairDelay[src]
    NS_LOG_INFO("Calculate BDP and delay.");
    maxRtt = maxBdp = 0;
    // for all pairs of hosts, find the max BDP and delay
    for (uint32_t i = 0; i < node_num; i++) {
        if (n.Get(i)->GetNodeType() != 0) continue;

        for (uint32_t j = 0; j < node_num; j++) {
            if (n.Get(j)->GetNodeType() != 0) continue;

            uint64_t delay = pairDelay[n.Get(i)][n.Get(j)];
            uint64_t txDelay = pairTxDelay[n.Get(i)][n.Get(j)];
            uint64_t rtt = delay * 2 + txDelay;
            uint64_t bw = pairBw[i][j];
            uint64_t bdp = rtt * bw / 1000000000 / 8; // in bytes (the rtt is in ns)

            pairBdp[n.Get(i)][n.Get(j)] = bdp;
            pairRtt[i][j] = rtt;

            if (bdp > maxBdp) maxBdp = bdp;
            if (rtt > maxRtt) maxRtt = rtt;
        }
    }
    std::cout << "maxRtt=" << maxRtt << " maxBdp=" << maxBdp << std::endl;
    NS_LOG_INFO("Finish Calculating BDP and delay.");


    NS_LOG_INFO("Tag ToR Switches");
    for (auto& pair : link_pairs) {
        Ptr<Node> probably_host = n.Get(pair.first);
        Ptr<Node> probably_switch = n.Get(pair.second);

        // host-switch link, means ToR switch
        if (probably_host->GetNodeType() == 0 && probably_switch->GetNodeType() == 1) {
            
            Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(probably_switch);
            sw->m_isToR = true;

            if (idxNodeToR.find(sw->GetId()) == idxNodeToR.end()) {
                idxNodeToR[sw->GetId()] = sw;
            };

            uint32_t hostIP = serverAddress[pair.first].Get();
            Settings::hostIp2SwitchId[hostIP] = sw->GetId();
        }
    }
    NS_LOG_INFO("Tag ToR Switches Done");

    // Tell each load balancing module which switch it is running on. This has to happen
    // after ToR tagging above, otherwise m_isToR would still be false everywhere.
    // (Without this, m_switchId stays (uint32_t)-1 and m_isToR stays false in all three
    // modules, which silently breaks any student implementation that branches on them.)
    for (uint32_t i = 0; i < node_num; i++) {
        if (n.Get(i)->GetNodeType() == 1) { // is switch
            Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(n.Get(i));
            sw->m_mmu->m_placeholderLoadBalancing.SetSwitchInfo(sw->m_isToR, sw->GetId());
            sw->m_mmu->m_ecmpLoadBalancing.SetSwitchInfo(sw->m_isToR, sw->GetId());
            sw->m_mmu->m_customizedLoadBalancing.SetSwitchInfo(sw->m_isToR, sw->GetId());
        }
    }

    switch (lb_mode) {
        case 0:
            NS_LOG_INFO("Placeholder");
            std::cout<<"Load Balancing Policy " << "Placeholder"<<std::endl;
            break;
        case 1:
            NS_LOG_INFO("Placeholder");
            std::cout<<"Load Balancing Policy " << "Placeholder"<<std::endl;
            break;
        case 3:
            NS_LOG_INFO("Conga");
            std::cout<<"Load Balancing Policy " << "Conga"<<std::endl;
            break;
        case 6:
            NS_LOG_INFO("Letflow");
            std::cout<<"Load Balancing Policy " << "Letflow"<<std::endl;
            break;
        case 9:
            NS_LOG_INFO("Conweave");
            std::cout<<"Load Balancing Policy " << "Conweave"<<std::endl;
            break;
        case 2:
            NS_LOG_INFO("Customized Load Balancing");
            std::cout << "Load Balancing Policy " << "Customized"<<std::endl;
            break;
        default:
            NS_LOG_INFO("Unknown");
            std::cout<<"Load Balancing Policy " << "Unknown"<<std::endl;
            break;
    }

    // In this block, set up the information needed by the load balancer
    // if (lb_mode == 3 || lb_mode == 6 || lb_mode == 9 || lb_mode == 2) {
    if (lb_mode == 0 || lb_mode == 2) {
        // Lb mode 3, 6, 9, 2
        // Conga, Letflow, Conweave, Kwisatz Haderach
        NS_LOG_INFO("Tor-Tor Load Balancer.");
        std::cout<<"Tor-Tor Load Balancer set up proceduce"<<std::endl;
        
        // For all the switch nodes, set up the load balancer usage:
        // Conga: m_congaFromLeafTable, m_congaToLeafTable, m_congaRoutingTable
        // Letflow: m_letflowRoutingTable
        // Conweave: m_ConWeaveRoutingTable, m_rxToRId2BaseRTT
        // Kwisatz: m_kwisatzRoutingTable, m_kwisatzCongestionRecord

        // nextHop: Mapping destination to next hop for each node: <node, <dest, <nexthop0, ...> > >
        // Means from "me" to "dest", what next hop switchs I can choose 
        // map<Ptr<Node>, map<Ptr<Node>, vector<Ptr<Node>>>>

        // The setting is on each ToR switch, need to get them
        for (auto i = nextHop.begin(); i != nextHop.end(); i++) {

            // i: <Ptr<Node>, map<Ptr<Node>, vector<Ptr<Node>>>>
            if (i->first->GetNodeType() == 1) { // for every switch

                // Get the switch information
                Ptr<Node> nodeSrc = i->first;
                Ptr<SwitchNode> swSrc = DynamicCast<SwitchNode>(nodeSrc);
                uint32_t swSrcId = swSrc->GetId();

                if (swSrc->m_isToR) { // for every ToR switch
                    // The next hop table for each destination host
                    
                    auto dst_table = i->second;
                    // dst_table: map<Ptr<Node>, vector<Ptr<Node>>>
                    // contains all the destination hosts that this switch can reach

                    // For each destination host
                    for (auto j = dst_table.begin(); j != dst_table.end(); j++) {
                        // j: <Ptr<Node>, vector<Ptr<Node>>>

                        Ptr<Node> dst = j->first; // dst, Ptr<Node>
                        uint32_t dstIP = Settings::hostId2IpMap[dst->GetId()];
                        uint32_t swDstId = Settings::hostIp2SwitchId[dstIP]; // the ToR switch of the destination host
                        Ptr<SwitchNode> swDst = DynamicCast<SwitchNode>(n.Get(swDstId));

                        // if (swSrcId == swDstId) {
                        //     // Traffic only flow in the same pod
                        //     // (Will not go through the core switch)
                        //     // Then just skip it
                        //     continue;
                        // }

                        // if (lb_mode == 3) { // Conga
                        //     // initialize `m_congaFromLeafTable` and `m_congaToLeafTable`
                        //     swSrc->m_mmu->m_congaRouting.m_congaFromLeafTable[swDstId];
                        //     swSrc->m_mmu->m_congaRouting.m_congaToLeafTable[swDstId];
                        // }

                        // if (lb_mode == 9) {
                        //     // swSrc->m_mmu->m_conweaveRouting.m_conweave_llt_from_me_table[swDstId];
                        //     // swSrc->m_mmu->m_conweaveRouting.m_conweave_llt_to_me_table[swDstId];
                        // }

                        // if (lb_mode == 2) {
                        //     // initialize the `m_kwisatz_llt_from_me_table` and `m_kwisatz_llt_to_me_table`
                        //     swSrc->m_mmu->m_customizedLoadBalancing.m_kwisatz_llt_from_me_table[swDstId];
                        //     swSrc->m_mmu->m_customizedLoadBalancing.m_kwisatz_llt_to_me_table[swDstId];
                        // }

                        //////////////////////////////
                        // COUNSTRACT ROUTING paths //
                        //////////////////////////////
                        uint32_t pathId;
                        uint64_t pathBw;
                        uint8_t path_ports[4] = {0, 0, 0, 0}; // interface is always large than 0
                        // the next switches that the current ToR switch (swSrc, swSrcId) can choose
                        // to go to the destination (dst, dstIP)
                        vector<Ptr<Node>> nextSwitch = j->second; // next: vector<Ptr<Node>>

                        // For each nextHop switch our current ToR switch possibly choose
                        for (auto nextSwitchNode : nextSwitch) {
                            // nextSwitchNode: Ptr<Node>

                            // Get the port number of the ToR switch to this core switch
                            uint32_t outPort1 = nbr2if[nodeSrc][nextSwitchNode].idx;
                            uint64_t outPort1Bw = nbr2if[nodeSrc][nextSwitchNode].bw;

                            // From next-hop switch to next-next-hop switch
                            auto next2Sws = nextHop[nextSwitchNode][dst]; // vector<Ptr<Node>>

                            // next2Sws.size() == 1 means that the next switch connects to the
                            // destination host and this next-next-hop is destination ToR switch:
                            // next2Sws[0] -> GetId() == swDstId
                            if (next2Sws.size() == 1 && next2Sws[0]->GetId() == swDstId) {
                                // The next-next-hop switch is the destination's ToR switch
                                // We are two hops away from the destination ToR switch

                                // The port on next-hop switch to the destination ToR switch
                                uint32_t outPort2 = nbr2if[nextSwitchNode][next2Sws[0]].idx;
                                uint64_t outPort2Bw = nbr2if[nextSwitchNode][next2Sws[0]].bw;

                                path_ports[0] = (uint8_t)outPort1;
                                path_ports[1] = (uint8_t)outPort2;

                                pathId = *((uint32_t*)path_ports);
                                pathBw = std::min(outPort1Bw, outPort2Bw);

                                // We are inserting the pathId into the load balancer
                                // This table contains the path to the destination ToR switch

                                // if (lb_mode == 3) {
                                //     swSrc->m_mmu->m_congaRouting.m_congaRoutingTable[swDstId].insert(pathId);
                                // }

                                // if (lb_mode == 6) {
                                //     swSrc->m_mmu->m_letflowRouting.m_letflowRoutingTable[swDstId].insert(pathId);
                                // }

                                // if (lb_mode == 9) {
                                //     swSrc->m_mmu->m_conweaveRouting.m_ConWeaveRoutingTable[swDstId].insert(pathId);
                                //     // swDst->m_mmu->m_conweaveRouting.m_ConWeaveIncomingRoutingTable[swSrcId].insert(pathId);
                                //     swSrc->m_mmu->m_conweaveRouting.m_rxToRId2BaseRTT[swDstId] = one_hop_delay * 4;
                                // }

                                continue;
                            }

                            // next2Sws.size() > 1, or next2Sws.size() == 1 but next2Sws[0]->GetId()
                            // != swDstId If the next-hop switch has multiple switches towards the
                            // destination, or the next-hop switch only connects to one switch, but
                            // it is not the destination ToR switch Then we need to keep seaching
                            // for the path
                            for (auto next2SwitchNode : next2Sws) {

                                uint32_t outPort2 = nbr2if[nextSwitchNode][next2SwitchNode].idx;
                                uint64_t outPort2Bw = nbr2if[nextSwitchNode][next2SwitchNode].bw;

                                auto next3Sws = nextHop[next2SwitchNode][dst];

                                // Now we are three hops away from the destination ToR switch
                                if (next3Sws.size() == 1 && next3Sws[0]->GetId() == swDstId) {
                                    // this destination has 3-hop distance
                                    uint32_t outPort3 = nbr2if[next2SwitchNode][next3Sws[0]].idx;
                                    uint64_t outPort3Bw = nbr2if[next2SwitchNode][next3Sws[0]].bw;

                                    path_ports[0] = (uint8_t)outPort1;
                                    path_ports[1] = (uint8_t)outPort2;
                                    path_ports[2] = (uint8_t)outPort3;

                                    pathId = *((uint32_t*)path_ports);
                                    pathBw = std::min(outPort1Bw, std::min(outPort2Bw, outPort3Bw));


                                    // if (lb_mode == 3) {
                                    //     swSrc->m_mmu->m_congaRouting.m_congaRoutingTable[swDstId].insert(pathId);
                                    // }

                                    // if (lb_mode == 6) {
                                    //     swSrc->m_mmu->m_letflowRouting.m_letflowRoutingTable[swDstId].insert(pathId);
                                    // }

                                    // if (lb_mode == 9) {
                                    //     swSrc->m_mmu->m_conweaveRouting.m_ConWeaveRoutingTable[swDstId].insert(pathId);
                                    //     // swDst->m_mmu->m_conweaveRouting.m_ConWeaveIncomingRoutingTable[swSrcId].insert(pathId);
                                    //     swSrc->m_mmu->m_conweaveRouting.m_rxToRId2BaseRTT[swDstId] = one_hop_delay * 6;
                                    // }

                                    continue;
                                }

                                // If the next-hop switch has multiple switches towards the
                                // destination, We still need to keep going on searching
                                for (auto next3SwitchNode : next3Sws) {
                                    uint32_t outPort3 = nbr2if[next2SwitchNode][next3SwitchNode].idx;
                                    uint64_t outPort3Bw = nbr2if[next2SwitchNode][next3SwitchNode].bw;

                                    auto next4Sws = nextHop[next3SwitchNode][dst];

                                    if (next4Sws.size() == 1 && next4Sws[0]->GetId() == swDstId) {
                                        // this destination has 4-hop distance
                                        uint32_t outPort4 = nbr2if[next3SwitchNode][next4Sws[0]].idx;
                                        uint64_t outPort4Bw = nbr2if[next3SwitchNode][next4Sws[0]].bw;

                                        path_ports[0] = (uint8_t)outPort1;
                                        path_ports[1] = (uint8_t)outPort2;
                                        path_ports[2] = (uint8_t)outPort3;
                                        path_ports[3] = (uint8_t)outPort4;

                                        pathId = *((uint32_t*)path_ports);
                                        pathBw = std::min(outPort1Bw, std::min(outPort2Bw, std::min(outPort3Bw, outPort4Bw)));


                                        // if (lb_mode == 3) {
                                        //     swSrc->m_mmu->m_congaRouting.m_congaRoutingTable[swDstId].insert(pathId);
                                        // }

                                        // if (lb_mode == 6) {
                                        //     swSrc->m_mmu->m_letflowRouting.m_letflowRoutingTable[swDstId].insert(pathId);
                                        // }

                                        // if (lb_mode == 9) {
                                        //     swSrc->m_mmu->m_conweaveRouting.m_ConWeaveRoutingTable[swDstId].insert(pathId);
                                        //     // swDst->m_mmu->m_conweaveRouting.m_ConWeaveIncomingRoutingTable[swSrcId].insert(pathId);
                                        //     swSrc->m_mmu->m_conweaveRouting.m_rxToRId2BaseRTT[swDstId] = one_hop_delay * 8;
                                        // }

                                        continue;
                                    } else {
                                        // We just search till 4 hops, won't go further
                                        printf("Too large topology?\n");
                                        assert(false);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Only for Conga
        // m_outPort2BitRateMap
        // for (auto i = nextHop.begin(); i != nextHop.end(); i++) {  // every node
        //     if (i->first->GetNodeType() == 1) {                    // switch
        //         Ptr<Node> node = i->first;
        //         Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(node);  // switch
        //         uint32_t swId = sw->GetId();

        //         auto table = i->second;
        //         for (auto j = table.begin(); j != table.end(); j++) {
        //             Ptr<Node> dst = j->first;  // dst
        //             uint32_t dstIP = Settings::hostId2IpMap[dst->GetId()];
        //             uint32_t swDstId = Settings::hostIp2SwitchId[dstIP];

        //             for (auto next : j->second) {
        //                 uint32_t outPort = nbr2if[node][next].idx;
        //                 uint64_t bw = nbr2if[node][next].bw;
        //                 sw->m_mmu->m_congaRouting.SetLinkCapacity(outPort, bw);
        //                 // printf("Node: %d, interface: %d, bw: %lu\n", swId, outPort, bw);
        //             }
        //         }
        //     }
        // }

        // if (lb_mode == 2) {
        //     // Only for Kwisatz
        //     // m_kwisatzSwitchId2Port
        //     for (auto i = nextHop.begin(); i != nextHop.end(); i++) {  // every node
        //         if (i->first->GetNodeType() == 1) {                    // switch
        //             Ptr<Node> node = i->first;
        //             Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(node);  // switch
        //             uint32_t swId = sw->GetId();

        //             auto table = i->second;

        //             for (auto j = table.begin(); j != table.end(); j++) {
        //                 Ptr<Node> dst = j->first;  // dst
        //                 uint32_t dstIP = Settings::hostId2IpMap[dst->GetId()];
        //                 uint32_t swDstId = Settings::hostIp2SwitchId[dstIP];

        //                 for (auto next : j->second) {
        //                     uint32_t outPort = nbr2if[node][next].idx;
        //                     uint64_t bw = nbr2if[node][next].bw;
        //                     sw->m_mmu->m_kwisatzRouting.m_kwisatzSwitchId2Port[next->GetId()] = outPort;
        //                     sw->m_mmu->m_kwisatzRouting.m_kwisatzPort2SwitchId[outPort] = next->GetId();
        //                     sw->m_mmu->m_kwisatzRouting.m_kwisatzOutPort2MaxBandwidth[outPort] = bw;
        //                 }
        //             }

        //             sw->m_mmu->m_kwisatzRouting.printPort2Bandwidth();
        //             sw->m_mmu->m_kwisatzRouting.printPath2Bandwidth();
        //         }
        //     }
        // }

        // Constant setup, and switchInfo
        // for (auto i = nextHop.begin(); i != nextHop.end(); i++) {  // every node

        //     if (i->first->GetNodeType() == 1) {
        //         Ptr<Node> node = i->first;
        //         Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(node);  // switch
                
        //         // TODO: suspicious
        //         std::cout << "Switch Info - ID:" << sw->GetId() << " ToR:" <<  sw->m_isToR << std::endl;
        //         if (lb_mode == 2) {
        //             sw->m_mmu->m_kwisatzRouting.SetSwitchInfo(sw->m_isToR, sw->GetId());
        //             sw->m_mmu->m_kwisatzRouting.SetAgingTime(kwisatz_agingTime);

        //             // go through the nbr2if[sw] to get the port number
        //             for (auto connected_switch : nbr2if[sw]) {
        //                 uint32_t port = connected_switch.second.idx;
        //                 sw->m_mmu->m_kwisatzRouting.m_kwisatzSwitchId2Port[connected_switch.first->GetId()] = port;
        //                 sw->m_mmu->m_kwisatzRouting.m_kwisatzPort2SwitchId[port] = connected_switch.first->GetId();
        //             }
        //         }
        //     }
        // }

        // Only for Kwisatz
        // Need to initialize the m_kwisatz_llt_from_me_table and m_kwisatz_llt_to_me_table
        // if (lb_mode == 2) {
        //     // Do this in the routing itself
        //     // for each ToR switch, do the initialization of the link load table
        //     std::cout << "Kwisatz: Initialize Link Load Table" << std::endl;
        //     for (auto i = nextHop.begin(); i != nextHop.end(); i++) { // every node
        //         if (i->first->GetNodeType() == 1) {                   // switch
        //             Ptr<Node> node = i->first;
        //             Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(node);
        //             uint32_t swId = sw->GetId();
        //             std::cout << "Switch ID: " << swId << " ";

        //             // Check ToR switch
        //             if (sw->m_isToR) {
        //                 std::cout << "is ToR Switch ";
        //                 // For each destination host
        //                 sw->m_mmu->m_kwisatzRouting.initLinkLoadTable();
        //                 // sw->m_mmu->m_kwisatzRouting.printLLT();
        //             }
        //             std::cout << std::endl;
        //         }
        //     }

        // }

        std::cout << "Set Load Balancer Done." << std::endl;
        flush(std::cout);

        NS_LOG_INFO("Set Tor-Tor Load Balancer Done.");
        std::cout<<"Set Tor-Tor Load Balancer Done."<<std::endl;
    } else {
        NS_LOG_INFO("Not Set Tor-Tor Load Balancer.");
        std::cout<<"Not Set Tor-Tor Load Balancer."<<std::endl;
    }



    //
    // setup switch CC
    // The Conweave / Mine Load balancing algorithm should be set here on the ToR switch.
    //
    NS_LOG_INFO("Set switch CC.");

    // mode for congestion control, 
    // 1: DCQCN, 
    // 3: HPCC, 
    // 7: TIMELY, 
    // 8: DCTCP, 
    // 10: HPCC-PINT
    switch (cc_mode) {
        case 1:
            NS_LOG_INFO("DCQCN");
            std::cout<<"Congestion Control Mode " << "DCQCN"<<std::endl;
            break;
        case 3:
            NS_LOG_INFO("HPCC");
            std::cout<<"Congestion Control Mode " << "HPCC"<<std::endl;
            break;
        case 7:
            NS_LOG_INFO("TIMELY");
            std::cout<<"Congestion Control Mode " << "TIMELY"<<std::endl;
            break;
        case 8:
            NS_LOG_INFO("DCTCP");
            std::cout<<"Congestion Control Mode " << "DCTCP"<<std::endl;
            break;
        case 10:
            NS_LOG_INFO("HPCC-PINT");
            std::cout<<"Congestion Control Mode " << "HPCC-PINT"<<std::endl;
            break;
        default:
            NS_LOG_INFO("Unknown");
            std::cout<<"Congestion Control Mode " << "Unknown"<<std::endl;
            break;
    }

    for (uint32_t i = 0; i < node_num; i++)
    {
        if (n.Get(i)->GetNodeType() == 1)
        { // for all the switches

            Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(n.Get(i));

            sw->SetAttribute("CcMode", UintegerValue(cc_mode));
            sw->SetAttribute("MaxRtt", UintegerValue(maxRtt));
            sw->SetAttribute("AckHighPrio", UintegerValue(1));
        }
    }
    // NS_LOG_INFO("Finish setting switch congestion control strategy.");
    // std::cout<<"Finish setting switch congestion control strategy."<<std::endl;

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    Time interPacketInterval = Seconds(0.0000005 / 2);
    // maintain port number for each host
    for (uint32_t i = 0; i < node_num; i++) {
        if (n.Get(i)->GetNodeType() == 0) {
            for (uint32_t j = 0; j < node_num; j++) {
                if (n.Get(j)->GetNodeType() == 0) {
                    // each host pair use port number start from 10000
                    // (and increment by 1 for each time the host pair is used)
                    portNumber[i][j] = 10000;
                }
            }
        }
    }
    flow_input.idx = -1;

    topof.close();
    tracef.close();

    // schedule link down
    // NOTE: this used to be scheduled at Seconds(2) + link_down_time, inherited from
    // upstream HPCC where flow.txt-driven traffic only starts at t=2s. Under ASTRA-sim
    // the workload fires at t~0 and the simulation stops as soon as every workload is
    // done -- microbenchmarks finish in the us-ms range -- so a t=2s+ event was never
    // reached and the feature was silently dead. link_down_time is now an absolute
    // offset in microseconds from t=0.
    if (link_down_time > 0) {
        std::cout << "Link down scheduled at " << link_down_time << " us: node "
                  << link_down_A << " <-> node " << link_down_B << std::endl;
        Simulator::Schedule(MicroSeconds(link_down_time),
                            &TakeDownLink,
                            n,
                            n.Get(link_down_A),
                            n.Get(link_down_B));
    }

    if (lb_mode == 2) {
        flow_rate_output = fopen(flow_rate_mon_file.c_str(), "w");
        cout << "Flow rate monitor file: " << flow_rate_mon_file << endl;
    }

    uplink_output = fopen(uplink_mon_file.c_str(), "w");  // common
    conn_output = fopen(conn_mon_file.c_str(), "w");      // common

    // update torId2UplinkIf, torId2DownlinkIf
    for (size_t ToRId = 0; ToRId < Settings::node_num; ToRId++) {
        
        Ptr<Node> node = n.Get(ToRId);

        if (node->GetNodeType() == 1) {  // switches

            auto swNode = DynamicCast<SwitchNode>(n.Get(ToRId));

            if (swNode->m_isToR) {  // TOR switch

                for (auto &nextNodeIf : nbr2if[node]) {
                    
                    if (nextNodeIf.first->GetNodeType() == 1) {  
                        // nextNode is switch (i.e., uplink)
                        auto &vec = torId2UplinkIf[ToRId];
                        // record this uplink port (outDev index)
                        vec.push_back(nextNodeIf.second.idx);
                    } else {
                        // nextNode is server (i.e., uplink)
                        auto &vec = torId2DownlinkIf[ToRId];
                        // record this downlink port (outDev index)
                        vec.push_back(nextNodeIf.second.idx);
                    }
                }
            }
        }
    }

    Simulator::Schedule(
        NanoSeconds(ToRMonitoringStart), 
        &periodic_monitoring, 
        uplink_output, conn_output, &lb_mode);

    std::cout << "Start qlen Monitoring" << std::endl;
    // schedule buffer monitor
    FILE* qlen_output = fopen(qlen_mon_file.c_str(), "w");
    Simulator::Schedule(NanoSeconds(qlen_mon_start), &monitor_buffer, qlen_output, &n);

    std::cout << "Start ECN Monitoring" << std::endl;
    FILE* ecn_count_output = fopen(ecn_count_file.c_str(), "w");
    Simulator::Schedule(NanoSeconds(qlen_mon_start), &record_ecn, ecn_count_output, &n);

    std::cout << "Start Switch workload monitoring" << std::endl;
    FILE* switch_workload_recording = fopen(switch_workload_statistics_file.c_str(), "w");
    Simulator::Schedule(NanoSeconds(qlen_mon_start), &record_switch_workload, switch_workload_recording, &n);

    // Settings::rerouting_record_output = fopen(reroute_record_file.c_str(), "w");
    Settings::rdma_recovery_record_output = fopen(rdma_recovery_record_file.c_str(), "w");

    std::cout << "Start per flow ECN recording" << std::endl;
    Settings::per_flow_ecn_record_output = fopen(per_flow_ecn_record_file.c_str(), "w");
    
    std::cout << "Finish setting up network" << std::endl;
    return;
}
