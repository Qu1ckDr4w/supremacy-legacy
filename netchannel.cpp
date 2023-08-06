#include "includes.h"

int Hooks::SendDatagram(void* data) {
	if (!IsValidSendDatagram())
		return g_hooks.m_net_channel.GetOldMethod<SendDatagram_t>(INetChannel::SENDDATAGRAM)(this, data);

	const auto net_chan = (INetChannel*)this;

	const auto backup_in_seq = net_chan->m_in_seq;
	const auto backup_in_rel_state = net_chan->m_in_rel_state;

	auto flow_outgoing = g_csgo.m_engine->GetNetChannelInfo()->GetLatency(0);
	auto target_ping = (g_menu.main.misc.fake_latency_amt.get() / 1000.f);

	if (flow_outgoing < target_ping) {
		auto target_in_seq = net_chan->m_in_seq - game::TIME_TO_TICKS(target_ping - flow_outgoing);
		net_chan->m_in_seq = target_in_seq;

		SetInRelStateFromIncSeq(target_in_seq);
	}

	int ret = g_hooks.m_net_channel.GetOldMethod<SendDatagram_t>(INetChannel::SENDDATAGRAM)(this, data);

	net_chan->m_in_seq = backup_in_seq;
	net_chan->m_in_rel_state = backup_in_rel_state;
	return ret;
}

void Hooks::ProcessPacket(void* packet, bool header) {
	g_hooks.m_net_channel.GetOldMethod<ProcessPacket_t>(INetChannel::PROCESSPACKET)(this, packet, header);

	g_cl.UpdateIncomingSequences();

	// set all delays to instant.
	SetEventDelaysToZero();

	// game events are actually fired in OnRenderStart which is WAY later after they are received
	// effective delay by lerp time, now we call them right after they're received (all receive proxies are invoked without delay).
	g_csgo.m_engine->FireEvents();

	if (g_csgo.m_cl->m_net_channel) {
		if (g_cl.m_local && g_cl.m_local->alive()) {
			AddIncomingSequence();

			// Remove outdated incoming sequences.
			RemoveOutdatedIncomingSequences();
		}
		else {
			ClearIncomingSequences();
		}
	}
}

bool Hooks::IsValidSendDatagram() {
	return (this && g_csgo.m_engine->IsInGame() && g_csgo.m_net && g_cl.m_local && g_cl.m_local->alive()
		&& (INetChannel*)this == g_csgo.m_cl->m_net_channel && g_aimbot.m_fake_latency && g_cl.m_processing);
}

void Hooks::SetInRelStateFromIncSeq(int target_in_seq) {
	const auto net_chan = (INetChannel*)this;

	for (auto& seq : g_cl.m_inc_seq) {
		if (seq.m_in_seq == target_in_seq) {
			net_chan->m_in_rel_state = seq.m_in_rel_state;
			break;
		}
	}
}

void Hooks::SetEventDelaysToZero() {
	for (CEventInfo* it{ g_csgo.m_cl->m_events }; it != nullptr; it = it->m_next) {
		if (!it->m_class_id)
			continue;

		it->m_fire_delay = 0.f;
	}
}

void Hooks::AddIncomingSequence() {
	const auto net_chan = (INetChannel*)this;
	g_cl.m_inc_seq.push_back(Client::incoming_seq_t{ net_chan->m_in_seq, net_chan->m_in_rel_state });
}

void Hooks::RemoveOutdatedIncomingSequences() {
	const auto net_chan = (INetChannel*)this;
	auto delta = [](int a, int b) { return std::abs(a - b); };

	for (auto it = g_cl.m_inc_seq.begin(); it != g_cl.m_inc_seq.end();) {
		auto delta_seq = delta(net_chan->m_in_seq, it->m_in_seq);
		if (delta_seq > 128) {
			it = g_cl.m_inc_seq.erase(it);
		}
		else {
			++it;
		}
	}
}

void Hooks::ClearIncomingSequences() {
	g_cl.m_inc_seq.clear();
}