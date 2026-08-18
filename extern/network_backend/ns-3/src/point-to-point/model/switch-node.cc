#include "switch-node.h"
#include <cmath>
#include "assert.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/packet.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-header.h"
#include "qbb-net-device.h"
#include "ppp-header.h"
#include "ns3/pause-header.h"
#include "ns3/flow-id-tag.h"
#include "ns3/int-header.h"
#include "ns3/simulator.h"
#include "ns3/settings.h"

namespace ns3 {

TypeId SwitchNode::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::SwitchNode")
    .SetParent<Node> ()
    .AddConstructor<SwitchNode> ()
	.AddAttribute("EcnEnabled",
			"Enable ECN marking.",
			BooleanValue(false),
			MakeBooleanAccessor(&SwitchNode::m_ecnEnabled),
			MakeBooleanChecker())
    .AddAttribute("PFCEnabled",
			"Enable PFC pausing.",
			BooleanValue(false),
			MakeBooleanAccessor(&SwitchNode::m_pfcEnabled),
			MakeBooleanChecker())
	.AddAttribute("CcMode",
			"CC mode.",
			UintegerValue(0),
			MakeUintegerAccessor(&SwitchNode::m_ccMode),
			MakeUintegerChecker<uint32_t>())
	.AddAttribute("AckHighPrio",
			"Set high priority for ACK/NACK or not",
			UintegerValue(0),
			MakeUintegerAccessor(&SwitchNode::m_ackHighPrio),
			MakeUintegerChecker<uint32_t>())
	.AddAttribute("MaxRtt",
			"Max Rtt of the network",
			UintegerValue(9000),
			MakeUintegerAccessor(&SwitchNode::m_maxRtt),
			MakeUintegerChecker<uint32_t>())
  ;
  return tid;
}

SwitchNode::SwitchNode(){
    m_isToR = false;
    m_node_type = 1;
    m_isToR = false;
    m_mmu = CreateObject<SwitchMmu>();

    // Every load-balancing policy uses the same switch admission/send path.
    m_mmu->m_placeholderLoadBalancing.SetSwitchSendCallback(
        MakeCallback(&SwitchNode::DoSwitchSend, this));

    m_mmu->m_ecmpLoadBalancing.SetSwitchSendCallback(
        MakeCallback(&SwitchNode::DoSwitchSend, this));

    // Customized Load Balancing's Callback for switch functions
    m_mmu->m_customizedLoadBalancing.SetSwitchSendCallback(
        MakeCallback(&SwitchNode::DoSwitchSend, this));

    for (uint32_t i = 0; i < pCnt; i++)
        m_txBytes[i] = 0;

	for (uint32_t i = 0; i < pCnt; i++)
		m_lastPktSize[i] = m_lastPktTs[i] = 0;

	for (uint32_t i = 0; i < pCnt; i++)
		m_u[i] = 0;
}


void SwitchNode::CheckAndSendPfc(uint32_t inDev, uint32_t qIndex){
	
	Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);

	if (m_mmu->CheckShouldPause(inDev, qIndex)){
		device->SendPfc(qIndex, 0);
		m_mmu->SetPause(inDev, qIndex);
        std::cout << "SwitchNode::CheckAndSendPfc: Pause sent on Switch: " << m_id 
                  << " for device: " << inDev << " qIndex: " << qIndex 
                  << " at time: " << Simulator::Now().GetNanoSeconds() << std::endl;
	}
}

void SwitchNode::CheckAndSendResume(uint32_t inDev, uint32_t qIndex){
	Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
	if (m_mmu->CheckShouldResume(inDev, qIndex)){
		device->SendPfc(qIndex, 1);
		m_mmu->SetResume(inDev, qIndex);
	}
}

/********************************************
 *              MAIN LOGICS                 *
 *******************************************/
// This part is about high level processing logic of the switch
// When a packet is received from a device (qbb-net-device), it is processed here
// We check the load balancing mode and call the corresponding function
// We get the routing decision, about which port to send the packet to
// We then call the switchSend function of the device to send the packet to the port

// This function can only be called in switch mode
bool SwitchNode::SwitchReceiveFromDevice(Ptr<NetDevice> device, Ptr<Packet> packet, CustomHeader &ch){
	SendToDev(packet, ch);
	return true;
}

void SwitchNode::SendToDev(Ptr<Packet>p, CustomHeader &ch){

	/* forward the packets to the MMU for load balancing */

    if (Settings::lb_mode == 0) {
        m_mmu->m_placeholderLoadBalancing.RouteInput(p, ch);
        return;
    } else if (Settings::lb_mode == 1) {
        m_mmu->m_ecmpLoadBalancing.RouteInput(p, ch);
        return;
    } else if (Settings::lb_mode == 2) {
        m_mmu->m_customizedLoadBalancing.RouteInput(p, ch);
        return;
    }

    std::cout << "Error: Unknown Load Balancing Scheme!" << std::endl;
    exit(1); // Key error, exit
}

// This function is called when the packet is ready to be sent out
// It can be called from the MMU (and the load balancing modules) with a decision
void SwitchNode::DoSwitchSend(Ptr<Packet> p, CustomHeader &ch, uint32_t outDev, uint32_t qIndex) {
    // admission control
    FlowIdTag t;
    p->PeekPacketTag(t);

    uint32_t inDev = t.GetFlowId();

    if (qIndex != 0) {  // not highest priority
        if (m_mmu->CheckEgressAdmission(outDev, qIndex, p->GetSize())) {  // Egress Admission control
            if (m_mmu->CheckIngressAdmission(inDev, qIndex, p->GetSize())) {  // Ingress Admission control
                m_mmu->UpdateIngressAdmission(inDev, qIndex, p->GetSize());
                m_mmu->UpdateEgressAdmission(outDev, qIndex, p->GetSize());
            } else { /** DROP: At Ingress */
                // /** NOTE: logging dropped pkts */
                // std::cout << "LostPkt ingress - Sw(" << m_id << ")," << PARSE_FIVE_TUPLE(ch)
                //           << "L3Prot:" << ch.l3Prot
                //           << ",Size:" << p->GetSize()
                //           << ",At " << Simulator::Now() << std::endl;
                Settings::dropped_pkt_sw_ingress++;
                return;  // drop
            }
        } else { /** DROP: At Egress */
            // /** NOTE: logging dropped pkts */
            // std::cout << "LostPkt egress - Sw(" << m_id << ")," << PARSE_FIVE_TUPLE(ch)
            //           << "L3Prot:" << ch.l3Prot << ",Size:" << p->GetSize() << ",At "
            //           << Simulator::Now() << std::endl;
            Settings::dropped_pkt_sw_egress++;
            return;  // drop
        }

        if (m_pfcEnabled) {
            CheckAndSendPfc(inDev, qIndex);
        }
    }

    m_devices[outDev]->SwitchSend(qIndex, p, ch);
}

// NOTE: the three load balancing modules each keep their own copy of the routing
// table (m_dstIPRouting). These two functions are the ONLY writers of m_rtTable,
// so keeping the copies in sync here makes it structurally impossible for them to
// go stale -- which matters when routes are recomputed at run time, e.g. after a
// LINK_DOWN event calls ClearTable() + SetRoutingEntries() again.
void SwitchNode::AddTableEntry(Ipv4Address &dstAddr, uint32_t intf_idx){
	uint32_t dip = dstAddr.Get();
	m_rtTable[dip].push_back(intf_idx);

	m_mmu->m_placeholderLoadBalancing.m_dstIPRouting[dip].push_back(intf_idx);
	m_mmu->m_ecmpLoadBalancing.m_dstIPRouting[dip].push_back(intf_idx);
	m_mmu->m_customizedLoadBalancing.m_dstIPRouting[dip].push_back(intf_idx);
}

void SwitchNode::ClearTable(){
	m_rtTable.clear();

	m_mmu->m_placeholderLoadBalancing.m_dstIPRouting.clear();
	m_mmu->m_ecmpLoadBalancing.m_dstIPRouting.clear();
	m_mmu->m_customizedLoadBalancing.m_dstIPRouting.clear();
}

void SwitchNode::SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p){
	FlowIdTag t;
	p->PeekPacketTag(t);

	if (qIndex != 0){

		uint32_t inDev = t.GetFlowId();

		if (inDev != Settings::CONWEAVE_CTRL_DUMMY_INDEV) {
            // NOTE: ConWeave's probe/reply does not need to pass inDev interface,
            // so skip for conweave's queued packets
            m_mmu->RemoveFromIngressAdmission(inDev, qIndex, p->GetSize());
        }
        m_mmu->RemoveFromEgressAdmission(ifIndex, qIndex, p->GetSize());
        
		if (m_ecnEnabled){
            
            bool egressCongested;

            if (Settings::ecn_mode == 0) { // ECN based on queue length (DCQCN)
                egressCongested = m_mmu->ShouldSendECN(ifIndex, qIndex);
            } else if (Settings::ecn_mode == 1) { // customized ECN (currently same as DCQCN, change if you want to)
                egressCongested = m_mmu->ShouldSendECN_Customized(ifIndex, qIndex);
            } else {
                std::cout << "Error: Unknown ECN marking scheme!" << std::endl;
                exit(1); // Key error, exit
            }

            if (egressCongested){
                PppHeader ppp;
                Ipv4Header h;
                p->RemoveHeader(ppp);
                p->RemoveHeader(h);
                h.SetEcn((Ipv4Header::EcnType)0x03);
                p->AddHeader(h);
                p->AddHeader(ppp);

                // Statistics for ECN distribution
                Settings::ecn_count_per_switch[m_id]++;
                Settings::ecn_count_per_switch_per_port[m_id][ifIndex]++;

                if (Settings::record_every_ecn) {
                    // Print info for debugging: want to know the packets': src IP, dst IP, src Port, dst Port, 
                    // and the time that ECN is marked
                    CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
                    p->PeekHeader(ch);

                    fprintf(Settings::per_flow_ecn_record_output, "%llu, %u, %u, %u, %u, %u, %u, %u, %u\n",
                        Simulator::Now().GetNanoSeconds(), m_id, Settings::hostIp2IdMap[ch.sip], Settings::hostIp2IdMap[ch.dip],
                        ch.udp.sport, ch.udp.dport,
                            ifIndex, qIndex, p->GetSize());
                    
                    fflush(Settings::per_flow_ecn_record_output);
                }
            }
		}
	}

	// HPCC monitoring part
    // 
	// Removed, not covering HPCC

	m_txBytes[ifIndex] += p->GetSize();
	m_lastPktSize[ifIndex] = p->GetSize();
	m_lastPktTs[ifIndex] = Simulator::Now().GetTimeStep();

    Settings::switch_workload_statistics_per_port_egress[GetId()][ifIndex] += p->GetSize();
}

uint64_t SwitchNode::GetTxBytesOutDev(uint32_t outdev) {
    assert(outdev < pCnt);
    return m_txBytes[outdev];
}

} /* namespace ns3 */
