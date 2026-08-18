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


#pragma once

#include <unordered_map>
#include <vector>

#include "ns3/callback.h"
#include "ns3/custom-header.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"

namespace ns3 {

/**
 * Deliberately weak routing policy used as the LB_MODE=2 starting point.
 *
 * For every destination, it always selects the first equal-cost next-hop
 * installed in m_dstIPRouting. It preserves normal packet priorities, but
 * deliberately performs no load balancing.
 */
class CustomizedLoadBalancing : public Object {
    friend class SwitchMmu;
    friend class SwitchNode;

   public:
    CustomizedLoadBalancing();

    static TypeId GetTypeId(void);

    void RouteInput(Ptr<Packet> p, CustomHeader ch);

    // Destination IP -> equal-cost output interfaces, installed during setup.
    std::unordered_map<uint32_t, std::vector<uint32_t>> m_dstIPRouting;

    void SetSwitchInfo(bool isToR, uint32_t switch_id);

    typedef Callback<void, Ptr<Packet>, CustomHeader&, uint32_t, uint32_t> SwitchSendCallback;
    void SetSwitchSendCallback(SwitchSendCallback switchSendCallback);

   private:
    void DoSwitchSend(Ptr<Packet> p, CustomHeader& ch, uint32_t outDev, uint32_t qIndex);
    uint32_t GetQueueIndex(const CustomHeader& ch) const;

    SwitchSendCallback m_switchSendCallback;
    bool m_isToR;
    uint32_t m_switchId;
};

}  // namespace ns3
