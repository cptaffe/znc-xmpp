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

class CXMPPClient : public CXMPPSocket {
public:
	CXMPPClient(CModule *pModule);
	virtual ~CXMPPClient();

	CUser* GetUser() const { return m_pUser; }
	CString GetResource() const { return m_sResource; }
	int GetPriority() const { return m_uiPriority; }
	CString GetJID() const;
	std::map<CString, CString> GetChannels() const { return m_sChannels; };

	bool Write(CString sData);
	bool Write(const CXMPPStanza& Stanza);
	bool Write(CXMPPStanza& Stanza, const CXMPPStanza *pStanza);

	void Error(CString tag, CString type, CString code = "", const CXMPPStanza *pStanza = NULL);

	virtual void StreamStart(CXMPPStanza &Stanza);
	virtual void ReceiveStanza(CXMPPStanza &Stanza);

protected:
	CUser *m_pUser;

	CString m_sResource;
	int m_uiPriority;
	std::map<CString, CString> m_sChannels;
};

