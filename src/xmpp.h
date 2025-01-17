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
	CXMPPChannel(const CXMPPJID &jid, CChan *const &pChan, int historyMaxStanzas = 25) {
		m_Jid = jid;
		m_pChan = pChan;
		// Used for callback joins
		m_historyMaxStanzas = historyMaxStanzas;
	}

	CXMPPJID GetJID() const { return m_Jid; }
	CChan *GetChannel() const { return m_pChan; }
	int GetHistoryMaxStanzas() { return m_historyMaxStanzas; }

protected:
	CXMPPJID m_Jid;
	CChan *m_pChan;
	int m_historyMaxStanzas;
};

class CXMPPModule : public CModule {
public:
	MODCONSTRUCTOR(CXMPPModule) {};

	virtual bool OnLoad(const CString& sArgs, CString& sMessage) override;
	virtual EModRet OnDeleteUser(CUser& User) override;

	void ClientConnected(CXMPPClient &Client);
	void ClientDisconnected(CXMPPClient &Client);

	std::vector<CXMPPClient*>& GetClients() { return m_vClients; };
	CXMPPClient* Client(CUser& User, CString sResource) const;
	CXMPPClient* Client(const CXMPPJID& jid, bool bAcceptNegative = true) const;

	CString GetServerName() const { return m_sServerName; }
	bool IsTLSAvailible() const;

	void SendStanza(CXMPPStanza &Stanza);

	virtual CModule::EModRet OnPrivTextMessage(CTextMessage &message) override;
    virtual CModule::EModRet OnChanTextMessage(CTextMessage &message) override;
	virtual void OnJoinMessage(CJoinMessage &message) override;
	virtual void OnPartMessage(CPartMessage &message) override;
	virtual void OnQuitMessage(CQuitMessage &message, const std::vector<CChan*> &vChans) override;
	virtual void OnKickMessage(CKickMessage &message) override;
	virtual CModule::EModRet OnNumericMessage(CNumericMessage &message) override;
protected:
	std::vector<CXMPPClient*> m_vClients;
	CString m_sServerName;
};

