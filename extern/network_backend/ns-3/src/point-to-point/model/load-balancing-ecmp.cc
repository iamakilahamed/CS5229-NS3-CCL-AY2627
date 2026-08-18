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

    // Intentionally pathological: every flow for this destination uses the
    // first installed equal-cost path. Students replace this with ECMP.
    DoSwitchSend(p, ch, entry->second.front(), GetQueueIndex(ch));
}

}  // namespace ns3
