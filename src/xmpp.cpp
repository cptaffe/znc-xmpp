/*
 * Copyright (C) 2004-2012  See the AUTHORS file for details.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <znc/IRCNetwork.h>
#include <znc/Chan.h>

#include "xmpp.h"
#include "Client.h"
#include "Listener.h"
#include "Stanza.h"
#include "Codes.h"

// Keep the socket alive
class CXMPPSpaceJob : public CTimer {
public:
	CXMPPSpaceJob(CModule* pModule, unsigned int uInterval, unsigned int uCycles, const CString& sLabel, const CString& sDescription)
		: CTimer(pModule, uInterval, uCycles, sLabel, sDescription) {}
	virtual ~CXMPPSpaceJob() {}
protected:
	virtual void RunJob() {
		CXMPPModule *module = (CXMPPModule *)m_pModule;
		std::vector<CXMPPClient *> &clients = module->GetClients();

		// TODO: Ensure this doesn't corrupt the stream
		for (std::vector<CXMPPClient *>::const_iterator iter = clients.begin(); iter != clients.end(); ++iter) {
			CXMPPClient *client = *iter;
			if (client->IsConnected())
				client->Write(" ");
		}
	}
};

bool CXMPPModule::OnLoad(const CString& sArgs, CString& sMessage) {
	m_sServerName = sArgs.Token(0);
	if (m_sServerName.empty()) {
		m_sServerName = "localhost";
	}

	CXMPPListener *pClient = new CXMPPListener(this);
	pClient->Listen(5222, false);

	AddTimer(new CXMPPSpaceJob(this, 30, 0, "CXMPPSpace", "Periodically sends a space on the socket to prevent closing"));

	return true;
}

CModule::EModRet CXMPPModule::OnDeleteUser(CUser& User) {
	// Delete clients
	std::vector<CXMPPClient*>::iterator it;
	for (it = m_vClients.begin(); it != m_vClients.end();) {
		CXMPPClient *pClient = *it;

		if (pClient->GetUser() == &User) {
			CZNC::Get().GetManager().DelSockByAddr(pClient);
			it = m_vClients.erase(it);
		} else {
			++it;
		}
	}

	return CONTINUE;
}

void CXMPPModule::ClientConnected(CXMPPClient &Client) {
	m_vClients.push_back(&Client);
}

void CXMPPModule::ClientDisconnected(CXMPPClient &Client) {
	for (std::vector<CXMPPClient*>::iterator it = m_vClients.begin(); it != m_vClients.end(); ++it) {
		if (*it == &Client) {
			m_vClients.erase(it);
			break;
		}
	}
}

CXMPPClient* CXMPPModule::Client(CUser& user, CString sResource) const {
	for (const auto &pClient : m_vClients) {
		if (pClient->GetUser() == &user && sResource.Equals(pClient->GetResource())) {
			return pClient;
		}
	}

	return NULL;
}

CXMPPClient* CXMPPModule::Client(const CXMPPJID& jid, bool bAcceptNegative) const {
	if (!jid.IsLocal(*this)) {
		return NULL;
	}

	CXMPPClient *pCurrent = NULL;

	for (const auto &pClient : m_vClients) {
		if (pClient->GetUser() && pClient->GetUser()->GetUserName().Equals(jid.GetUser())) {
			if (!jid.GetResource().empty() && jid.GetResource().Equals(pClient->GetResource())) {
				return pClient;
			}

			if (!pCurrent || (pClient->GetPriority() > pCurrent->GetPriority())) {
				pCurrent = pClient;
			}
		}
	}

	if (!bAcceptNegative && pCurrent && (pCurrent->GetPriority() < 0)) {
		return NULL;
	}

	return pCurrent;
}

bool CXMPPModule::IsTLSAvailible() const {
#ifdef HAVE_LIBSS
	CString sPemFile = CZNC::Get().GetPemLocation();
	if (!sPemFile.empty() && access(sPemFile.c_str(), R_OK) == 0) {
		return true;
	}
#endif

	return false;
}

void CXMPPModule::SendStanza(CXMPPStanza &Stanza) {
	CXMPPJID to(Stanza.GetAttribute("to"));

	if (to.IsLocal(*this)) {
		CXMPPClient *pClient = Client(to);
		if (pClient) {
			pClient->Write(Stanza);
			return;
		}
	}

	// TODO: Keep-alive so that the socket doesn't close
	// TODO: Send messages addressed to IRC channels or users to those channels/users
	// TODO: Proxy messages received on IRC

	CXMPPJID from(Stanza.GetAttribute("from"));
	if (from.IsLocal(*this)) {
		CXMPPClient *pClient = Client(from);
		if (pClient) {
			CXMPPStanza errorStanza(Stanza.GetName());
			errorStanza.SetAttribute("to", Stanza.GetAttribute("from"));
			errorStanza.SetAttribute("from", Stanza.GetAttribute("to"));
			errorStanza.SetAttribute("id", Stanza.GetAttribute("id"));
			errorStanza.SetAttribute("type", "error");

			CXMPPStanza& error = errorStanza.NewChild("error");
			error.SetAttribute("type", "cancel");

			if (to.IsLocal(*this)) {
				error.NewChild("service-unavailable", "urn:ietf:params:xml:ns:xmpp-stanzas");
			} else {
				error.NewChild("remote-server-not-found", "urn:ietf:params:xml:ns:xmpp-stanzas");
			}

			pClient->Write(errorStanza);
		}
	}
}

CModule::EModRet CXMPPModule::OnChanTextMessage(CTextMessage& message) {
	CIRCNetwork *network = message.GetNetwork();
	CChan *channel = message.GetChan();
	CNick &nick = message.GetNick();

	if (!network || !channel) {
		return CModule::CONTINUE;
	}

	CXMPPJID from(channel->GetName() + "!" + network->GetName() + "+irc", GetServerName(), nick.GetNick());

	CXMPPStanza iq("message");
	iq.SetAttribute("id", "znc_" + CString::RandomString(8));
	iq.SetAttribute("type", "groupchat");
	if (!nick.GetNick().Equals(network->GetCurNick())) {
		iq.SetAttribute("from", from.ToString());
	}
	CXMPPStanza &body = iq.NewChild("body");
	body.NewChild().SetText(message.GetText());

	for (const auto &client : m_vClients) {
		CUser *user = client->GetUser();
		if (!user || !user->GetUsername().Equals(network->GetUser()->GetUsername()))
			continue;

		// Check that this client is in the channel
		CXMPPJID jid = client->GetChannels()[from.GetUser()].GetJID();
		if (jid.IsBlank())
			continue;

		// self messages
		if (!iq.HasAttribute("from")) {
			iq.SetAttribute("from", jid.ToString());
		}

		iq.SetAttribute("to", client->GetJID());
		client->Write(iq);
	}

	return CModule::CONTINUE;
}

CModule::EModRet CXMPPModule::OnPrivTextMessage(CTextMessage& message) {
	CIRCNetwork *network = message.GetNetwork();
	CNick &nick = message.GetNick();

	if (!network) {
		return CModule::CONTINUE;
	}

	CXMPPStanza iq("message");
	iq.SetAttribute("id", "znc_" + CString::RandomString(8));
	iq.SetAttribute("type", "chat");
	if (!nick.GetNick().Equals(network->GetCurNick())) {
		iq.SetAttribute("from", nick.GetNick() + "!" + network->GetName() + "+irc@" + GetServerName());
	}
	CXMPPStanza &body = iq.NewChild("body");
	body.NewChild().SetText(message.GetText());

	for (const auto &client : m_vClients) {
		CUser *user = client->GetUser();
		// TODO: Are user pointers comparable?
		if (!user || !user->GetUsername().Equals(network->GetUser()->GetUsername()))
			continue;

		if (!iq.HasAttribute("from")) {
			iq.SetAttribute("from", client->GetJID());
		}
		iq.SetAttribute("to", client->GetJID());
		client->Write(iq);
	}

	return CModule::CONTINUE;
}

void CXMPPModule::OnJoinMessage(CJoinMessage& message) {
	/* Send presence to channel members */
	CIRCNetwork *network = message.GetNetwork();
	CChan *channel = message.GetChan();
	CNick &nick = message.GetNick();

	if (!network || !channel) {
		return;
	}
	if (nick.NickEquals(network->GetNick()))
		return; // ignore self-join

	CXMPPJID from(channel->GetName() + "!" + network->GetName() + "+irc", GetServerName(), nick.GetNick());
	CXMPPJID jid(nick.GetNick() + "!" + network->GetName() + "+irc", GetServerName());

	for (const auto &client : m_vClients) {
		CUser *user = client->GetUser();
		if (network->GetUser() != user)
			continue;

		// Check that this client is in the channel
		CXMPPJID jid = client->GetChannels()[from.GetUser()].GetJID();
		if (jid.IsBlank())
			continue;

		client->ChannelPresence(from, jid);
	}

	return;
}

void CXMPPModule::OnPartMessage(CPartMessage & message) {
	/* Send unavailable status to channel members */
	CIRCNetwork *network = message.GetNetwork();
	CChan *channel = message.GetChan();
	CNick &nick = message.GetNick();

	if (!network || !channel) {
		return;
	}

	CXMPPJID from(channel->GetName() + "!" + network->GetName() + "+irc", GetServerName(), nick.GetNick());
	CXMPPJID jid(nick.GetNick() + "!" + network->GetName() + "+irc", GetServerName());

	for (const auto &client : m_vClients) {
		CUser *user = client->GetUser();
		if (!user || !user->GetUsername().Equals(network->GetUser()->GetUsername()))
			continue;

		// Check that this client is in the channel
		CXMPPJID jid = client->GetChannels()[from.GetUser()].GetJID();
		if (jid.IsBlank())
			continue;

		client->ChannelPresence(from, jid, "unavailable", message.GetReason());
	}

	return;
}

void CXMPPModule::OnQuitMessage(CQuitMessage &message, const std::vector<CChan*> &vChans) {
		/* Send unavailable status to channel members */
	CIRCNetwork *network = message.GetNetwork();
	CNick &nick = message.GetNick();

	if (!network) {
		return;
	}

	CXMPPJID jid(nick.GetNick() + "!" + network->GetName() + "+irc", GetServerName());

	for (const auto &client : m_vClients) {
		CUser *user = client->GetUser();
		if (!user || !user->GetUsername().Equals(network->GetUser()->GetUsername()))
			continue;

		for (const auto &channel : vChans) {
			CXMPPJID from(channel->GetName() + "!" + network->GetName() + "+irc", GetServerName(), nick.GetNick());

			// Check that this client is in the channel
			CXMPPJID jid = client->GetChannels()[from.GetUser()].GetJID();
			if (jid.IsBlank())
				continue;

			client->ChannelPresence(from, jid, "unavailable", message.GetParam(0));
		}

		client->Presence(jid, "unavailable", message.GetParam(0));
	}

	return;
}

void CXMPPModule::OnKickMessage(CKickMessage &message) {
	/* Send unavailable status to channel members */
	CIRCNetwork *network = message.GetNetwork();
	CChan *channel = message.GetChan();
	CString nick = message.GetTarget();
	CString status = message.GetText();

	if (!network || !channel) {
		return;
	}

	CXMPPJID from(channel->GetName() + "!" + network->GetName() + "+irc", GetServerName(), nick);
	CXMPPJID jid(nick + "!" + network->GetName() + "+irc", GetServerName());

	for (const auto &client : m_vClients) {
		CUser *user = client->GetUser();
		if (!user || !user->GetUsername().Equals(network->GetUser()->GetUsername()))
			continue;

		// Check that this client is in the channel
		CXMPPJID jid = client->GetChannels()[from.GetUser()].GetJID();
		if (jid.IsBlank())
			continue;

		client->ChannelPresence(from, jid, "unavailable", status, {"307"});
	}

	return;
}

// Taken from https://www.alien.net.au/irc/irc2numerics.html
static const CString errors[] = {
	// starts at 400
	"Unknown error",
	"No such nick",
	"No such server",
	"No such channel",

};

CModule::EModRet CXMPPModule::OnNumericMessage(CNumericMessage &message) {
	const IRCCode &code = IRCCode::FindCode(message.GetCode());
	CIRCNetwork *network = message.GetNetwork();
	CNick &nick = message.GetNick();
	if (code.GetCode() == 366) {
		/* RPL_ENDOFNAMES signals that a join is finished
		   and the topic/nicks for a channel are populated. */
		CIRCNetwork *network = message.GetNetwork();
		CChan *channel = network->FindChan(message.GetParam(1));

		if (!network || !channel) {
			return CModule::CONTINUE;
		}

		DEBUG("XMPPModule finishing join to " + channel->GetName() + " on " + network->GetName());

		CString chanuser = channel->GetName() + "!" + network->GetName() + "+irc";

		for (const auto &client : m_vClients) {
			CUser *user = client->GetUser();
			if (!user)
				continue;

			CXMPPChannel chan = client->GetChannels()[chanuser];
			CXMPPJID jid = chan.GetJID();
			if (jid.IsBlank())
				continue;

			// Finish join
			client->JoinChannel(channel, jid, chan.GetHistoryMaxStanzas());
		}

		return CModule::CONTINUE;
	}

	/* Send error message to client as PM */
	if (code.IsClientError() || code.IsServerError()) {
		CXMPPStanza iq("message");
		iq.SetAttribute("id", "znc_" + CString::RandomString(8));
		iq.SetAttribute("type", "chat");
		iq.SetAttribute("from", nick.GetNick() + "!" + network->GetName() + "+irc@" + GetServerName());
		CXMPPStanza &body = iq.NewChild("body");
		CString text;
		for (const auto &param : message.GetParams()) {
			if (!text.empty()) {
				text += " ";
			}
			text += param;
		}
		body.NewChild().SetText(text);

		for (const auto &client : m_vClients) {
			CUser *user = client->GetUser();
			if (!user || user != network->GetUser())
				continue;

			iq.SetAttribute("to", client->GetJID());
			client->Write(iq);
		}
	}

	return CModule::CONTINUE;
}

GLOBALMODULEDEFS(CXMPPModule, "XMPP support for ZNC");

