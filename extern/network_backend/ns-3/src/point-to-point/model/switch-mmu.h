#ifndef SWITCH_MMU_H
#define SWITCH_MMU_H

#include <list>
#include <unordered_map>

#include <ns3/node.h>
#include <ns3/random-variable-stream.h>

// #include "ns3/conga-routing.h"
// #include "ns3/conweave-routing.h"
// #include "ns3/letflow-routing.h"
#include "ns3/load-balancing-customized.h"
#include "ns3/load-balancing-ecmp.h"
#include "ns3/load-balancing-placeholder.h"
#include "ns3/broadcom-egress-queue.h"
#include "ns3/settings.h"

namespace ns3 {

class Packet;

class SwitchMmu: public Object{

	public:
		static TypeId GetTypeId (void);
		SwitchMmu(void);
		
		void InitSwitch(void);

		static const uint32_t pCnt = 1025;	// Number of ports used
		static const uint32_t qCnt = 8;	// Number of queues/priorities used
		static const unsigned MTU = 1048;  // 1000 + headers

		// config
		uint32_t node_id;
		uint32_t buffer_size;
		uint32_t pfc_a_shift[pCnt];
		uint32_t reserve;
		uint32_t headroom[pCnt];
		uint32_t resume_offset;
		uint32_t total_hdrm;
		uint32_t total_rsrv;

		// ECN parameters
		uint32_t kmin[pCnt], kmax[pCnt];
		double pmax[pCnt];

		// runtime
		uint32_t shared_used_bytes;
		uint32_t hdrm_bytes[pCnt][qCnt];
		uint32_t ingress_bytes[pCnt][qCnt];
		uint32_t paused[pCnt][qCnt];
		uint32_t egress_bytes[pCnt][qCnt];

		// Admission control, packet accept or drop is based on this
		bool CheckIngressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize);
		bool CheckEgressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize);
		void UpdateIngressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize);
		void UpdateEgressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize);
		void RemoveFromIngressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize);
		void RemoveFromEgressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize);

		// PFC functions, not included in this module, you can ignore or maybe you can use it
		// They are turned off by default
		bool CheckShouldPause(uint32_t port, uint32_t qIndex);
		bool CheckShouldResume(uint32_t port, uint32_t qIndex);
		void SetPause(uint32_t port, uint32_t qIndex);
		void SetResume(uint32_t port, uint32_t qIndex);
		uint32_t GetPfcThreshold(uint32_t port);
		uint32_t GetSharedUsed(uint32_t port, uint32_t qIndex);

		// The default ECN
		bool ShouldSendECN(uint32_t ifindex, uint32_t qIndex);
		
		// The customized ECN
		bool ShouldSendECN_Customized(uint32_t ifindex, uint32_t qIndex);
		
		void ConfigEcn(uint32_t port, uint32_t _kmin, uint32_t _kmax, double _pmax);
		void ConfigHdrm(uint32_t port, uint32_t size);
		void ConfigNPort(uint32_t n_port);
		void ConfigBufferSize(uint32_t size);

		uint128_t GetFlowKey(uint32_t ip1, uint32_t ip2, uint16_t port1, uint16_t port2);
		std::tuple<uint32_t, uint32_t, uint16_t, uint16_t> GetFlowTuple(uint128_t flowkey);

		/*------------ Load Balancing Objects-------------*/
		PlaceholderLoadBalancing m_placeholderLoadBalancing;
		ECMPLoadBalancing m_ecmpLoadBalancing;

		/*------------ Customized Load Balancing Objects-------------*/
		CustomizedLoadBalancing m_customizedLoadBalancing;
};

} /* namespace ns3 */

#endif /* SWITCH_MMU_H */
