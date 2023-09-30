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
	std::vector<CXMPPClient*>::iterator it;
	for (it = m_vClients.begin(); it != m_vClients.end(); ++it) {
		CXMPPClient *pClient = *it;

		if (pClient == &Client) {
			m_vClients.erase(it);
			break;
		}
	}
}

CXMPPClient* CXMPPModule::Client(CUser& User, CString sResource) const {
	std::vector<CXMPPClient*>::const_iterator it;
	for (it = m_vClients.begin(); it != m_vClients.end(); ++it) {
		CXMPPClient *pClient = *it;

		if (pClient->GetUser() == &User && sResource.Equals(pClient->GetResource())) {
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

	std::vector<CXMPPClient*>::const_iterator it;
	for (it = m_vClients.begin(); it != m_vClients.end(); ++it) {
		CXMPPClient *pClient = *it;

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
				CXMPPStanza& unavailable = error.NewChild("service-unavailable",
											"urn:ietf:params:xml:ns:xmpp-stanzas");
			} else {
				CXMPPStanza &notFound = error.NewChild("remote-server-not-found",
											"urn:ietf:params:xml:ns:xmpp-stanzas");
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

	CString channelJID = channel->GetName() + "!" + network->GetName() + "+irc@" + GetServerName();

	CXMPPStanza iq("message");
	iq.SetAttribute("id", "znc_" + CString::RandomString(8));
	iq.SetAttribute("type", "groupchat");
	if (!nick.GetNick().Equals(network->GetCurNick())) {
		iq.SetAttribute("from", channelJID + "/" + nick.GetNick());
	}
	iq.SetAttribute("to", channelJID);
	CXMPPStanza &body = iq.NewChild("body");
	body.NewChild().SetText(message.GetText());

	for (std::vector<CXMPPClient*>::const_iterator it = m_vClients.begin(); it != m_vClients.end(); ++it) {
		CXMPPClient *client = *it;
		CUser *user = client->GetUser();
		if (!user || !user->GetUsername().Equals(network->GetUser()->GetUsername()))
			continue;

		// Check that this client is in the channel
		CString jid = client->GetChannels()[channel->GetName() + "!" + network->GetName() + "+irc"];
		if (jid.empty())
			continue;

		// self messages
		if (!iq.HasAttribute("from")) {
			iq.SetAttribute("from", jid);
		}

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

	for (std::vector<CXMPPClient*>::const_iterator it = m_vClients.begin(); it != m_vClients.end(); ++it) {
		CXMPPClient *client = *it;
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

GLOBALMODULEDEFS(CXMPPModule, "XMPP support for ZNC");

