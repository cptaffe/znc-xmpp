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

	void SendStanza(CXMPPStanza &Stanza);

	virtual CModule::EModRet OnPrivTextMessage(CTextMessage &Message);
    virtual CModule::EModRet OnChanTextMessage(CTextMessage &Message);
	virtual CModule::EModRet OnJoinMessage(CTextMessage &Message);
	virtual CModule::EModRet OnPartMessage(CTextMessage &Message);
	virtual CModule::EModRet OnQuitMessage(CTextMessage& message, const std::vector<CChan*> &vChans);
	virtual CModule::EModRet OnKickMessage(CTextMessage &Message);
protected:
	std::vector<CXMPPClient*> m_vClients;
	CString m_sServerName;
};

