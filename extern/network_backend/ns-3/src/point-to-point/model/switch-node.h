#ifndef SWITCH_NODE_H
#define SWITCH_NODE_H

#include <ns3/node.h>

#include <unordered_map>
#include <unordered_set>

#include "qbb-net-device.h"
#include "switch-mmu.h"

#include "pint.h"

namespace ns3 {

class Packet;

class SwitchNode : public Node{
		
	static const uint32_t pCnt = 1025;	// Number of ports used
	static const uint32_t qCnt = 8;	// Number of queues/priorities used
	
	uint64_t m_txBytes[pCnt]; // counter of tx bytes
	uint32_t m_lastPktSize[pCnt];
	uint64_t m_lastPktTs[pCnt]; // ns
	double m_u[pCnt];

	protected:
		bool m_ecnEnabled;
		bool m_pfcEnabled; // PFC, not included in this module, you can ignore or maybe you can use it
		uint32_t m_ccMode;
		uint64_t m_maxRtt;

		uint32_t m_ackHighPrio; // set high priority for ACK/NACK

	public:
		static TypeId GetTypeId (void);
		SwitchNode();

		Ptr<SwitchMmu> m_mmu;
		bool m_isToR;                                 // true if ToR switch

		// map from ip address (u32) to the output interface index vector
		// This is simplified routing table
		std::unordered_map<uint32_t, std::vector<uint32_t>> m_rtTable; 
		void AddTableEntry(Ipv4Address &dstAddr, uint32_t intf_idx);
		void ClearTable();

		bool SwitchReceiveFromDevice(Ptr<NetDevice> device, Ptr<Packet> packet, CustomHeader &ch);
		void SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p);

		uint64_t GetTxBytesOutDev(uint32_t outdev);

	private:
		int GetOutDev(Ptr<Packet>, CustomHeader &ch);
		void SendToDev(Ptr<Packet>p, CustomHeader &ch);
		void CheckAndSendPfc(uint32_t inDev, uint32_t qIndex);
		void CheckAndSendResume(uint32_t inDev, uint32_t qIndex);

		/* Sending packet to Egress port */
		void DoSwitchSend(Ptr<Packet> p, CustomHeader &ch, uint32_t outDev, uint32_t qIndex);
};

} /* namespace ns3 */

#endif /* SWITCH_NODE_H */
