/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/load-balancing-ecmp.h"

#include "ns3/assert.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE("ECMPLoadBalancing");

namespace ns3 {

ECMPLoadBalancing::ECMPLoadBalancing()
    : m_isToR(false), m_switchId(static_cast<uint32_t>(-1)) {}

TypeId ECMPLoadBalancing::GetTypeId(void) {
    static TypeId tid = TypeId("ns3::ECMPLoadBalancing")
                            .SetParent<Object>()
                            .AddConstructor<ECMPLoadBalancing>();
    return tid;
}

void ECMPLoadBalancing::SetSwitchSendCallback(SwitchSendCallback switchSendCallback) {
    m_switchSendCallback = switchSendCallback;
}

void ECMPLoadBalancing::SetSwitchInfo(bool isToR, uint32_t switch_id) {
    m_isToR = isToR;
    m_switchId = switch_id;
}

void ECMPLoadBalancing::DoSwitchSend(Ptr<Packet> p,
                                      CustomHeader& ch,
                                      uint32_t outDev,
                                      uint32_t qIndex) {
    m_switchSendCallback(p, ch, outDev, qIndex);
}

uint32_t ECMPLoadBalancing::GetQueueIndex(const CustomHeader& ch) const {
    if (ch.l3Prot == 0xFF || ch.l3Prot == 0xFE || ch.l3Prot == 0xFD || ch.l3Prot == 0xFC) {
        return 0;  // QCN, PFC, NACK, and ACK have highest priority.
    }
    return ch.l3Prot == 0x06 ? 1 : ch.udp.pg;
}

void ECMPLoadBalancing::RouteInput(Ptr<Packet> p, CustomHeader ch) {
    auto entry = m_dstIPRouting.find(ch.dip);
    NS_ASSERT_MSG(entry != m_dstIPRouting.end(),
                  "No route from switch " << m_switchId << " to destination " << ch.dip);
    NS_ASSERT_MSG(!entry->second.empty(),
                  "No equal-cost next hop from switch " << m_switchId << " to destination " << ch.dip);

    // Implement ECMP by hashing the 5-tuple (SIP, DIP, Protocol, SPORT, DPORT).
    uint64_t hash = 17;
    hash = hash * 31 + ch.sip;
    hash = hash * 31 + ch.dip;
    hash = hash * 31 + ch.l3Prot;
    
    // Extract ports based on protocol type. 0x06 is TCP.
    // Others typically use the UDP struct in the union for ports.
    if (ch.l3Prot == 0x06) {
        hash = hash * 31 + ch.tcp.sport;
        hash = hash * 31 + ch.tcp.dport;
    } else {
        hash = hash * 31 + ch.udp.sport;
        hash = hash * 31 + ch.udp.dport;
    }

    uint32_t path_index = hash % entry->second.size();
    DoSwitchSend(p, ch, entry->second[path_index], GetQueueIndex(ch));
}

}  // namespace ns3
