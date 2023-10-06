/*
 * Copyright (C) 2004-2012  See the AUTHORS file for details.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <znc/Modules.h>
#include "JID.h"

class CXMPPClient;
class CXMPPStanza;

class CXMPPChannel {
public:
	CXMPPChannel() {}
	CXMPPChannel(const CXMPPJID &jid, CChan *const &pChan) {
		m_Jid = jid;
		m_pChan = pChan;
	}

	CXMPPJID GetJID() const { return m_Jid; }
	CChan *GetChannel() const { return m_pChan; }

protected:
	CXMPPJID m_Jid;
	CChan *m_pChan;
};

class CXMPPModule : public CModule {
public:
	MODCONSTRUCTOR(CXMPPModule) {};

	virtual bool OnLoad(const CString& sArgs, CString& sMessage);
	virtual EModRet OnDeleteUser(CUser& User);

	void ClientConnected(CXMPPClient &Client);
	void ClientDisconnected(CXMPPClient &Client);

	std::vector<CXMPPClient*>& GetClients() { return m_vClients; };
	CXMPPClient* Client(CUser& User, CString sResource) const;
	CXMPPClient* Client(const CXMPPJID& jid, bool bAcceptNegative = true) const;

	CString GetServerName() const { return m_sServerName; }
	bool IsTLSAvailible() const;
	std::map<CString, CXMPPChannel> &GetChannels(CUser *pUser) {
		return m_vUserChannels[pUser];
	}

	void SendStanza(CXMPPStanza &Stanza);

	virtual CModule::EModRet OnPrivTextMessage(CTextMessage &Message) override;
    virtual CModule::EModRet OnChanTextMessage(CTextMessage &Message) override;
	virtual void OnJoinMessage(CJoinMessage &Message) override;
	virtual void OnPartMessage(CPartMessage &Message) override;
	virtual void OnQuitMessage(CQuitMessage& message, const std::vector<CChan*> &vChans) override;
	virtual void OnKickMessage(CKickMessage &Message) override;
protected:
	std::vector<CXMPPClient*> m_vClients;
	std::map<CUser *, std::map<CString, CXMPPChannel>> m_vUserChannels;
	CString m_sServerName;
};

