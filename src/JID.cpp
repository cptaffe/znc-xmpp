/*
 * Copyright (C) 2004-2012  See the AUTHORS file for details.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "JID.h"
#include "xmpp.h"

CXMPPJID::CXMPPJID(CString sJID) {
	m_sUser = sJID.Token(0, false, "@");
	CString sDomain = sJID.Token(1, false, "@");
	m_sResource = sDomain.Token(1, false, "/");
	m_sDomain = sDomain.Token(0, false, "/");
}

CXMPPJID::CXMPPJID(CString user, CString domain, CString resource) {
	m_sUser = user;
	m_sDomain = domain;
	m_sResource = resource;
}

CString CXMPPJID::ToString() const {
	CString sResult;

	if (!m_sUser.empty()) {
		sResult = m_sUser + "@";
	}

	sResult += m_sDomain;

	if (!m_sResource.empty()) {
		sResult += "/" + m_sResource;
	}

	return sResult;
}

bool CXMPPJID::IsLocal(const CXMPPModule &Module) const {
	return m_sDomain.Equals(Module.GetServerName());
}

bool CXMPPJID::IsBlank() const {
	return m_sUser.empty() && m_sDomain.empty() && m_sResource.empty();
}

bool CXMPPJID::IsIRC() const {
	return m_sUser.EndsWith("+irc");
}

bool CXMPPJID::IsIRCChannel() const {
	return IsIRC() && m_sUser.StartsWith("#");
}

bool CXMPPJID::IsIRCUser() const {
	return IsIRC() && !(IsIRCChannel() && m_sResource.empty());
}

CString CXMPPJID::GetIRCChannel() const {
	if (!IsIRCChannel())
		return "";
	return m_sUser.Token(0, false, "!");
}

CString CXMPPJID::GetIRCUser() const {
	if (!IsIRCUser())
		return "";
	if (IsIRCChannel())
		return m_sResource;
	return m_sUser.Token(0, false, "!");
}

CString CXMPPJID::GetIRCNetwork() const {
	return m_sUser.Token(1, false, "!").Token(0, false, "+irc");
}

bool CXMPPJID::Equals(const CXMPPJID &other) const {
	return m_sUser.Equals(other.m_sUser)
		&& m_sDomain.Equals(other.m_sDomain)
		&& m_sResource.Equals(other.m_sResource);
}
