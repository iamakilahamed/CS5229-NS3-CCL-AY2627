#include <ns3/simulator.h>
#include <ns3/seq-ts-header.h>
#include <ns3/udp-header.h>
#include <ns3/ipv4-header.h>
#include "ns3/ppp-header.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/data-rate.h"
#include "ns3/pointer.h"
#include "rdma-hw.h"
#include "ppp-header.h"
#include "qbb-header.h"
#include "cn-header.h"
#include "assert.h"
#include "ns3/settings.h"

namespace ns3{

// std::unordered_map<unsigned, unsigned> acc_timeout_count;

TypeId RdmaHw::GetTypeId (void)
{
	static TypeId tid = TypeId ("ns3::RdmaHw")
		.SetParent<Object> ()
		.AddAttribute("MinRate",
				"Minimum rate of a throttled flow",
				DataRateValue(DataRate("100Mb/s")),
				MakeDataRateAccessor(&RdmaHw::m_minRate),
				MakeDataRateChecker())
		.AddAttribute("Mtu",
				"Mtu.",
				UintegerValue(1000),
				MakeUintegerAccessor(&RdmaHw::m_mtu),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute ("CcMode",
				"which mode of DCQCN is running",
				UintegerValue(0),
				MakeUintegerAccessor(&RdmaHw::m_cc_mode),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute("NACK_Generation_Interval",
				"The NACK Generation interval",
				DoubleValue(500.0),
				MakeDoubleAccessor(&RdmaHw::m_nack_interval),
				MakeDoubleChecker<double>())
		.AddAttribute("L2ChunkSize",
				"Layer 2 chunk size. Disable chunk mode if equals to 0.",
				UintegerValue(0),
				MakeUintegerAccessor(&RdmaHw::m_chunk),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute("L2AckInterval",
				"Layer 2 Ack intervals. Disable ack if equals to 0.",
				UintegerValue(0),
				MakeUintegerAccessor(&RdmaHw::m_ack_interval),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute("L2BackToZero",
				"Layer 2 go back to zero transmission.",
				BooleanValue(false),
				MakeBooleanAccessor(&RdmaHw::m_backto0),
				MakeBooleanChecker())
		.AddAttribute("EwmaGain",
				"Control gain parameter which determines the level of rate decrease",
				DoubleValue(1.0 / 16),
				MakeDoubleAccessor(&RdmaHw::m_g),
				MakeDoubleChecker<double>())
		.AddAttribute ("RateOnFirstCnp",
				"the fraction of rate on first CNP",
				DoubleValue(1.0),
				MakeDoubleAccessor(&RdmaHw::m_rateOnFirstCNP),
				MakeDoubleChecker<double> ())
		.AddAttribute("ClampTargetRate",
				"Clamp target rate.",
				BooleanValue(false),
				MakeBooleanAccessor(&RdmaHw::m_EcnClampTgtRate),
				MakeBooleanChecker())
		.AddAttribute("RPTimer",
				"The rate increase timer at RP in microseconds",
				DoubleValue(1500.0),
				MakeDoubleAccessor(&RdmaHw::m_rpgTimeReset),
				MakeDoubleChecker<double>())
		.AddAttribute("RateDecreaseInterval",
				"The interval of rate decrease check",
				DoubleValue(4.0),
				MakeDoubleAccessor(&RdmaHw::m_rateDecreaseInterval),
				MakeDoubleChecker<double>())
		.AddAttribute("FastRecoveryTimes",
				"The rate increase timer at RP",
				UintegerValue(5),
				MakeUintegerAccessor(&RdmaHw::m_rpgThreshold),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute("AlphaResumInterval",
				"The interval of resuming alpha",
				DoubleValue(55.0),
				MakeDoubleAccessor(&RdmaHw::m_alpha_resume_interval),
				MakeDoubleChecker<double>())
		.AddAttribute("RateAI",
				"Rate increment unit in AI period",
				DataRateValue(DataRate("5Mb/s")),
				MakeDataRateAccessor(&RdmaHw::m_rai),
				MakeDataRateChecker())
		.AddAttribute("RateHAI",
				"Rate increment unit in hyperactive AI period",
				DataRateValue(DataRate("50Mb/s")),
				MakeDataRateAccessor(&RdmaHw::m_rhai),
				MakeDataRateChecker())
		.AddAttribute("VarWin",
				"Use variable window size or not",
				BooleanValue(false),
				MakeBooleanAccessor(&RdmaHw::m_var_win),
				MakeBooleanChecker())
		.AddAttribute("FastReact",
				"Fast React to congestion feedback",
				BooleanValue(true),
				MakeBooleanAccessor(&RdmaHw::m_fast_react),
				MakeBooleanChecker())
		.AddAttribute("RateBound",
				"Bound packet sending by rate, for test only",
				BooleanValue(true),
				MakeBooleanAccessor(&RdmaHw::m_rateBound),
				MakeBooleanChecker())
		.AddAttribute("TotalPauseTimes",
				"The number of pause times to simulate PFC pause due to PCIe",
				UintegerValue(0),
				MakeUintegerAccessor(&RdmaHw::m_total_pause_times),
				MakeUintegerChecker<uint64_t>())
		.AddAttribute("L2Timeout", 
				"Sender's timer of waiting for the ack",
                TimeValue(MilliSeconds(4)), 
				MakeTimeAccessor(&RdmaHw::m_waitAckTimeout),
                MakeTimeChecker());
		;
	return tid;
}

RdmaHw::RdmaHw(){
	enable_pcie_pause = true;
	m_paused_times = 0;
}

void RdmaHw::SetNode(Ptr<Node> node){
	m_node = node;
}

void RdmaHw::Setup(QpCompleteCallback cb){
	
	for (uint32_t i = 0; i < m_nic.size(); i++) {

		Ptr<QbbNetDevice> dev = m_nic[i].dev;
		if (!dev) continue;

		// share data with NIC
		dev->m_rdmaEQ->m_qpGrp = m_nic[i].qpGrp;

		// setup callback
		dev->m_rdmaReceiveCb = MakeCallback(&RdmaHw::Receive, this);
		dev->m_rdmaLinkDownCb = MakeCallback(&RdmaHw::SetLinkDown, this);
		dev->m_rdmaPktSent = MakeCallback(&RdmaHw::PktSent, this);

		// config NIC
		dev->m_rdmaEQ->m_rdmaGetNxtPkt = MakeCallback(&RdmaHw::GetNxtPacket, this);
	}

	// setup qp complete callback
	m_qpCompleteCallback = cb;
}

uint32_t RdmaHw::GetNicIdxOfQp(Ptr<RdmaQueuePair> qp){
	// get the destination IP

	auto &v = m_rtTable[qp->dip.Get()];

	if (v.size() > 0){
		return v[qp->GetHash() % v.size()];
	}else{
		NS_ASSERT_MSG(false, "We assume at least one NIC is alive");
	}
}

uint64_t RdmaHw::GetQpKey(uint32_t dip, uint16_t sport, uint16_t pg){
	return ((uint64_t)dip << 32) | ((uint64_t)sport << 16) | (uint64_t)pg;
}

Ptr<RdmaQueuePair> RdmaHw::GetQp(uint32_t dip, uint16_t sport, uint16_t pg){
	uint64_t key = GetQpKey(dip, sport, pg);
	auto it = m_qpMap.find(key);
	if (it != m_qpMap.end())
		return it->second;
	return NULL;
}

void RdmaHw::AddQueuePair(uint32_t src, uint32_t dest, uint64_t tag, uint64_t size, uint16_t pg, Ipv4Address sip, Ipv4Address dip, uint16_t sport, uint16_t dport, uint32_t win, uint64_t baseRtt, Callback<void> notifyAppFinish, Callback<void> notifyAppSent){
	
	// create qp
	Ptr<RdmaQueuePair> qp = CreateObject<RdmaQueuePair>(pg, sip, dip, sport, dport);
	qp->SetSrc(src);
	qp->SetDest(dest);
	qp->SetTag(tag);
	qp->SetSize(size);
	qp->SetInitialSize(size);
	qp->SetWin(win);
	qp->SetBaseRtt(baseRtt);
	qp->SetVarWin(m_var_win);
	qp->SetAppNotifyCallback(notifyAppFinish);
	qp->SetAppSentCallback(notifyAppSent);
	qp->SetTimeout(m_waitAckTimeout);

	// add qp
	uint32_t nic_idx = GetNicIdxOfQp(qp);
	m_nic[nic_idx].qpGrp->AddQp(qp);
	uint64_t key = GetQpKey(dip.Get(), sport, pg);
	m_qpMap[key] = qp;

	// set init variables
	DataRate m_bps = m_nic[nic_idx].dev->GetDataRate();
	qp->m_rate = m_bps;
	qp->m_max_rate = m_bps;
	if (m_cc_mode == 1){
		qp->mlx.m_targetRate = m_bps;
	} else if (m_cc_mode == 2) { // Customized Congestion Control
		// Any initialization for customized CC
	}

	// Notify Nic
	m_nic[nic_idx].dev->NewQp(qp);
}

void RdmaHw::DeleteQueuePair(Ptr<RdmaQueuePair> qp){
	// remove qp from the m_qpMap
	uint64_t key = GetQpKey(qp->dip.Get(), qp->sport, qp->m_pg);
	m_qpMap.erase(key);
}

Ptr<RdmaRxQueuePair> RdmaHw::GetRxQp(uint32_t sip, uint32_t dip, uint16_t sport, uint16_t dport, uint16_t pg, bool create){
	
	uint64_t key = ((uint64_t)dip << 32) | ((uint64_t)pg << 16) | (uint64_t)dport;
	
	auto it = m_rxQpMap.find(key);
	
	if (it != m_rxQpMap.end())
		return it->second;

	if (create){
		// create new rx qp
		Ptr<RdmaRxQueuePair> q = CreateObject<RdmaRxQueuePair>();
		// init the qp
		q->sip = sip;
		q->dip = dip;
		q->sport = sport;
		q->dport = dport;
		q->m_ecn_source.qIndex = pg;
		// store in map
		m_rxQpMap[key] = q;
		return q;
	}

	return NULL;
}

uint32_t RdmaHw::GetNicIdxOfRxQp(Ptr<RdmaRxQueuePair> q){
	auto &v = m_rtTable[q->dip];
	if (v.size() > 0){
		return v[q->GetHash() % v.size()];
	}else{
		NS_ASSERT_MSG(false, "We assume at least one NIC is alive");
	}
}

void RdmaHw::DeleteRxQp(uint32_t dip, uint16_t pg, uint16_t dport){
	uint64_t key = ((uint64_t)dip << 32) | ((uint64_t)pg << 16) | (uint64_t)dport;
	m_rxQpMap.erase(key);
}

void RdmaHw::PCIeResume(uint32_t nic_idx, uint32_t qIndex){
	Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
	Ptr<Packet> p = dev->NICSendPfc(qIndex, 1);
	m_nic[nic_idx].dev->RdmaEnqueueHighPrioQ(p);
    m_nic[nic_idx].dev->TriggerTransmit();
    Simulator::Schedule(MicroSeconds(1000), &RdmaHw::EnablePause, this);
}

void RdmaHw::EnablePause(){
	enable_pcie_pause = true;
}

void RdmaHw::PCIePause(uint32_t nic_idx, uint32_t qIndex){
	if (m_paused_times >= m_total_pause_times){
		return ;
	}
	m_paused_times++;
	Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
	Ptr<Packet> p = dev->NICSendPfc(qIndex, 0);
	m_nic[nic_idx].dev->RdmaEnqueueHighPrioQ(p);
    m_nic[nic_idx].dev->TriggerTransmit();
	std::cout << "NIC pause "<< m_node->GetId() << " at " << Simulator::Now().GetNanoSeconds() << " paused " << m_paused_times << std::endl;
    Simulator::Schedule(MicroSeconds(10), &RdmaHw::PCIeResume, this, nic_idx, qIndex);
}

int RdmaHw::ReceiveUdp(Ptr<Packet> p, CustomHeader &ch){

	// std::cout<<"ReceiveUdp on: "<<m_node->GetId();
	// std::cout<<" at "<<Simulator::Now().GetNanoSeconds();
	// std::cout<<" from ";
	// Ipv4Address(ch.sip).Print(std::cout);
	// std::cout<<" to ";
	// Ipv4Address(ch.dip).Print(std::cout);
	// std::cout<<" sport "<<ch.udp.sport;
	// std::cout<<" dport "<<ch.udp.dport;
	// std::cout<<" seq "<<ch.udp.seq;
	// std::cout<<" pg "<<ch.udp.pg;
	// std::cout<<" size "<<p->GetSize();
	// std::cout<<" payload_size "<<p->GetSize() - ch.GetSerializedSize();
	// std::cout<<std::endl;

	uint8_t ecnbits = ch.GetIpv4EcnBits();
	uint32_t payload_size = p->GetSize() - ch.GetSerializedSize();

	Ptr<RdmaRxQueuePair> rxQp = GetRxQp(ch.dip, ch.sip, ch.udp.dport, ch.udp.sport, ch.udp.pg, true);
	uint32_t nic_id = GetNicIdxOfRxQp(rxQp);

    if (enable_pcie_pause){
      PCIePause(nic_id, ch.udp.pg);
      enable_pcie_pause = false;
    }

	if (ecnbits != 0){
		rxQp->m_ecn_source.ecnbits |= ecnbits;
		rxQp->m_ecn_source.qfb++;
	}

	rxQp->m_ecn_source.total++;
	rxQp->m_milestone_rx = m_ack_interval;

	int x = ReceiverCheckSeq(ch.udp.seq, rxQp, payload_size);

	// if sequence number is not correct, print debug info
	// if (x == 2) {
	// 	std::cout << "ReceiverCheckSeq Error on " << m_node->GetId() << " " << Simulator::Now().GetNanoSeconds() << " Rx ";
	// 	Ipv4Address(ch.sip).Print(std::cout);
	// 	std::cout << " " << ch.udp.sport << " ";
	// 	Ipv4Address(ch.dip).Print(std::cout);
	// 	std::cout << " " << ch.udp.dport << " " << ch.udp.seq << " " << ch.udp.pg << " " << p->GetSize() << " " << payload_size;
	// 	std::cout << " ReceiverCheckSeq: " << x << std::endl;
	// 	std::cout << "ReceiverCheckSeq: " << ch.udp.seq << " Expected: " << rxQp->ReceiverNextExpectedSeq << std::endl;
	// }

	if (x == 1 || x == 2 || x == 3){ //generate ACK or NACK
		// 1, 3: ACK
		// 2: NACK

		qbbHeader seqh;
		seqh.SetSeq(rxQp->ReceiverNextExpectedSeq);
		seqh.SetPG(ch.udp.pg);
		seqh.SetSport(ch.udp.dport);
		seqh.SetDport(ch.udp.sport);
		seqh.SetIntHeader(ch.udp.ih);
		if (ecnbits)
			seqh.SetCnp();

		Ptr<Packet> newp = Create<Packet>(std::max(60-14-20-(int)seqh.GetSerializedSize(), 0));
		newp->AddHeader(seqh);

		Ipv4Header head;	// Prepare IPv4 header
		head.SetDestination(Ipv4Address(ch.sip));
		head.SetSource(Ipv4Address(ch.dip));
		head.SetProtocol((x == 1 || x == 3) ? 0xFC : 0xFD); //ack=0xFC nack=0xFD
		head.SetTtl(64);
		head.SetPayloadSize(newp->GetSize());
		head.SetIdentification(rxQp->m_ipid++);

		newp->AddHeader(head);
		AddHeader(newp, 0x800);	// Attach PPP header

		// send
		uint32_t nic_idx = GetNicIdxOfRxQp(rxQp);
		m_nic[nic_idx].dev->RdmaEnqueueHighPrioQ(newp);
		m_nic[nic_idx].dev->TriggerTransmit();
	}
	return 0;
}

int RdmaHw::ReceiveCnp(Ptr<Packet> p, CustomHeader &ch){
	// QCN on NIC
	// This is a Congestion signal
	// Then, extract data from the congestion packet.
	// We assume, without verify, the packet is destinated to me
	uint32_t qIndex = ch.cnp.qIndex;
	if (qIndex == 1){		//DCTCP
		return 0;
	}
	uint16_t udpport = ch.cnp.fid; // corresponds to the sport
	uint8_t ecnbits = ch.cnp.ecnBits;
	uint16_t qfb = ch.cnp.qfb;
	uint16_t total = ch.cnp.total;

	Settings::ecn_count++;

	uint32_t i;
	// get qp
	Ptr<RdmaQueuePair> qp = GetQp(ch.sip, udpport, qIndex);
	if (!qp)
		std::cout << "ERROR: QCN NIC cannot find the flow\n";
	// get nic
	uint32_t nic_idx = GetNicIdxOfQp(qp);
	Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;

	if (qp->m_rate == 0) {			//lazy initialization
		qp->m_rate = dev->GetDataRate();
		if (m_cc_mode == 1) {
			qp->mlx.m_targetRate = dev->GetDataRate();
		} else if (m_cc_mode == 2) { // Customized Congestion Control
			// Any initialization for customized CC
		}
	}
	return 0;
}

int RdmaHw::ReceiveAck(Ptr<Packet> p, CustomHeader &ch){
	// if (ch.l3Prot == 0xFD) { // NACK
	// 	std::cout << "NACK on " << m_node->GetId() << " " << Simulator::Now().GetNanoSeconds() << " ";
	// 	Ipv4Address(ch.sip).Print(std::cout);
	// 	std::cout << " " << ch.ack.dport << " ";
	// 	Ipv4Address(ch.dip).Print(std::cout);
	// 	std::cout << " " << ch.ack.dport << " " << ch.ack.seq << " " << ch.ack.pg << " " << p->GetSize() << std::endl;
	// 	assert(false);
	// 	exit(1);
	// 	return 0;
	// }

	uint16_t qIndex = ch.ack.pg;
	uint16_t port = ch.ack.dport;
	uint16_t sport = ch.ack.sport;  // dport for this host (sport of ACK packet)
	uint32_t seq = ch.ack.seq;
	uint8_t cnp = (ch.ack.flags >> qbbHeader::FLAG_CNP) & 1;

	int i;

	Ptr<RdmaQueuePair> qp = GetQp(ch.sip, port, qIndex);

	if (!qp){
		return 0;
	}

	uint32_t nic_idx = GetNicIdxOfQp(qp);
	Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;

	if (m_ack_interval == 0) {
		std::cout << "ERROR: shouldn't receive ack\n";
	} else {
		if (!m_backto0){
			qp->Acknowledge(seq);
		}else {
			uint32_t goback_seq = seq / m_chunk * m_chunk;
			qp->Acknowledge(goback_seq);
		}
		if (qp->IsFinished()){
			QpComplete(qp);
		}
	}

	/**
     * IB Spec Vol. 1 o9-85
     * The requester need not separately time each request launched into the
     * fabric, but instead simply begins the timer whenever it is expecting a response.
     * Once started, the timer is restarted each time an acknowledge
     * packet is received as long as there are outstanding expected responses.
     * The timer does not detect the loss of a particular expected acknowledge
     * packet, but rather simply detects the persistent absence of response
     * packets.
     * */
    if (!qp->IsFinished() && qp->GetOnTheFly() > 0) {

        if (qp->m_retransmit.IsRunning()) {
			qp->m_retransmit.Cancel();
		}

        qp->m_retransmit = Simulator::Schedule(qp->GetRto(m_mtu), &RdmaHw::HandleTimeout, this, qp,
                                               qp->GetRto(m_mtu));
    }

	if (ch.l3Prot == 0xFD) { // NACK
		
		RecoverQueue(qp);

		// std::cout << "Warning: NACK Received";
		// std::cout << qp->snd_nxt << " " << qp->snd_una << std::endl;
	}

	// handle cnp
	if (cnp){
		if (m_cc_mode == 1) { // mlx version of DCQCN
			cnp_received_mlx(qp);
		}
		// else if (m_cc_mode == 2) { // Customized Congestion Control
		// 	cnp_received_customized(qp);
		// }
	}

	if (m_cc_mode == 2) { // Customized Congestion Control
		// Or you can also response to CNP, can change to the above code block
		HandleAckCustomized(qp, p, ch);
	}

	// ACK may advance the on-the-fly window, allowing more packets to send
	dev->TriggerTransmit();

	return 0;
}

int RdmaHw::Receive(Ptr<Packet> p, CustomHeader &ch){
	if (ch.l3Prot == 0x11){ // UDP
		ReceiveUdp(p, ch);
	}else if (ch.l3Prot == 0xFF){ // CNP
		ReceiveCnp(p, ch);
	}else if (ch.l3Prot == 0xFD){ // NACK
		ReceiveAck(p, ch);
	}else if (ch.l3Prot == 0xFC){ // ACK
		ReceiveAck(p, ch);
	}
	return 0;
}

/**
 * @brief Check sequence number when UDP DATA is received
 *
 * @return int
 * 0: should not reach here
 * 1: generate ACK
 * 2: generate NACK 
 * 3: Duplicate packet, 
 * 4: OoO, but skip to send NACK as it is already NACKed.
 * 6: NACK but functionality is ACK (indicating all packets are received)
 */
int RdmaHw::ReceiverCheckSeq(uint32_t seq, Ptr<RdmaRxQueuePair> q, uint32_t size){
	uint32_t expected = q->ReceiverNextExpectedSeq;

	if (seq == expected){

		q->ReceiverNextExpectedSeq = expected + size;
		if (q->ReceiverNextExpectedSeq >= q->m_milestone_rx){
			q->m_milestone_rx += m_ack_interval;
			return 1; //Generate ACK
		}else if (q->ReceiverNextExpectedSeq % m_chunk == 0){
			return 1;
		}else {
			return 5;
		}

	} else if (seq > expected) {
		// Generate NACK
		if (Simulator::Now() >= q->m_nackTimer || q->m_lastNACK != expected){
			q->m_nackTimer = Simulator::Now() + MicroSeconds(m_nack_interval);
			q->m_lastNACK = expected;
			if (m_backto0){
				q->ReceiverNextExpectedSeq = q->ReceiverNextExpectedSeq / m_chunk*m_chunk;
			}
			return 2;
		}else
			return 4;

	}else {
		// Duplicate.

		/**
		* IB Spec C9-110
		* A responder shall respond to all duplicate requests in PSN order;
		* i.e. the request with the (logically) earliest PSN shall be executed first. If,
		* while responding to a new or duplicate request, a duplicate request is received
		* with a logically earlier PSN, the responder shall cease responding
		* to the original request and shall begin responding to the duplicate request
		* with the logically earlier PSN.
		*/
		return 3;
	}
}

void RdmaHw::AddHeader (Ptr<Packet> p, uint16_t protocolNumber){
	PppHeader ppp;
	ppp.SetProtocol (EtherToPpp (protocolNumber));
	p->AddHeader (ppp);
}

uint16_t RdmaHw::EtherToPpp (uint16_t proto){
	switch(proto){
		case 0x0800: return 0x0021;   //IPv4
		case 0x86DD: return 0x0057;   //IPv6
		default: NS_ASSERT_MSG (false, "PPP Protocol number not defined!");
	}
	return 0;
}

void RdmaHw::RecoverQueue(Ptr<RdmaQueuePair> qp){
	std::cout << "RecoverQueue " << m_node->GetId() << " at " << Simulator::Now().GetNanoSeconds();
	std::cout << "Change snd_nxt from " << qp->snd_nxt << " to " << qp->snd_una << std::endl;

	// record the recovery info
	fprintf(Settings::rdma_recovery_record_output, "%llu %u %u %u %llu %llu\n",
			Simulator::Now().GetNanoSeconds(), m_node->GetId(), qp->GetSrc(), qp->GetDest(), qp->GetTag(),
			qp->snd_nxt, qp->snd_una);

	qp->snd_nxt = qp->snd_una;
}

void RdmaHw::QpComplete(Ptr<RdmaQueuePair> qp){
	NS_ASSERT(!m_qpCompleteCallback.IsNull());
	if (m_cc_mode == 1){
		Simulator::Cancel(qp->mlx.m_eventUpdateAlpha);
		Simulator::Cancel(qp->mlx.m_eventDecreaseRate);
		Simulator::Cancel(qp->mlx.m_rpTimer);
	}
	if (qp->m_retransmit.IsRunning()) qp->m_retransmit.Cancel();

	// This callback will log info
	// It may also delete the rxQp on the receiver
	m_qpCompleteCallback(qp);

	qp->m_notifyAppFinish();

	// delete the qp
	DeleteQueuePair(qp);
}

void RdmaHw::SetLinkDown(Ptr<QbbNetDevice> dev){
	printf("RdmaHw: node:%u a link down\n", m_node->GetId());
}

void RdmaHw::AddTableEntry(Ipv4Address &dstAddr, uint32_t intf_idx){
	uint32_t dip = dstAddr.Get();
	m_rtTable[dip].push_back(intf_idx);
}

void RdmaHw::ClearTable(){
	m_rtTable.clear();
}

void RdmaHw::RedistributeQp(){
	// clear old qpGrp
	for (uint32_t i = 0; i < m_nic.size(); i++){
		if (!m_nic[i].dev)
			continue;
		m_nic[i].qpGrp->Clear();
	}

	// redistribute qp
	for (auto &it : m_qpMap){
		Ptr<RdmaQueuePair> qp = it.second;
		uint32_t nic_idx = GetNicIdxOfQp(qp);
		m_nic[nic_idx].qpGrp->AddQp(qp);
		// Notify Nic
		m_nic[nic_idx].dev->ReassignedQp(qp);
	}
}

Ptr<Packet> RdmaHw::GetNxtPacket(Ptr<RdmaQueuePair> qp){
	uint32_t payload_size = qp->GetBytesLeft();
	if (m_mtu < payload_size)
		payload_size = m_mtu;
	Ptr<Packet> p = Create<Packet> (payload_size);
	// add SeqTsHeader
	SeqTsHeader seqTs;
	seqTs.SetSeq (qp->snd_nxt);
	seqTs.SetPG (qp->m_pg);
	p->AddHeader (seqTs);
	// add udp header
	UdpHeader udpHeader;
	udpHeader.SetDestinationPort (qp->dport);
	udpHeader.SetSourcePort (qp->sport);
	p->AddHeader (udpHeader);
	// add ipv4 header
	Ipv4Header ipHeader;
	ipHeader.SetSource (qp->sip);
	ipHeader.SetDestination (qp->dip);
	ipHeader.SetProtocol (0x11);
	ipHeader.SetPayloadSize (p->GetSize());
	ipHeader.SetTtl (64);
	ipHeader.SetTos (0);
	ipHeader.SetIdentification (qp->m_ipid);
	p->AddHeader(ipHeader);
	// add ppp header
	PppHeader ppp;
	ppp.SetProtocol (0x0021); // EtherToPpp(0x800), see point-to-point-net-device.cc
	p->AddHeader (ppp);

	// update state
	qp->snd_nxt += payload_size;
	qp->m_ipid++;

	return p;
}

void RdmaHw::PktSent(Ptr<RdmaQueuePair> qp, Ptr<Packet> pkt, Time interframeGap){

	qp->lastPktSize = pkt->GetSize();
	UpdateNextAvail(qp, interframeGap, pkt->GetSize());

	if (pkt) {
        CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header |
                        CustomHeader::L4_Header);
        pkt->PeekHeader(ch);

#if (SLB_DEBUG == true)
        std::cout << "[RdmaHw::PktSent] Node(" << m_node->GetId() << ")," << PARSE_FIVE_TUPLE(ch)
                  << "l3Prot:" << ch.l3Prot << ",at" << Simulator::Now() << std::endl;
#endif

        // RdmaHw::nAllPkts += 1;
        if (ch.l3Prot == 0x11) {  // UDP

            // Update Timer
            if (qp->m_retransmit.IsRunning()){
				qp->m_retransmit.Cancel();
			}

            qp->m_retransmit = Simulator::Schedule(
				qp->GetRto(m_mtu), &RdmaHw::HandleTimeout, this,
                qp, qp->GetRto(m_mtu));

        } else if (ch.l3Prot == 0xFC || ch.l3Prot == 0xFD || ch.l3Prot == 0xFF) {  // ACK, NACK, CNP
        
		} else if (ch.l3Prot == 0xFE) {                                            // PFC

        }
    }
}

void RdmaHw::HandleTimeout(Ptr<RdmaQueuePair> qp, Time rto) {
	Ipv4Address(qp->sip).Print(std::cout);
	Ipv4Address(qp->dip).Print(std::cout);

    if (qp->IsFinished()) {
        return;
    }

	uint32_t nic_idx = GetNicIdxOfQp(qp);
    Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;

    RecoverQueue(qp);
    dev->TriggerTransmit();
}

void RdmaHw::UpdateNextAvail(Ptr<RdmaQueuePair> qp, Time interframeGap, uint32_t pkt_size){
	Time sendingTime;
	if (m_rateBound)
		sendingTime = interframeGap + qp->m_rate.CalculateBytesTxTime(pkt_size);
	else
		sendingTime = interframeGap + qp->m_max_rate.CalculateBytesTxTime(pkt_size);
	qp->m_nextAvail = Simulator::Now() + sendingTime;
}

void RdmaHw::ChangeRate(Ptr<RdmaQueuePair> qp, DataRate new_rate){
	#if 1
	Time sendingTime = qp->m_rate.CalculateBytesTxTime(qp->lastPktSize);
	Time new_sendintTime = new_rate.CalculateBytesTxTime(qp->lastPktSize);
	qp->m_nextAvail = qp->m_nextAvail + new_sendintTime - sendingTime;
	// update nic's next avail event
	uint32_t nic_idx = GetNicIdxOfQp(qp);
	m_nic[nic_idx].dev->UpdateNextAvail(qp->m_nextAvail);
	#endif

	// change to new rate
	qp->m_rate = new_rate;
}

#define PRINT_LOG 0

/******************************
 * Mellanox's version of DCQCN
 *****************************/

void RdmaHw::UpdateAlphaMlx(Ptr<RdmaQueuePair> q){
	#if PRINT_LOG
	//printf("%lu alpha update: %08x %08x %u %u %.6lf->", Simulator::Now().GetTimeStep(), q->sip.Get(), q->dip.Get(), q->sport, q->dport, q->mlx.m_alpha);
	#endif
	// std::cout << "UpdateAlphaMlx " << m_node->GetId() << " at " << Simulator::Now().GetNanoSeconds();
	// std::cout << ", from: " << q->mlx.m_alpha << " to: ";
	if (q->mlx.m_alpha_cnp_arrived){
		q->mlx.m_alpha = (1 - m_g)*q->mlx.m_alpha + m_g; 	//binary feedback
	}else {
		q->mlx.m_alpha = (1 - m_g)*q->mlx.m_alpha; 	//binary feedback
	}
	// std::cout << q->mlx.m_alpha << std::endl;
	#if PRINT_LOG
	//printf("%.6lf\n", q->mlx.m_alpha);
	#endif
	q->mlx.m_alpha_cnp_arrived = false; // clear the CNP_arrived bit
	ScheduleUpdateAlphaMlx(q);
}
void RdmaHw::ScheduleUpdateAlphaMlx(Ptr<RdmaQueuePair> q){
	q->mlx.m_eventUpdateAlpha = Simulator::Schedule(MicroSeconds(m_alpha_resume_interval), &RdmaHw::UpdateAlphaMlx, this, q);
}

void RdmaHw::cnp_received_mlx(Ptr<RdmaQueuePair> q){
	Settings::ecn_count++;
	q->mlx.m_alpha_cnp_arrived = true; // set CNP_arrived bit for alpha update
	q->mlx.m_decrease_cnp_arrived = true; // set CNP_arrived bit for rate decrease
	if (q->mlx.m_first_cnp){
		// init alpha
		q->mlx.m_alpha = 1;
		q->mlx.m_alpha_cnp_arrived = false;
		// schedule alpha update
		ScheduleUpdateAlphaMlx(q);
		// schedule rate decrease
		ScheduleDecreaseRateMlx(q, 1); // add 1 ns to make sure rate decrease is after alpha update
		// set rate on first CNP
		q->mlx.m_targetRate = q->m_rate = m_rateOnFirstCNP * q->m_rate;
		q->mlx.m_first_cnp = false;
	}
}

void RdmaHw::CheckRateDecreaseMlx(Ptr<RdmaQueuePair> q){
	ScheduleDecreaseRateMlx(q, 0);
	if (q->mlx.m_decrease_cnp_arrived){
		#if PRINT_LOG
		printf("%lu rate dec: %08x %08x %u %u (%0.3lf %.3lf)->", Simulator::Now().GetTimeStep(), q->sip.Get(), q->dip.Get(), q->sport, q->dport, q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
		#endif
		// printf("%lu rate dec: %08x %08x %u %u (%0.3lf %.3lf)->", Simulator::Now().GetTimeStep(), q->sip.Get(), q->dip.Get(), q->sport, q->dport, q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
		bool clamp = true;
		if (!m_EcnClampTgtRate){
			if (q->mlx.m_rpTimeStage == 0)
				clamp = false;
		}
		if (clamp)
			q->mlx.m_targetRate = q->m_rate;
		// std::cout << "CheckRateDecreaseMlx " << m_node->GetId() << " at " << Simulator::Now().GetNanoSeconds();
		// std::cout << ", from: " << q->m_rate.GetBitRate() * 1e-9 << " to: ";
		q->m_rate = std::max(m_minRate, q->m_rate * (1 - q->mlx.m_alpha / 2));
		// std::cout << q->m_rate.GetBitRate() * 1e-9 << std::endl;
		// reset rate increase related things
		q->mlx.m_rpTimeStage = 0;
		q->mlx.m_decrease_cnp_arrived = false;
		Simulator::Cancel(q->mlx.m_rpTimer);
		q->mlx.m_rpTimer = Simulator::Schedule(MicroSeconds(m_rpgTimeReset), &RdmaHw::RateIncEventTimerMlx, this, q);
		#if PRINT_LOG
		printf("(%.3lf %.3lf)\n", q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
		#endif
	}
}
void RdmaHw::ScheduleDecreaseRateMlx(Ptr<RdmaQueuePair> q, uint32_t delta){
	q->mlx.m_eventDecreaseRate = Simulator::Schedule(MicroSeconds(m_rateDecreaseInterval) + NanoSeconds(delta), &RdmaHw::CheckRateDecreaseMlx, this, q);
}

void RdmaHw::RateIncEventTimerMlx(Ptr<RdmaQueuePair> q){
	q->mlx.m_rpTimer = Simulator::Schedule(MicroSeconds(m_rpgTimeReset), &RdmaHw::RateIncEventTimerMlx, this, q);
	RateIncEventMlx(q);
	q->mlx.m_rpTimeStage++;
}

void RdmaHw::RateIncEventMlx(Ptr<RdmaQueuePair> q){
	// check which increase phase: fast recovery, active increase, hyper increase
	// std::cout << "RateIncEventMlx " << m_node->GetId() << " at " << Simulator::Now().GetNanoSeconds() << " ";
	if (q->mlx.m_rpTimeStage < m_rpgThreshold){ // fast recovery
		// std::cout << "FastRecoveryMlx " << std::endl;
		FastRecoveryMlx(q);
	}else if (q->mlx.m_rpTimeStage == m_rpgThreshold){ // active increase
		// std::cout << "ActiveIncreaseMlx " << std::endl;
		ActiveIncreaseMlx(q);
	}else { // hyper increase
		// std::cout << "HyperIncreaseMlx " << std::endl;
		HyperIncreaseMlx(q);
	}
}

void RdmaHw::FastRecoveryMlx(Ptr<RdmaQueuePair> q){
	#if PRINT_LOG
	printf("%lu fast recovery: %08x %08x %u %u (%0.3lf %.3lf)->", Simulator::Now().GetTimeStep(), q->sip.Get(), q->dip.Get(), q->sport, q->dport, q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
	#endif

	q->m_rate = (q->m_rate / 2) + (q->mlx.m_targetRate / 2);

	#if PRINT_LOG
	printf("(%.3lf %.3lf)\n", q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
	#endif
}
void RdmaHw::ActiveIncreaseMlx(Ptr<RdmaQueuePair> q){
	#if PRINT_LOG
	printf("%lu active inc: %08x %08x %u %u (%0.3lf %.3lf)->", Simulator::Now().GetTimeStep(), q->sip.Get(), q->dip.Get(), q->sport, q->dport, q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
	#endif
	// get NIC
	uint32_t nic_idx = GetNicIdxOfQp(q);
	Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
	// increate rate
	q->mlx.m_targetRate += m_rai;
	if (q->mlx.m_targetRate > dev->GetDataRate())
		q->mlx.m_targetRate = dev->GetDataRate();
	q->m_rate = (q->m_rate / 2) + (q->mlx.m_targetRate / 2);
	#if PRINT_LOG
	printf("(%.3lf %.3lf)\n", q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
	#endif
}
void RdmaHw::HyperIncreaseMlx(Ptr<RdmaQueuePair> q){
	#if PRINT_LOG
	printf("%lu hyper inc: %08x %08x %u %u (%0.3lf %.3lf)->", Simulator::Now().GetTimeStep(), q->sip.Get(), q->dip.Get(), q->sport, q->dport, q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
	#endif
	// get NIC
	uint32_t nic_idx = GetNicIdxOfQp(q);
	Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
	// increate rate
	q->mlx.m_targetRate += m_rhai;
	if (q->mlx.m_targetRate > dev->GetDataRate())
		q->mlx.m_targetRate = dev->GetDataRate();
	q->m_rate = (q->m_rate / 2) + (q->mlx.m_targetRate / 2);
	#if PRINT_LOG
	printf("(%.3lf %.3lf)\n", q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
	#endif
}

/******************************
 * Customized Congestion Control
 *****************************/

void RdmaHw::cnp_received_customized(Ptr<RdmaQueuePair> q) {

}

void RdmaHw::HandleAckCustomized(Ptr<RdmaQueuePair> qp, Ptr<Packet> p, CustomHeader &ch) {

}


}