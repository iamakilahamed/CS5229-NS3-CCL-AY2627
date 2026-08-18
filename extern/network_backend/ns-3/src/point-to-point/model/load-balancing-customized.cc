/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2023 NUS
 *
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
 *
 * Authors: Jin Yuze <jinyuze@comp.nus.edu.sg>
 */

#include "ns3/load-balancing-customized.h"

#include "ns3/assert.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE("CustomizedLoadBalancing");

namespace ns3 {

CustomizedLoadBalancing::CustomizedLoadBalancing() {
    m_isToR = false;
    m_switchId = static_cast<uint32_t>(-1);
}

TypeId CustomizedLoadBalancing::GetTypeId(void) {
    static TypeId tid =
        TypeId("ns3::CustomizedLoadBalancing")
            .SetParent<Object>()
            .AddConstructor<CustomizedLoadBalancing>();

    return tid;
}

void CustomizedLoadBalancing::DoSwitchSend(Ptr<Packet> p, CustomHeader& ch, uint32_t outDev, uint32_t qIndex) {
    m_switchSendCallback(p, ch, outDev, qIndex);
}

void CustomizedLoadBalancing::SetSwitchSendCallback(SwitchSendCallback switchSendCallback) {
    m_switchSendCallback = switchSendCallback;
}

void CustomizedLoadBalancing::SetSwitchInfo(bool isToR, uint32_t switch_id) {
    m_isToR = isToR;
    m_switchId = switch_id;
}

uint32_t CustomizedLoadBalancing::GetQueueIndex(const CustomHeader& ch) const {
    if (ch.l3Prot == 0xFF || ch.l3Prot == 0xFE || ch.l3Prot == 0xFD || ch.l3Prot == 0xFC) {
        return 0;
    }
    return ch.l3Prot == 0x06 ? 1 : ch.udp.pg;
}

void CustomizedLoadBalancing::RouteInput(Ptr<Packet> p, CustomHeader ch) {
    auto entry = m_dstIPRouting.find(ch.dip);
    NS_ASSERT_MSG(entry != m_dstIPRouting.end(),
                  "No route from switch " << m_switchId << " to destination " << ch.dip);
    NS_ASSERT_MSG(!entry->second.empty(),
                  "No equal-cost next hop from switch " << m_switchId << " to destination " << ch.dip);

    // Intentionally pathological: every flow for this destination uses the
    // first installed equal-cost path.
    DoSwitchSend(p, ch, entry->second.front(), GetQueueIndex(ch));
}

}  // namespace ns3
