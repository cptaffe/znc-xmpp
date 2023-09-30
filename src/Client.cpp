/*
 * Copyright (C) 2004-2012  See the AUTHORS file for details.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <znc/IRCNetwork.h>
#include <znc/Chan.h>

#include "Client.h"
#include "xmpp.h"

#define SUPPORT_RFC_3921

CXMPPClient::CXMPPClient(CModule *pModule) : CXMPPSocket(pModule) {
	m_pUser = NULL;

	GetModule()->ClientConnected(*this);
}

CXMPPClient::~CXMPPClient() {
	GetModule()->ClientDisconnected(*this);
}

CString CXMPPClient::GetJID() const {
	CString sResult;

	if (m_pUser) {
		sResult = m_pUser->GetUserName() + "@";
	}

	sResult += GetServerName();

	if (!m_sResource.empty()) {
		sResult += "/" + m_sResource;
	}

	return sResult;
}

bool CXMPPClient::Write(CString sData) {
	return CXMPPSocket::Write(sData);
}

bool CXMPPClient::Write(const CXMPPStanza& Stanza) {
	return CXMPPSocket::Write(Stanza);
}

bool CXMPPClient::Write(CXMPPStanza &Stanza, const CXMPPStanza *pStanza) {
	if (!Stanza.HasAttribute("to") && m_pUser) {
		Stanza.SetAttribute("to", GetJID());
	}
	if (!Stanza.HasAttribute("id") && pStanza && pStanza->HasAttribute("id")) {
		Stanza.SetAttribute("id", pStanza->GetAttribute("id"));
	}
	return Write(Stanza.ToString());
}

void CXMPPClient::Error(CString tag, CString type, CString code, const CXMPPStanza *pStanza) {
	CXMPPStanza iq("iq");
	iq.SetAttribute("to", GetJID());
	iq.SetAttribute("type", "error");
	CXMPPStanza &error = iq.NewChild("error");
	if (!code.empty()) {
		error.SetAttribute("code", code);
	}
	error.SetAttribute("type", type);
	error.NewChild(tag, "urn:ietf:params:xml:ns:xmpp-stanzas");
	Write(iq, pStanza);
}

void CXMPPClient::StreamStart(CXMPPStanza &Stanza) {
	Write("<?xml version='1.0' ?>");
	Write("<stream:stream from='" + GetServerName() + "' version='1.0' xml:lang='en' xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams'>");

	if (!Stanza.GetAttribute("to").Equals(GetServerName())) {
		CXMPPStanza error("stream:error");
		CXMPPStanza& host = error.NewChild("host-unknown", "urn:ietf:params:xml:ns:xmpp-streams");
		CXMPPStanza& text = host.NewChild("text", "urn:ietf:params:xml:ns:xmpp-streams");
		text.NewChild().SetText("This server does not serve " + Stanza.GetAttribute("to"));
		Write(error);

		Write("</stream:stream>");
		Close(Csock::CLT_AFTERWRITE);
		return;
	}

	CXMPPStanza features("stream:features");

#ifdef HAVE_LIBSSL
	if (!GetSSL() && ((CXMPPModule*)m_pModule)->IsTLSAvailible()) {
		CXMPPStanza &starttls = features.NewChild("starttls", "urn:ietf:params:xml:ns:xmpp-tls");
		starttls.NewChild("required");
	}
#endif

	if (m_pUser) {
		features.NewChild("bind", "urn:ietf:params:xml:ns:xmpp-bind");
	} else if (!((CXMPPModule*)m_pModule)->IsTLSAvailible() || GetSSL()) {
		CXMPPStanza& mechanisms = features.NewChild("mechanisms", "urn:ietf:params:xml:ns:xmpp-sasl");

		CXMPPStanza& plain = mechanisms.NewChild("mechanism");
		plain.NewChild().SetText("PLAIN");
	}

	features.NewChild("auth", "http://jabber.org/features/iq-auth");

	Write(features);
}

void CXMPPClient::ReceiveStanza(CXMPPStanza &Stanza) {
	if (Stanza.GetName().Equals("auth")) {
		if (Stanza.GetAttribute("mechanism").Equals("plain")) {
			CString sSASL;
			CXMPPStanza *pStanza = Stanza.GetTextChild();

			if (pStanza)
				sSASL = pStanza->GetText().Base64Decode_n();

			const char *sasl = sSASL.c_str();
			unsigned int y = 0;
			for (unsigned int x = 0; x < sSASL.size(); x++) {
				if (sasl[x] == 0) {
					y++;
				}
			}

			CString sUsername = "unknown";

			if (y > 1) {
				const char *username = &sasl[strlen(sasl) + 1];
				const char *password = &username[strlen(username) + 1];
				sUsername = username;

				CUser *pUser = CZNC::Get().FindUser(sUsername);

				if (pUser && pUser->CheckPass(password)) {
					Write(CXMPPStanza("success", "urn:ietf:params:xml:ns:xmpp-sasl"));

					m_pUser = pUser;
					DEBUG("XMPPClient SASL::PLAIN for [" << sUsername << "] success.");

					/* Restart the stream */
					m_bResetParser = true;

					return;
				}
			}

			DEBUG("XMPPClient SASL::PLAIN for [" << sUsername << "] failed.");

			CXMPPStanza failure("failure", "urn:ietf:params:xml:ns:xmpp-sasl");
			failure.NewChild("not-authorized");
			Write(failure);
			return;
		}

		CXMPPStanza failure("failure", "urn:ietf:params:xml:ns:xmpp-sasl");
		failure.NewChild("invalid-mechanism");
		Write(failure);
		return;
	} else if (Stanza.GetName().Equals("iq")) {
		/* Non-SASL Authentication: https://xmpp.org/extensions/xep-0078.html */
		CXMPPStanza iq("iq");
		iq.SetAttribute("id", Stanza.GetAttribute("id"));

		CXMPPStanza *pQuery = Stanza.GetChildByName("query");
		if (pQuery && pQuery->GetAttribute("xmlns").Equals("jabber:iq:auth")) {
			if (Stanza.GetAttribute("type").Equals("get")) {
				iq.SetAttribute("type", "result");
				CXMPPStanza &query = iq.NewChild("query", "jabber:iq:auth");
				query.NewChild("username");
				query.NewChild("password");
				query.NewChild("resource");
				Write(iq);
				return;
			} else if (Stanza.GetAttribute("type").Equals("set")) {
				CXMPPStanza *pUsername = pQuery->GetChildByName("username");
				CXMPPStanza *pPassword = pQuery->GetChildByName("password");
				CXMPPStanza *pResource = pQuery->GetChildByName("resource");

				if (pUsername && pPassword) {
					pUsername = pUsername->GetTextChild();
					pPassword = pPassword->GetTextChild();
					pResource = pResource->GetTextChild();
				}

				CString sUsername = "unknown";

				if (pUsername && pPassword) {
					sUsername = pUsername->GetText().c_str();
					CString sPassword = pPassword->GetText().c_str();

					CUser *pUser = CZNC::Get().FindUser(sUsername);

					if (pUser && pUser->CheckPass(sPassword)) {
						iq.SetAttribute("type", "result");
						Write(iq);

						m_pUser = pUser;
						if (pResource) {
							m_sResource = pResource->GetText().c_str();
						}
						DEBUG("XMPPClient jabber:iq:auth for [" << sUsername << "] success.");

						return;
					}

					DEBUG("XMPPClient jabber:iq:auth for [" << sUsername << "] failed: incorrect credentials.");

					/* Incorrect Credentials */
					Error("not-authorized", "auth", "401", &Stanza);
					return;
				}

				DEBUG("XMPPClient jabber:iq:auth for [" << sUsername << "] failed: required information not provided.");

				/* Required Information Not Provided */
				Error("not-acceptable", "modify", "406", &Stanza);
				return;
			}
		}
		/* Other iq stanzas are handled further down */
	} else if (Stanza.GetName().Equals("starttls")) {
#ifdef HAVE_LIBSSL
		if (!GetSSL() && ((CXMPPModule*)m_pModule)->IsTLSAvailible()) {
			Write(CXMPPStanza("proceed", "urn:ietf:params:xml:ns:xmpp-tls"));

			/* Restart the stream */
			m_bResetParser = true;

			SetPemLocation(CZNC::Get().GetPemLocation());
			StartTLS();

			return;
		}
#endif

		Write(CXMPPStanza("failure", "urn:ietf:params:xml:ns:xmpp-tls"));
		Write("</stream:stream>");
		Close(Csock::CLT_AFTERWRITE);
		return;
	}

	if (!m_pUser) {
		Error("forbidden", "auth", "403", &Stanza);
		return; /* the following stanzas require auth */
	}

	Stanza.SetAttribute("from", GetJID());

	if (Stanza.GetName().Equals("iq")) {
		CXMPPStanza iq("iq");

		if (Stanza.GetAttribute("type").Equals("get")) {
			if (Stanza.GetChildByName("ping")) {
				iq.SetAttribute("type", "result");
			}

			CXMPPStanza *pQuery = Stanza.GetChildByName("query");
			if (pQuery) {
				CXMPPJID to(Stanza.GetAttribute("to"));

				/* Service Discovery: https://xmpp.org/extensions/xep-0030.html */
				/* MUC: Discovering Rooms: https://xmpp.org/extensions/xep-0045.html#disco-rooms */
				if (pQuery->GetAttribute("xmlns").Equals("http://jabber.org/protocol/disco#items")
						&& Stanza.GetAttribute("to").Equals(GetServerName())) {
					iq.SetAttribute("type", "result");
					CXMPPStanza &query = iq.NewChild("query", "http://jabber.org/protocol/disco#items");

					// Enumerate networks
					const std::vector<CIRCNetwork*> &networks = m_pUser->GetNetworks();
					for (std::vector<CIRCNetwork*>::const_iterator it = networks.begin(); it != networks.end(); ++it) {
						CIRCNetwork *network = *it;
						if (!network->IsIRCConnected())
							continue;

						// Enumerate channels
						const std::vector<CChan*> &channels = network->GetChans();
						for (std::vector<CChan*>::const_iterator it = channels.begin(); it != channels.end(); ++it) {
							CChan *channel = *it;
							if (!channel->IsOn())
								continue;

							// Present each channel as a room
							// JID grammar: https://xmpp.org/extensions/xep-0029.html#sect-idm45406366945648
							CString jid = channel->GetName() + "!" + network->GetName() + "+irc@" + GetServerName();
							CString name = channel->GetName() + " on " + network->GetName();

							CXMPPStanza &item = query.NewChild("item");
							item.SetAttribute("jid", jid);
							item.SetAttribute("name", name);
						}
					}

					Write(iq, &Stanza);
					return;
				}

				/* MUC: Querying Room Information: https://xmpp.org/extensions/xep-0045.html#disco-roominfo */
				if (pQuery->GetAttribute("xmlns").Equals("http://jabber.org/protocol/disco#info")) {
					if (Stanza.GetAttribute("to").Equals(GetServerName())) {
						iq.SetAttribute("type", "result");
						CXMPPStanza &query = iq.NewChild("query", "http://jabber.org/protocol/disco#info");
						CXMPPStanza &identity = query.NewChild("identity");
						identity.SetAttribute("category", "conference");
						identity.SetAttribute("name", "XMPP Gateway ZNC Module");
						identity.SetAttribute("type", "text");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/muc");

						Write(iq, &Stanza);
						return;
					}

					if (to.IsLocal(*GetModule()) && to.IsIRCChannel()) {
						CIRCNetwork *network = m_pUser->FindNetwork(to.GetIRCNetwork());
						if (network) {
							CChan *channel = network->FindChan(to.GetIRCTarget());
							if (channel) {
								iq.SetAttribute("type", "result");
								CXMPPStanza &query = iq.NewChild("query", "http://jabber.org/protocol/disco#info");
								CXMPPStanza &identity = query.NewChild("identity");
								identity.SetAttribute("category", "conference");
								identity.SetAttribute("name", channel->GetName() + " on " + network->GetName());
								identity.SetAttribute("type", "text");
								query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/muc");
								query.NewChild("feature").SetAttribute("var", "muc_nonanonymous");
								query.NewChild("feature").SetAttribute("var", "muc_open");
								query.NewChild("feature").SetAttribute("var", "muc_persistent");
								query.NewChild("feature").SetAttribute("var", "muc_public");

								Write(iq, &Stanza);
								return;
							}
						}
					}

					/* Item Not Found */
					Error("item-not-found", "cancel", "404", &Stanza);
					return;
				}

				/* Roster Get: https://xmpp.org/rfcs/rfc6121.html#roster-syntax-actions-get */
				if (pQuery->GetAttribute("xmlns").Equals("jabber:iq:roster")) {
					iq.SetAttribute("type", "result");
					CXMPPStanza &query = iq.NewChild("query", "jabber:iq:roster");

					// Enumerate networks
					const std::vector<CIRCNetwork*> &networks = m_pUser->GetNetworks();
					for (std::vector<CIRCNetwork*>::const_iterator it = networks.begin(); it != networks.end(); ++it) {
						CIRCNetwork *network = *it;
						if (!network->IsIRCConnected())
							continue;

						// Add yourself
						CXMPPStanza &item = query.NewChild("item");
						item.SetAttribute("jid", network->GetCurNick() + "!" + network->GetName() + "+irc@" + GetServerName());
						item.SetAttribute("name", "You on " + network->GetName());
					}

					Write(iq, &Stanza);
					return;
				}
			}

			CXMPPStanza *pVCard = Stanza.GetChildByName("vCard");
			if (pVCard && pVCard->GetAttribute("xmlns").Equals("vcard-temp")) {
				/* vcard-temp: https://xmpp.org/extensions/xep-0054.html */
				if (!pVCard->HasAttribute("to")) {
					/* Retrieving user's own vCard */

					/* Item Not Found */
					Error("item-not-found", "cancel", "404", &Stanza);
					return;
				}
			}


		} else if (Stanza.GetAttribute("type").Equals("set")) {
			CXMPPStanza *bindStanza = Stanza.GetChildByName("bind");

			if (bindStanza) {
				bool bResource = false;
				CString sResource;

				CXMPPStanza *pResourceStanza = bindStanza->GetChildByName("resource");

				if (pResourceStanza) {
					CXMPPStanza *pStanza = pResourceStanza->GetTextChild();
					if (pStanza) {
						bResource = true;
						sResource = pStanza->GetText();
					}
				}

				if (!bResource) {
					// Generate a resource
					sResource = CString::RandomString(32).SHA256();
				}

				if (sResource.empty()) {
					/* Invalid resource*/
					Error("bad-request", "modify", "400", &Stanza);
					return;
				}

				if (((CXMPPModule*)m_pModule)->Client(*m_pUser, sResource)) {
					/* We already have a client with this resource */
					Error("conflict", "cancel", "409", &Stanza);
					return;
				}

				/* The resource is all good, lets use it */
				m_sResource = sResource;

				iq.SetAttribute("type", "result");
				CXMPPStanza& bindStanza = iq.NewChild("bind", "urn:ietf:params:xml:ns:xmpp-bind");
				CXMPPStanza& jidStanza = bindStanza.NewChild("jid");
				jidStanza.NewChild().SetText(GetJID());
#ifdef SUPPORT_RFC_3921
			} else if (Stanza.GetChildByName("session")) {
				iq.SetAttribute("type", "result");
			}
#endif
		}

		if (!iq.HasAttribute("type")) {
			iq.SetAttribute("type", "error");
			iq.NewChild("bad-request");

			DEBUG("XMPPClient unsupported iq type [" + Stanza.GetAttribute("type") + "]");
		}

		Write(iq, &Stanza);
		return;
	} else if (Stanza.GetName().Equals("message")) {
		CXMPPJID to(Stanza.GetAttribute("to"));

		// IRC interface
		if (to.IsIRC()) {
			CString targetName = to.GetIRCTarget();
			CString networkName = to.GetIRCNetwork();

			CXMPPStanza *pBody = Stanza.GetChildByName("body");
			if (pBody) {
				CXMPPStanza *pBodyText = pBody->GetTextChild();
				if (pBodyText) {
					CString body = pBodyText->GetText().c_str();

					CIRCNetwork *network = m_pUser->FindNetwork(networkName);
					if (network) {
						CMessage message;
						message.SetNick(network->GetIRCNick());
						message.SetCommand("PRIVMSG");
						message.SetParam(0, targetName);
						message.SetParam(1, body);

						network->PutIRC(message);
						network->PutUser(message);
						return;
					}
				}
			}
		}

		GetModule()->SendStanza(Stanza);
		return;
	} else if (Stanza.GetName().Equals("presence")) {
		CXMPPStanza presence("presence");

		if (!Stanza.HasAttribute("type")) {
			if (!Stanza.HasAttribute("to")) {
				CXMPPStanza *pPriority = Stanza.GetChildByName("priority");

				if (pPriority) {
					CXMPPStanza *pPriorityText = pPriority->GetTextChild();
					if (pPriorityText) {
						int priority = pPriorityText->GetText().ToInt();

						if ((priority >= -128) && (priority <= 127)) {
							m_uiPriority = priority;
						}
					}

					CXMPPStanza& priority = presence.NewChild("priority");
					priority.NewChild().SetText(CString(GetPriority()));
				}
			} else {
				// channel join
				CXMPPJID to(Stanza.GetAttribute("to"));

				if (!to.IsLocal(*GetModule())) {
					return; // ignore
				}
				if (to.GetResource().empty()) {
					Error("jid-malformed", "modify", "400", &Stanza);
					return;
				}

				CXMPPStanza *pX = Stanza.GetChildByName("x");
				if (pX && pX->GetAttribute("xmlns").Equals("http://jabber.org/protocol/muc")) {
					// TODO: Broadcast to any other XMPP clients in this room

					if (to.IsIRCChannel()) {
						Error("jid-malformed", "modify", "400", &Stanza);
						return;
					}

					CIRCNetwork *network = m_pUser->FindNetwork(to.GetIRCNetwork());
					if (!network) {
						Error("item-not-found", "cancel", "404", &Stanza);
						return;
					}

					CChan *channel = network->FindChan(to.GetIRCTarget());
					if (!channel) {
						Error("item-not-found", "cancel", "404", &Stanza);
						return;
					}

					std::map<CString, CNick> nicks = channel->GetNicks();
					for (std::map<CString, CNick>::const_iterator iter = nicks.begin(); iter != nicks.end(); ++iter) {
						const CNick &nick = iter->second;

						CXMPPJID from = to;
						from.SetResource(nick.GetNick());

						CXMPPStanza presence("presence");
						presence.SetAttribute("id", "znc_" + CString::RandomString(8));
						presence.SetAttribute("from", from.ToString());
						CXMPPStanza &x = presence.NewChild("x", "http://jabber.org/protocol/muc#user");
						CXMPPStanza &item = x.NewChild("item");
						// TODO: check permissions
						item.SetAttribute("affiliation", "member");
						item.SetAttribute("role", "participant");

						Write(presence, &Stanza);
					}

					// User's own presence
					presence.SetAttribute("from", to.ToString());
					CXMPPStanza &x = presence.NewChild("x", "http://jabber.org/protocol/muc#user");
					CXMPPStanza &item = x.NewChild("item");
					item.SetAttribute("affiliation", "member");
					item.SetAttribute("role", "participant");

					Write(presence, &Stanza);

					m_sChannels.push_back(to.GetUser());
					return;
				}
			}
		} else if (Stanza.GetAttribute("type").Equals("unavailable")) {
			presence.NewChild("unavailable");
		} else if (Stanza.GetAttribute("type").Equals("available")) {
			presence.NewChild("available");
		}

		Write(presence, &Stanza);
		return;
	}

	DEBUG("XMPPClient unsupported stanza [" << Stanza.GetName() << "]");
}

