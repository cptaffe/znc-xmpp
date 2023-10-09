/*
 * Copyright (C) 2004-2012  See the AUTHORS file for details.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <znc/User.h>
#include <znc/znc.h>

#include "Socket.h"
#include "JID.h"

class CXMPPClient : public CXMPPSocket {
public:
	CXMPPClient(CModule *pModule);
	virtual ~CXMPPClient();

	CUser* GetUser() const { return m_pUser; }
	CString GetResource() const { return m_sResource; }
	int GetPriority() const { return m_uiPriority; }
	CString GetJID() const;
	std::map<CString, CXMPPChannel> &GetChannels() const { return m_mChannels; }

	bool Write(CString sData);
	bool Write(const CXMPPStanza& Stanza);
	bool Write(CXMPPStanza& Stanza, const CXMPPStanza *pStanza = nullptr);

	void Error(const CString &tag, const CString &type, const CString &code = "", const CXMPPStanza *pStanza = nullptr, const CString &text = "");
	void Presence(const CXMPPJID &from, const CString &type = "", const CString &status = "",  const CXMPPStanza *pStanza = nullptr);
	void ChannelPresence(const CXMPPJID &from, const CXMPPJID &jid, const CString &type = "", const CString &status = "", const std::vector<CString> &codes = {}, const CXMPPStanza *pStanza = nullptr);

	virtual void StreamStart(CXMPPStanza &Stanza);
	virtual void ReceiveStanza(CXMPPStanza &Stanza);

	void JoinChannel(CChan *const &channel, const CXMPPJID &to, int maxStanzas = 25);

protected:
	CUser *m_pUser;

	CString m_sResource;
	int m_uiPriority;
	std::map<CString, CXMPPChannel> m_mChannels;
};

