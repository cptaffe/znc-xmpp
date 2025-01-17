/*
 * Copyright (C) 2004-2012  See the AUTHORS file for details.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <znc/IRCNetwork.h>
#include <znc/Chan.h>
#include <znc/Utils.h>

#include "Client.h"
#include "xmpp.h"

#define SUPPORT_RFC_3921

class CXMPPBufLine : public CBufLine {
public:
	CXMPPBufLine(const CBufLine &line) : CBufLine(line) {}
	CMessage GetMessage() const { return m_Message; }
};

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

void CXMPPClient::Error(const CString &tag, const CString &type, const CString &code, const CXMPPStanza *pStanza, const CString &text) {
	CXMPPStanza parent("iq");
	if (pStanza) {
		parent.SetName(pStanza->GetName());
		parent.SetAttribute("xmlns", pStanza->GetAttribute("xmlns"));
	}
	parent.SetAttribute("to", GetJID());
	parent.SetAttribute("type", "error");
	CXMPPStanza &error = parent.NewChild("error");
	if (!code.empty()) {
		error.SetAttribute("code", code);
	}
	error.SetAttribute("type", type);
	error.NewChild(tag, "urn:ietf:params:xml:ns:xmpp-stanzas");
	if (!text.empty()) {
		CXMPPStanza &txt = error.NewChild("text", "urn:ietf:params:xml:ns:xmpp-streams");
		txt.SetAttribute("xml:lang", "en-US");
		txt.NewChild().SetText(text);
	}
	Write(parent, pStanza);
}

void CXMPPClient::Presence(const CXMPPJID &from, const CString &type, const CString &status,  const CXMPPStanza *pStanza) {
	CXMPPStanza presence("presence");
	presence.SetAttribute("id", "znc_" + CString::RandomString(8));
	presence.SetAttribute("from", from.ToString());
	if (!type.empty())
		presence.SetAttribute("type", type);
	if (!status.empty())
		presence.NewChild("status").NewChild().SetText(status);
	/* Hash stolen from iChat vCard update */
	presence.NewChild("x", "vcard-temp:x:update").NewChild("photo").NewChild().SetText("341f80f531fbce7a0441b5983e2ebf9fa84868d0");

	Write(presence, pStanza);
}

void CXMPPClient::ChannelPresence(const CXMPPJID &from, const CXMPPJID &jid, const CString &type, const CString &status, const std::vector<CString> &codes,  const CXMPPStanza *pStanza) {
	CXMPPStanza presence("presence");
	presence.SetAttribute("id", "znc_" + CString::RandomString(8));
	presence.SetAttribute("from", from.ToString());
	if (!type.empty())
		presence.SetAttribute("type", type);
	if (!status.empty())
		presence.NewChild("status").NewChild().SetText(status);
	CXMPPStanza &x = presence.NewChild("x", "http://jabber.org/protocol/muc#user");
	CXMPPStanza &item = x.NewChild("item");
	// TODO: check permissions
	if (!jid.Equals(GetJID()))
		item.SetAttribute("jid", jid.ToString());
	item.SetAttribute("affiliation", "member");
	item.SetAttribute("role", "participant");
	for (const auto& it : codes) {
		presence.NewChild("status").SetAttribute("code", it);
	}

	Write(presence, pStanza);
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

void AddDelay(CXMPPStanza &in, CString from, timeval t) {
	CXMPPStanza &delay = in.NewChild("delay", "urn:xmpp:delay");
	delay.SetAttribute("from", from);
	delay.SetAttribute("stamp", CUtils::FormatTime(t, "%Y-%m-%dT%H:%M:%SZ", "UTC"));
	CXMPPStanza &x = in.NewChild("x", "jabber:x:delay");
	x.SetAttribute("from", from);
	x.SetAttribute("stamp", CUtils::FormatTime(t, "%Y%m%dT%H:%M:%S", "UTC"));
}

void AddDelay(CXMPPStanza &in, CString from, time_t t) {
	AddDelay(in, from, timeval{.tv_sec = t});
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
					sUsername = pUsername->GetText();
					CString sPassword = pPassword->GetText();

					CUser *pUser = CZNC::Get().FindUser(sUsername);

					if (pUser && pUser->CheckPass(sPassword)) {
						iq.SetAttribute("type", "result");
						Write(iq);

						m_pUser = pUser;
						if (pResource) {
							m_sResource = pResource->GetText();
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
				if (pQuery->GetAttribute("xmlns").Equals("http://jabber.org/protocol/disco#items")) {
					if (Stanza.GetAttribute("to").Equals(GetServerName())) {
						iq.SetAttribute("type", "result");
						CXMPPStanza &query = iq.NewChild("query", "http://jabber.org/protocol/disco#items");

						/* List directories as separate servers */
						CXMPPStanza &item = query.NewChild("item");
						item.SetAttribute("jid", "channels." + GetServerName());
						item.SetAttribute("name", "Directory of IRC Channels");
						item = query.NewChild("item");
						item.SetAttribute("jid", "users." + GetServerName());
						item.SetAttribute("name", "Directory of IRC Users");

						Write(iq, &Stanza);
						return;
					}

					/* User Directory */
					if (Stanza.GetAttribute("to").Equals("users." + GetServerName())) {
						iq.SetAttribute("type", "result");
						CXMPPStanza &query = iq.NewChild("query", "http://jabber.org/protocol/disco#items");

						// Enumerate networks
						const std::vector<CIRCNetwork*> &networks = m_pUser->GetNetworks();
						for (const auto& network : networks) {
							if (!network->IsIRCConnected())
								continue;

							// Enumerate channels
							std::set<CString> unicks;
							const std::vector<CChan*> &channels = network->GetChans();
							for (const auto &channel : channels) {
								if (!channel->IsOn())
									continue;

								const std::map<CString, CNick> &nicks = channel->GetNicks();
								for (const auto &entry : nicks) {
									unicks.insert(entry.second.GetNick());
								}
							}

							// Present each unique nick on the network as a user
							for (const auto &nick : unicks) {
								// JID grammar: https://xmpp.org/extensions/xep-0029.html#sect-idm45406366945648
								CString jid = nick + "!" + network->GetName() + "+irc@" + GetServerName();
								CString name = nick + " on " + network->GetName();

								CXMPPStanza &item = query.NewChild("item");
								item.SetAttribute("jid", jid);
								item.SetAttribute("name", name);
							}
						}

						Write(iq, &Stanza);
						return;
					}

					/* Channel Directory */
					if (Stanza.GetAttribute("to").Equals("channels." + GetServerName())) {
						iq.SetAttribute("type", "result");
						CXMPPStanza &query = iq.NewChild("query", "http://jabber.org/protocol/disco#items");

						// Enumerate networks
						const std::vector<CIRCNetwork*> &networks = m_pUser->GetNetworks();
						for (const auto& network : networks) {
							if (!network->IsIRCConnected())
								continue;

							// Enumerate channels
							const std::vector<CChan*> &channels = network->GetChans();
							for (const auto &channel : channels) {
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

				}

				if (pQuery->GetAttribute("xmlns").Equals("http://jabber.org/protocol/disco#info")) {
					if (Stanza.GetAttribute("to").Equals(GetServerName())) {
						iq.SetAttribute("type", "result");
						CXMPPStanza &query = iq.NewChild("query", "http://jabber.org/protocol/disco#info");
						/* XMPP Server */
						CXMPPStanza &identity1 = query.NewChild("identity");
						identity1.SetAttribute("category", "server");
						identity1.SetAttribute("type", "im");
						identity1.SetAttribute("name", "XMPP ZNC Module");
						/* IRC Gateway */
						CXMPPStanza identity2 = query.NewChild("identity");
						identity2.SetAttribute("category", "gateway");
						identity2.SetAttribute("type", "irc");
						identity2.SetAttribute("name", "XMPP ZNC Module");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/disco#info");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/disco#items");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/muc");
						query.NewChild("feature").SetAttribute("var", "vcard-temp");
						query.NewChild("feature").SetAttribute("var", "jabber:iq:search");
						query.NewChild("feature").SetAttribute("var", "jabber:iq:time");
						query.NewChild("feature").SetAttribute("var", "jabber:iq:version");

						Write(iq, &Stanza);
						return;
					}

					/* User Directory */
					if (Stanza.GetAttribute("to").Equals("users." + GetServerName())) {
						iq.SetAttribute("type", "result");
						CXMPPStanza &query = iq.NewChild("query", "http://jabber.org/protocol/disco#info");
						CXMPPStanza &identity = query.NewChild("identity");
						identity.SetAttribute("category", "directory");
						identity.SetAttribute("type", "user");
						identity.SetAttribute("name", "IRC Users");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/disco#info");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/disco#items");
						query.NewChild("feature").SetAttribute("var", "vcard-temp");
						query.NewChild("feature").SetAttribute("var", "jabber:iq:search");
						query.NewChild("feature").SetAttribute("var", "jabber:iq:time");
						query.NewChild("feature").SetAttribute("var", "jabber:iq:version");

						Write(iq, &Stanza);
						return;
					}

					/* Chatroom Directory */
					if (Stanza.GetAttribute("to").Equals("channels." + GetServerName())) {
						iq.SetAttribute("type", "result");
						CXMPPStanza &query = iq.NewChild("query", "http://jabber.org/protocol/disco#info");
						CXMPPStanza &identity1 = query.NewChild("identity");
						identity1.SetAttribute("category", "conference");
						identity1.SetAttribute("type", "text");
						identity1.SetAttribute("name", "IRC Channels");
						CXMPPStanza &identity2 = query.NewChild("identity");
						identity2.SetAttribute("category", "directory");
						identity2.SetAttribute("type", "chatroom");
						identity2.SetAttribute("name", "IRC Channels");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/disco#info");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/disco#items");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/muc");
						query.NewChild("feature").SetAttribute("var", "jabber:iq:search");
						query.NewChild("feature").SetAttribute("var", "jabber:iq:time");
						query.NewChild("feature").SetAttribute("var", "jabber:iq:version");

						Write(iq, &Stanza);
						return;
					}

					if (!to.IsLocal(*GetModule())) {
						Error("item-not-found", "cancel", "404", &Stanza, "Unknown server");
						return;
					}

					/* Info on a user */
					if (to.IsIRCUser()) {
						CIRCNetwork *network = m_pUser->FindNetwork(to.GetIRCNetwork());
						if (!network) {
							Error("item-not-found", "cancel", "404", &Stanza, "Unknown IRC network");
							return;
						}

						/* Traverse all channels this client is connected to on this network to confirm the user exists */
						const CNick *nick;
						for (const auto &entry : GetChannels()) {
							const CXMPPJID &jid = entry.second.GetJID();
							const CChan *channel = entry.second.GetChannel();

							if (!channel) {
								Error("item-not-found", "cancel", "404", &Stanza, "Unknown IRC channel in network");
								return;
							}

							const CString user = to.GetIRCUser();
							nick = channel->FindNick(user);
							if (nick)
								break;
						}
						if (!nick) {
							Error("item-not-found", "cancel", "404", &Stanza, "Unknown IRC nick " + to.GetIRCUser() + " in network " + to.GetIRCNetwork());
							return;
						}

						iq.SetAttribute("type", "result");
						CXMPPStanza &query = iq.NewChild("query", "http://jabber.org/protocol/disco#info");
						CXMPPStanza &identity = query.NewChild("identity");
						identity.SetAttribute("category", "account");
						identity.SetAttribute("type", "registered");
						identity = query.NewChild("identity");
						identity.SetAttribute("category", "pubsub");
						identity.SetAttribute("type", "pep");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/disco#info");
						query.NewChild("feature").SetAttribute("var", "vcard-temp");
						query.NewChild("feature").SetAttribute("var", "urn:xmpp:tmp:profile");
						/* Personal Eventing Protocol: https://xmpp.org/extensions/xep-0163.html */
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/pubsub#access-presence");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/pubsub#auto-create");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/pubsub#auto-subscribe");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/pubsub#config-node");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/pubsub#create-and-configure");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/pubsub#create-nodes");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/pubsub#filtered-notifications");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/pubsub#persistent-items");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/pubsub#publish");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/pubsub#retrieve-items");
						query.NewChild("feature").SetAttribute("var", "http://jabber.org/protocol/pubsub#subscribe");

						Write(iq, &Stanza);
						return;
					}

					/* MUC: Querying Room Information: https://xmpp.org/extensions/xep-0045.html#disco-roominfo */
					if (to.IsIRCChannel()) {
						CIRCNetwork *network = m_pUser->FindNetwork(to.GetIRCNetwork());
						if (network) {
							CChan *channel = network->FindChan(to.GetIRCChannel());
							if (channel) {
								iq.SetAttribute("type", "result");
								CXMPPStanza &query = iq.NewChild("query", "http://jabber.org/protocol/disco#info");
								CXMPPStanza &identity = query.NewChild("identity");
								identity.SetAttribute("category", "conference");
								identity.SetAttribute("type", "text");
								identity.SetAttribute("name", channel->GetName() + " on " + network->GetName());
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

					Error("item-not-found", "cancel", "404", &Stanza, "Unknown entity, not this server or an IRC channel or nick");
					return;
				}

				/* Roster Get: https://xmpp.org/rfcs/rfc6121.html#roster-syntax-actions-get */
				if (pQuery->GetAttribute("xmlns").Equals("jabber:iq:roster")) {
					iq.SetAttribute("type", "result");
					CXMPPStanza &query = iq.NewChild("query", "jabber:iq:roster");

					// Enumerate networks
					const std::vector<CIRCNetwork*> &networks = m_pUser->GetNetworks();
					for (const auto &network : networks) {
						if (!network->IsIRCConnected())
							continue;

						// Add yourself
						CXMPPStanza &item = query.NewChild("item");
						item.SetAttribute("subscription", "to");
						item.SetAttribute("jid", network->GetCurNick() + "!" + network->GetName() + "+irc@" + GetServerName());
						item.SetAttribute("name", m_pUser->GetRealName());
						item.SetAttribute("group", network->GetName());
					}

					Write(iq, &Stanza);
					return;
				}
			}

			CXMPPStanza *pVCard = Stanza.GetChildByName("vCard", "vcard-temp");
			if (pVCard) {
				/* vcard-temp: https://xmpp.org/extensions/xep-0054.html */
				if (!pVCard->HasAttribute("to")) {
					/* Retrieving user's own vCard */

					iq.SetAttribute("type", "result");
					CXMPPStanza &vCard = iq.NewChild("vCard", "vcard-temp");
					vCard.NewChild("NICKNAME").NewChild().SetText(m_pUser->GetNick());
					vCard.NewChild("FN").NewChild().SetText(m_pUser->GetRealName());

					Write(iq, &Stanza);
					return;
				}

				// Another user's vCard
				CXMPPJID to(pVCard->GetAttribute("to"));
				if (to.IsIRCUser()) {
					iq.SetAttribute("type", "result");
					CXMPPStanza &vCard = iq.NewChild("vCard", "vcard-temp");
					vCard.NewChild("NICKNAME").NewChild().SetText(to.GetIRCUser());
					vCard.NewChild("FN").NewChild().SetText(to.GetIRCUser() + " on " + to.GetIRCNetwork());

					Write(iq, &Stanza);
					return;
				}

				/* Item Not Found */
				Error("item-not-found", "cancel", "404", &Stanza, "vCard can only be fetched for this user or an IRC nick");
				return;
			}


		} else if (Stanza.GetAttribute("type").Equals("set")) {
			CXMPPStanza *pVCard = Stanza.GetChildByName("vCard", "vcard-temp");
			if (pVCard) {
				/* vcard-temp: https://xmpp.org/extensions/xep-0054.html */
				if (!pVCard->HasAttribute("to")) {
					/* Updating user's own vCard */
					iq.SetAttribute("type", "result");
					// TODO: Store vCard
					Write(iq, &Stanza);
					return;
				}

				/* Not Allowed */
				Error("not-allowed", "cancel", "405", &Stanza);
				return;
			}

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
			CString targetName;
			if (to.IsIRCUser())
				targetName = to.GetIRCUser();
			else
				targetName = to.GetIRCChannel();
			CString networkName = to.GetIRCNetwork();

			CXMPPStanza *pBody = Stanza.GetChildByName("body");
			if (pBody) {
				CString body = pBody->GetAllText();
				CIRCNetwork *network = m_pUser->FindNetwork(networkName);
				if (network) {
					CMessage message;
					message.SetNick(network->GetIRCNick());
					message.SetCommand("PRIVMSG");
					message.SetParam(0, targetName);
					message.SetParam(1, body);

					network->PutIRC(message);
					network->PutUser(message);

					if (Stanza.GetAttribute("type").Equals("groupchat")) {
						CXMPPStanza message("message");
						message.SetAttribute("type", "groupchat");
						CXMPPJID nick = m_mChannels[to.GetUser()].GetJID();
						if (!nick.IsBlank()) {
							message.SetAttribute("from", nick.ToString());
						}
						message.SetAttribute("to", to.ToString());
						message.NewChild("body").NewChild().SetText(body);

						Write(message, &Stanza);
					}

					return;
				}
			}
		}

		GetModule()->SendStanza(Stanza);
		return;
	} else if (Stanza.GetName().Equals("presence")) {
		CXMPPStanza presence("presence");

		if (!Stanza.HasAttribute("type")) {
			if (!Stanza.HasAttribute("to")) {
				/* Initial presence */
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
				CXMPPStanza *pXVCard = Stanza.GetChildByName("x", "vcard-temp:x:update");
				if (pXVCard) {
					presence.NewChild("x", "vcard-temp:x:update");
				}

				Write(presence, &Stanza);

				/* Invite to all channels */
				for (const auto &network : m_pUser->GetNetworks()) {
					for (const auto &channel : network->GetChans()) {
						CXMPPStanza message("message");
						message.SetAttribute("from", channel->GetName() + "!" + network->GetName() + "+irc@" + GetServerName());
						message.SetAttribute("id", "znc_" + CString::RandomString(8));
						CXMPPStanza &invite = message.NewChild("x", "http://jabber.org/protocol/muc#user").NewChild("invite");
						invite.SetAttribute("from", GetServerName());
						invite.NewChild("reason").NewChild().SetText(m_pUser->GetNick() + " is joined to " + channel->GetName() + " on " + network->GetName());

						Write(message);
					}
				}
				return;
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

				CXMPPStanza *pX = Stanza.GetChildByName("x", "http://jabber.org/protocol/muc");
				if (pX) {
					// TODO: Broadcast to any other XMPP clients in this room
					// TODO: we need a per-client channel list

					if (!(to.IsLocal(*GetModule()) && to.IsIRCChannel())) {
						Error("item-not-found", "cancel", "404", &Stanza, "Channel is not on this server or is not an IRC channel");
						return;
					}

					CIRCNetwork *network = m_pUser->FindNetwork(to.GetIRCNetwork());
					if (!network) {
						Error("item-not-found", "cancel", "404", &Stanza, "Unknown IRC network");
						return;
					}

					// Room history
					int maxStanzas = 25;
					CXMPPStanza *pHistory = pX->GetChildByName("history");
					if (pHistory && pHistory->HasAttribute("maxstanzas")) {
						maxStanzas = pHistory->GetAttribute("maxstanzas").ToInt();
					}

					CChan *channel = network->FindChan(to.GetIRCChannel());
					if (!channel) {
						// Add the channel to the network
						channel = new CChan(to.GetIRCChannel(), network, false);
						network->AddChan(channel);
					}
					if (channel->IsDisabled()) {
						Error("item-not-found", "cancel", "404", &Stanza, "Unknown IRC channel");
						return;
					}
					if (!channel->IsOn()) {
						// Join the channel
						std::set<CChan *> joins{channel};
						network->JoinChans(joins);

						DEBUG("XMPPClient finish join to " + channel->GetName() + " on " + network->GetName() + " in callback");
						m_mChannels.emplace(to.GetUser(), CXMPPChannel(to, channel, maxStanzas));
						return;
					}

					JoinChannel(channel, to, maxStanzas);
					return;
				}
			}
		} else if (Stanza.GetAttribute("type").Equals("unavailable")) {
			if (!Stanza.HasAttribute("to")) {
				presence.NewChild("unavailable");
				Write(presence, &Stanza);
				return;
			} else {
				/* An occupant exits a room by sending presence of type "unavailable" to its current <room@service/nick>. */
				CXMPPJID to(Stanza.GetAttribute("to"));
				if (!(to.IsLocal(*GetModule()) && to.IsIRCChannel())) {
					/* Unknown, ignore */
					return;
				}
				CXMPPJID jid = m_mChannels[to.GetUser()].GetJID();
				if (jid.IsBlank() || !jid.Equals(to)) {
					/* Not joined, ignore */
					return;
				}

				m_mChannels.erase(to.GetUser());
				CXMPPJID from = to;
				to.SetResource("");
				ChannelPresence(from, jid, "unavailable");
				return;
			}
		} else if (Stanza.GetAttribute("type").Equals("available")) {
			presence.NewChild("available");
		}

		Write(presence, &Stanza);
		return;
	}

	DEBUG("XMPPClient unsupported stanza [" << Stanza.GetName() << "]");
}

// TODO: Support multiple channels so nick presence is de-duplicated when logging back in.
void CXMPPClient::JoinChannel(CChan *const &channel, const CXMPPJID &to, int maxStanzas) {
	const CIRCNetwork *network = channel->GetNetwork();
	DEBUG("XMPPClient sending join to " + channel->GetName() + " on " + network->GetName());
	const std::map<CString, CNick> &nicks = channel->GetNicks();
	for (const auto &entry : nicks) {
		const CNick &nick = entry.second;

		CXMPPJID from = to;
		from.SetResource(nick.GetNick());
		CXMPPJID jid(nick.GetNick() + "!" + network->GetName() + "+irc", GetServerName());
		ChannelPresence(from, jid);
	}

	// User's own presence
	ChannelPresence(to, GetJID(), "", "", {"100", "110"});

	// Traverse back through time, finding messages
	const CBuffer &buffer = channel->GetBuffer();
	if (buffer.Size()) {
		std::deque<CXMPPBufLine> history;
		for (size_t i = buffer.Size(); i-- > 0;) {
			const CXMPPBufLine &line = CXMPPBufLine(buffer.GetBufLine(i));
			if (!line.GetCommand().Equals("PRIVMSG"))
				continue;

			history.push_front(line);
			if (history.size() == maxStanzas)
				break;
		}

		// Traverse forward through time, writing messages
		for (const auto &line : history) {
			const CMessage &msg = line.GetMessage();

			CXMPPJID from(to.GetUser(), to.GetDomain(), msg.GetNick().GetNick());
			CXMPPJID channelJID(to.GetUser(), GetServerName());
			CXMPPStanza message("message");
			message.SetAttribute("id", "znc_" + CString::RandomString(8));
			message.SetAttribute("from", from.ToString());
			message.SetAttribute("type", "groupchat");
			message.NewChild("body").NewChild().SetText(line.GetText());
			AddDelay(message, channelJID.ToString(), msg.GetTime());
			Write(message);
		}
	}

	// Room subject
	CString topic = channel->GetTopic();
	if (!topic.empty()) {
		CXMPPJID owner(to.GetUser(), to.GetDomain(), CNick(channel->GetTopicOwner()).GetNick());
		CXMPPJID channelJID(to.GetUser(), GetServerName());
		CXMPPStanza message("message");
		message.SetAttribute("id", "znc_" + CString::RandomString(8));
		message.SetAttribute("from", owner.ToString());
		message.SetAttribute("type", "groupchat");
		message.NewChild("subject").NewChild().SetText(topic);
		AddDelay(message, channelJID.ToString(), (time_t)channel->GetTopicDate());
		Write(message);
	}

	m_mChannels.emplace(to.GetUser(), CXMPPChannel(to, channel));

	// Finally, send the non-channel presence of channel members
	for (const auto &entry : nicks) {
		const CNick &nick = entry.second;

		CXMPPJID from(nick.GetNick() + "!" + network->GetName() + "+irc", GetServerName());
		Presence(from);
	}
}
