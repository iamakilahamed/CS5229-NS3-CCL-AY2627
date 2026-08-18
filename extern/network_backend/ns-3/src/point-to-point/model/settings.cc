#include "ns3/settings.h"
#include "ns3/random-variable-stream.h"
#include "ns3/random-variable.h"

namespace ns3 {

/* helper function */
Ipv4Address Settings::node_id_to_ip(uint32_t id) {
    return Ipv4Address(0x0b000001 + ((id / 256) * 0x00010000) + ((id % 256) * 0x00000100));
}
uint32_t Settings::ip_to_node_id(Ipv4Address ip) {
    return (ip.Get() >> 8) & 0xffff;
}

/* others */
uint32_t Settings::lb_mode = 0;
uint32_t Settings::ecn_mode = 0;
uint32_t Settings::queue_mgmt_mode = 0;

std::map<uint32_t, uint32_t> Settings::hostIp2IdMap;
std::map<uint32_t, uint32_t> Settings::hostId2IpMap;

/* statistics */
uint32_t Settings::node_num = 0;
uint32_t Settings::host_num = 0;
uint32_t Settings::switch_num = 0;
uint64_t Settings::cnt_finished_flows = 0;
uint32_t Settings::packet_payload = 1000;

uint32_t Settings::dropped_pkt_sw_ingress = 0;
uint32_t Settings::dropped_pkt_sw_egress = 0;

uint32_t Settings::ecn_count = 0;
uint32_t Settings::pause_count = 0;
bool Settings::record_every_ecn = false;

/* for load balancer */
std::map<uint32_t, uint32_t> Settings::hostIp2SwitchId;

std::map<uint32_t, uint64_t> Settings::switch_workload_statistics;
std::map<uint32_t, uint64_t> Settings::ecn_count_per_switch;
std::map<uint32_t, std::map<uint32_t, uint64_t>> Settings::switch_workload_statistics_per_port_ingress;
std::map<uint32_t, std::map<uint32_t, uint64_t>> Settings::switch_workload_statistics_per_port_egress;
std::map<uint32_t, std::map<uint32_t, uint64_t>> Settings::ecn_count_per_switch_per_port;

FILE *Settings::per_flow_ecn_record_output = nullptr;
FILE *Settings::rdma_recovery_record_output = nullptr;
}  // namespace ns3
