/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <base/system.h>
#include <base/hash_ctxt.h>

#include <engine/console.h>

#include "netban.h"
#include "network.h"
#include "config.h"

#include <engine/shared/protocol.h>

static SECURITY_TOKEN ToSecurityToken(const unsigned char *pData)
{
	return (int)pData[0] | (pData[1] << 8) | (pData[2] << 16) | (pData[3] << 24);
}

bool CNetServer::Open(NETADDR BindAddr, CConfig *pConfig, IConsole *pConsole, IEngine *pEngine, CNetBan *pNetBan,
	int MaxClients, int MaxClientsPerIP, NETFUNC_NEWCLIENT pfnNewClient, NETFUNC_DELCLIENT pfnDelClient,
	NETFUNC_CLIENTREJOIN pfnClientRejoin, NETFUNC_CLIENTCANCLOSE pfnClientCanClose, void *pUser)
{
	// zero out the whole structure
	mem_zero(this, sizeof(*this));

	// open socket
	NETSOCKET Socket = net_udp_create(BindAddr, 0);
	if(!Socket)
		return false;

	NETSOCKET SocketTwo = 0;
	if (pConfig->m_SvPortTwo != 0)
	{
		BindAddr.port = pConfig->m_SvPortTwo;
		SocketTwo = net_udp_create(BindAddr, 0);
		if(!SocketTwo)
			return false;
	}

	// init
	m_pNetBan = pNetBan;
	Init(Socket, SocketTwo, pConfig, pConsole, pEngine);

	m_TokenManager.Init(this);
	m_TokenCache.Init(this, &m_TokenManager);

	m_NumClients = 0;
	SetMaxClients(MaxClients);
	SetMaxClientsPerIP(MaxClientsPerIP);

	for(int i = 0; i < NET_MAX_CLIENTS; i++)
		m_aSlots[i].m_Connection.Init(this, true);

	m_pfnNewClient = pfnNewClient;
	m_pfnDelClient = pfnDelClient;
	m_pfnClientRejoin = pfnClientRejoin;
	m_pfnClientCanClose = pfnClientCanClose;
	m_UserPtr = pUser;

	// F-DDrace
	m_ShutdownMessage[0] = '\0';
	secure_random_fill(m_SecurityTokenSeed, sizeof(m_SecurityTokenSeed));

	return true;
}

void CNetServer::Close()
{
	for(int i = 0; i < NET_MAX_CLIENTS; i++)
		Drop(i, m_ShutdownMessage[0] != '\0' ? m_ShutdownMessage : "Server shutdown", false, true);

	Shutdown();
}

void CNetServer::Drop(int ClientID, const char *pReason, bool Banned, bool Shutdown)
{
	if(ClientID < 0 || ClientID >= NET_MAX_CLIENTS)
		return;

	if(m_pfnDelClient)
		m_pfnDelClient(ClientID, pReason, m_UserPtr);

	char aBuf[256];
	str_copy(aBuf, pReason, sizeof(aBuf));
	if (Banned && Config()->m_SvDiscordURL[0])
		str_format(aBuf, sizeof(aBuf), "%s - Appeal: %s", pReason, Config()->m_SvDiscordURL);

	m_aSlots[ClientID].m_Connection.Disconnect(aBuf, Shutdown);
	m_NumClients--;
}

int CNetServer::Update()
{
	for(int i = 0; i < NET_MAX_CLIENTS; i++)
	{
		if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
			continue;

		m_aSlots[i].m_Connection.Update();
		if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_ERROR &&
			(!m_aSlots[i].m_Connection.m_TimeoutProtected ||
			 !m_aSlots[i].m_Connection.m_TimeoutSituation))
		{
			Drop(i, m_aSlots[i].m_Connection.ErrorString());
		}
	}

	m_TokenManager.Update();
	for (int i = 0; i < NUM_SOCKETS; i++)
		m_TokenCache.Update(i);

	return 0;
}

SECURITY_TOKEN CNetServer::GetGlobalToken()
{
	static NETADDR NullAddr = {0};
	return GetSecurityToken(NullAddr);
}

SECURITY_TOKEN CNetServer::GetSecurityToken(const NETADDR &Addr)
{
	SHA256_CTX Sha256;
	sha256_init(&Sha256);
	sha256_update(&Sha256, (unsigned char*)m_SecurityTokenSeed, sizeof(m_SecurityTokenSeed));
	sha256_update(&Sha256, (unsigned char*)&Addr, sizeof(Addr));

	SECURITY_TOKEN SecurityToken = ToSecurityToken(sha256_finish(&Sha256).data);

	if (SecurityToken == NET_SECURITY_TOKEN_UNKNOWN ||
		SecurityToken == NET_SECURITY_TOKEN_UNSUPPORTED)
			SecurityToken = 1;

	return SecurityToken;
}

bool CNetServer::GetSevendown(const NETADDR *pAddr, CNetPacketConstruct *pPacket, unsigned char *pBuffer)
{
	for(int i = 0; i < NET_MAX_CLIENTS; i++)
	{
		if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
			continue;

		if(net_addr_comp(m_aSlots[i].m_Connection.PeerAddress(), pAddr, true) == 0)
			return m_aSlots[i].m_Connection.m_Sevendown;
	}
	
	// connless packets use this now: *pSevendown = (pBuffer[0] & 0x3) != 1;
	/*int Flags = pBuffer[0]>>4;
	if (pPacket->m_Flags&2) Flags |= NET_PACKETFLAG_CONNLESS;
	if (Flags&NET_PACKETFLAG_CONNLESS)
	{
		pPacket->m_Flags = Flags;
		return true;
	}

	if (pPacket->m_Flags&NET_PACKETFLAG_CONNLESS && pBuffer[0] != 0xff)
		return false;*/

	return !(pPacket->m_Flags&1);
}

/*
	TODO: chopp up this function into smaller working parts
*/
int CNetServer::Recv(CNetChunk *pChunk, TOKEN *pResponseToken, bool *pSevendown, int Socket)
{
	while(1)
	{
		// check for a chunk
		if(m_RecvUnpacker.IsActive() && m_RecvUnpacker.FetchChunk(pChunk))
			return 1;

		// TODO: empty the recvinfo
		NETADDR Addr;
		int Result = UnpackPacket(&Addr, m_RecvUnpacker.m_aBuffer, &m_RecvUnpacker.m_Data, pSevendown, Socket, this);
		// no more packets for now
		if(Result > 0)
			break;

		if(!Result)
		{
			// check for bans
			char aBuf[128];
			int LastInfoQuery;
			if(NetBan() && NetBan()->IsBanned(&Addr, aBuf, sizeof(aBuf), &LastInfoQuery))
			{
				// banned, reply with a message (5 second cooldown)
				int Time = time_timestamp();
				if(LastInfoQuery + 5 < Time)
				{
					if (Config()->m_SvDiscordURL[0])
					{
						char aTemp[128];
						str_format(aTemp, sizeof(aTemp), " - Appeal: %s", Config()->m_SvDiscordURL);
						str_append(aBuf, aTemp, sizeof(aBuf));
					}

					SendControlMsg(&Addr, m_RecvUnpacker.m_Data.m_ResponseToken, 0, NET_CTRLMSG_CLOSE, aBuf, str_length(aBuf) + 1, *pSevendown, Socket, NET_SECURITY_TOKEN_UNSUPPORTED);
				}
				continue;
			}

			if (*pSevendown && !Config()->m_SvAllowSevendown)
				continue;

			if(m_RecvUnpacker.m_Data.m_Flags&NET_PACKETFLAG_CONNLESS)
			{
				if (!*pSevendown && (SECURITY_TOKEN)m_RecvUnpacker.m_Data.m_Token != GetGlobalToken())
				{
					int Accept = m_TokenManager.ProcessMessage(&Addr, &m_RecvUnpacker.m_Data, Socket);
					if (Accept <= 0)
						continue;
				}

				pChunk->m_Flags = NETSENDFLAG_CONNLESS;
				pChunk->m_ClientID = -1;
				pChunk->m_Address = Addr;
				pChunk->m_DataSize = m_RecvUnpacker.m_Data.m_DataSize;
				pChunk->m_pData = m_RecvUnpacker.m_Data.m_aChunkData;
				if (*pSevendown)
					mem_copy(pChunk->m_aExtraData, m_RecvUnpacker.m_Data.m_aExtraData, sizeof(pChunk->m_aExtraData));
				if(pResponseToken)
					*pResponseToken = m_RecvUnpacker.m_Data.m_ResponseToken;
				return 1;
			}
			else
			{
				int Slot = -1;
				// try to find matching slot
				for(int i = 0; i < NET_MAX_CLIENTS; i++)
				{
					if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
						continue;

					if(net_addr_comp(m_aSlots[i].m_Connection.PeerAddress(), &Addr, true) == 0)
					{
						Slot = i;
						break;
					}
				}

				int ControlMsg = m_RecvUnpacker.m_Data.m_aChunkData[0];
				bool Control = (m_RecvUnpacker.m_Data.m_Flags & NET_PACKETFLAG_CONTROL);
				bool AcceptConnect = false;
				SECURITY_TOKEN SecurityToken = NET_SECURITY_TOKEN_UNSUPPORTED;

				if (!*pSevendown && (Slot == -1 || Control))
				{
					int Accept = m_TokenManager.ProcessMessage(&Addr, &m_RecvUnpacker.m_Data, Socket);
					if(Accept <= 0)
						continue;
				}

				if (Control)
				{
					if (!*pSevendown)
					{
						if(ControlMsg == NET_CTRLMSG_TOKEN)
							m_TokenCache.AddToken(&Addr, m_RecvUnpacker.m_Data.m_ResponseToken, NET_TOKENFLAG_RESPONSEONLY, Socket);
						else if(ControlMsg == NET_CTRLMSG_CONNECT)
						{
							SendControlMsg(&Addr, m_RecvUnpacker.m_Data.m_ResponseToken, 0, NET_CTRLMSG_ACCEPT, 0, 0, false, Socket);
							AcceptConnect = true;
						}
					}
					else
					{
						if(ControlMsg == NET_CTRLMSG_CONNECT)
						{
							// response connection request with token
							SecurityToken = GetSecurityToken(Addr);
							SendControlMsg(&Addr, 0, 0, NET_CTRLMSG_ACCEPT, SECURITY_TOKEN_MAGIC, sizeof(SECURITY_TOKEN_MAGIC), true, Socket, SecurityToken);
						}
						else if(ControlMsg == 3) // NET_CTRLMSG_ACCEPT
						{
							SecurityToken = ToSecurityToken(&m_RecvUnpacker.m_Data.m_aChunkData[1]);
							if(SecurityToken == GetSecurityToken(Addr))
							{
								// correct token
								// try to accept client
								if(Config()->m_Debug)
									dbg_msg("security", "new client (ddnet token)");
								AcceptConnect = true;
							}
							else
							{
								// invalid token
								if(Config()->m_Debug)
									dbg_msg("security", "invalid token");
							}
						}
					}

					if (AcceptConnect)
					{
						if (Slot != -1)
						{
							// reset netconn and process rejoin
							m_aSlots[Slot].m_Connection.DirectInit(&Addr, &m_RecvUnpacker.m_Data, SecurityToken, *pSevendown, Socket);
							m_pfnClientRejoin(Slot, *pSevendown, Socket, m_UserPtr);
						}
						else
						{
							if (Connlimit(Addr))
							{
								const char LimitMsg[] = "Too many connections in a short time";
								SendControlMsg(&Addr, m_RecvUnpacker.m_Data.m_ResponseToken, 0, NET_CTRLMSG_CLOSE, LimitMsg, sizeof(LimitMsg), *pSevendown, Socket, SecurityToken);
								continue; // failed to add client
							}

							// check if there are free slots
							if(m_NumClients >= m_MaxClients)
							{
								const char FullMsg[] = "This server is full";
								SendControlMsg(&Addr, m_RecvUnpacker.m_Data.m_ResponseToken, 0, NET_CTRLMSG_CLOSE, FullMsg, sizeof(FullMsg), *pSevendown, Socket, SecurityToken);
								continue;
							}

							// only allow a specific number of players with the same ip
							int FoundAddr = 1;
							bool Continue = false;

							for(int i = 0; i < NET_MAX_CLIENTS; i++)
							{
								if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
									continue;

								if(!net_addr_comp(&Addr, m_aSlots[i].m_Connection.PeerAddress(), false))
								{
									if(FoundAddr++ >= m_MaxClientsPerIP)
									{
										char aBuf[128];
										str_format(aBuf, sizeof(aBuf), "Only %d players with the same IP are allowed", m_MaxClientsPerIP);
										SendControlMsg(&Addr, m_RecvUnpacker.m_Data.m_ResponseToken, 0, NET_CTRLMSG_CLOSE, aBuf, str_length(aBuf) + 1, *pSevendown, Socket, SecurityToken);
										Continue = true;
										break;
									}
								}
							}

							if(Continue)
								continue;

							for(int i = 0; i < NET_MAX_CLIENTS; i++)
							{
								if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
								{
									m_NumClients++;
									m_aSlots[i].m_Connection.DirectInit(&Addr, &m_RecvUnpacker.m_Data, SecurityToken, *pSevendown, Socket);
									if(m_pfnNewClient)
										m_pfnNewClient(i, *pSevendown, Socket, m_UserPtr);

									if (Config()->m_Debug)
									{
										char aAddrStr[NETADDR_MAXSTRSIZE];
										net_addr_str(&Addr, aAddrStr, sizeof(aAddrStr), true);
										dbg_msg("security", "client accepted %s", aAddrStr);
									}
									break;
								}
							}
						}
					}
				}

				if (Slot != -1)
				{
					if (m_aSlots[Slot].m_Connection.Feed(&m_RecvUnpacker.m_Data, &Addr, *pSevendown, Socket, m_pfnClientCanClose(Slot, m_UserPtr)))
					{
						if (m_RecvUnpacker.m_Data.m_DataSize)
							m_RecvUnpacker.Start(&Addr, &m_aSlots[Slot].m_Connection, Slot);
					}
				}
			}
		}
	}
	return 0;
}

int CNetServer::Send(CNetChunk *pChunk, TOKEN Token, bool Sevendown, int Socket)
{
	if(pChunk->m_Flags&NETSENDFLAG_CONNLESS)
	{
		if(pChunk->m_DataSize >= NET_MAX_PAYLOAD)
		{
			dbg_msg("netserver", "packet payload too big. %d. dropping packet", pChunk->m_DataSize);
			return -1;
		}

		if(pChunk->m_ClientID == -1)
		{
			for(int i = 0; i < NET_MAX_CLIENTS; i++)
			{
				if(m_aSlots[i].m_Connection.State() == NET_CONNSTATE_OFFLINE)
					continue;

				if(net_addr_comp(&pChunk->m_Address, m_aSlots[i].m_Connection.PeerAddress(), true) == 0)
				{
					// upgrade the packet, now that we know its recipent
					pChunk->m_ClientID = i;
					break;
				}
			}
		}

		if(Token != NET_TOKEN_NONE || Sevendown)
		{
			SendPacketConnless(&pChunk->m_Address, Token, m_TokenManager.GenerateToken(&pChunk->m_Address), pChunk->m_pData, pChunk->m_DataSize, Sevendown, Socket);
		}
		else
		{
			if(pChunk->m_ClientID == -1)
			{
				m_TokenCache.SendPacketConnless(&pChunk->m_Address, pChunk->m_pData, pChunk->m_DataSize, Socket);
			}
			else
			{
				dbg_assert(pChunk->m_ClientID >= 0, "errornous client id");
				dbg_assert(pChunk->m_ClientID < NET_MAX_CLIENTS, "errornous client id");
				dbg_assert(m_aSlots[pChunk->m_ClientID].m_Connection.State() != NET_CONNSTATE_OFFLINE, "errornous client id");

				m_aSlots[pChunk->m_ClientID].m_Connection.SendPacketConnless((const char *)pChunk->m_pData, pChunk->m_DataSize);
			}
		}
	}
	else
	{
		if (m_aSlots[pChunk->m_ClientID].m_Connection.State() == NET_CONNSTATE_DUMMY)
			return -1;

		if(pChunk->m_DataSize+NET_MAX_CHUNKHEADERSIZE >= NET_MAX_PAYLOAD)
		{
			dbg_msg("netclient", "chunk payload too big. %d. dropping chunk", pChunk->m_DataSize);
			return -1;
		}

		int Flags = 0;
		dbg_assert(pChunk->m_ClientID >= 0, "errornous client id");
		dbg_assert(pChunk->m_ClientID < NET_MAX_CLIENTS, "errornous client id");
		//dbg_assert(m_aSlots[pChunk->m_ClientID].m_Connection.State() != NET_CONNSTATE_OFFLINE, "errornous client id");

		if(pChunk->m_Flags&NETSENDFLAG_VITAL)
			Flags = NET_CHUNKFLAG_VITAL;

		if(m_aSlots[pChunk->m_ClientID].m_Connection.QueueChunk(Flags, pChunk->m_DataSize, pChunk->m_pData) == 0)
		{
			if(pChunk->m_Flags&NETSENDFLAG_FLUSH)
				m_aSlots[pChunk->m_ClientID].m_Connection.Flush();
		}
		else
		{
			// unused due to timeout protection
			//Drop(pChunk->m_ClientID, "Error sending data");
		}
	}
	return 0;
}

void CNetServer::SetMaxClients(int MaxClients)
{
	m_MaxClients = clamp(MaxClients, 1, int(NET_MAX_CLIENTS));
}

void CNetServer::SetMaxClientsPerIP(int MaxClientsPerIP)
{
	m_MaxClientsPerIP = clamp(MaxClientsPerIP, 1, int(NET_MAX_CLIENTS));
}

// F-DDrace
void CNetServer::DummyInit(int DummyID)
{
	m_aSlots[DummyID].m_Connection.DummyConnect();
	m_NumClients++;
}

void CNetServer::DummyDelete(int DummyID)
{
	m_aSlots[DummyID].m_Connection.DummyDrop();
	m_NumClients--;
}

bool CNetServer::Connlimit(NETADDR Addr)
{
	int64 Now = time_get();
	int Oldest = 0;

	for(int i = 0; i < NET_CONNLIMIT_IPS; ++i)
	{
		if(!net_addr_comp(&m_aSpamConns[i].m_Addr, &Addr, true))
		{
			if(m_aSpamConns[i].m_Time > Now - time_freq() * Config()->m_SvConnlimitTime)
			{
				if(m_aSpamConns[i].m_Conns >= Config()->m_SvConnlimit)
					return true;
			}
			else
			{
				m_aSpamConns[i].m_Time = Now;
				m_aSpamConns[i].m_Conns = 0;
			}
			m_aSpamConns[i].m_Conns++;
			return false;
		}

		if(m_aSpamConns[i].m_Time < m_aSpamConns[Oldest].m_Time)
			Oldest = i;
	}

	m_aSpamConns[Oldest].m_Addr = Addr;
	m_aSpamConns[Oldest].m_Time = Now;
	m_aSpamConns[Oldest].m_Conns = 1;
	return false;
}

bool CNetServer::SetTimedOut(int ClientID, int OrigID)
{
	if (m_aSlots[ClientID].m_Connection.State() != NET_CONNSTATE_ERROR)
		return false;

	m_aSlots[ClientID].m_Connection.SetTimedOut(ClientAddr(OrigID), m_aSlots[OrigID].m_Connection.SeqSequence(), m_aSlots[OrigID].m_Connection.AckSequence(), m_aSlots[OrigID].m_Connection.Token(), m_aSlots[OrigID].m_Connection.ResendBuffer(), m_aSlots[OrigID].m_Connection.PeerToken(), m_aSlots[OrigID].m_Connection.m_Sevendown, m_aSlots[OrigID].m_Connection.m_SecurityToken);
	m_aSlots[OrigID].m_Connection.Reset();
	m_NumClients--;
	return true;
}

void CNetServer::SetTimeoutProtected(int ClientID)
{
	m_aSlots[ClientID].m_Connection.m_TimeoutProtected = true;
}

int CNetServer::ResetErrorString(int ClientID)
{
	m_aSlots[ClientID].m_Connection.ResetErrorString();
	return 0;
}

const char *CNetServer::ErrorString(int ClientID)
{
	return m_aSlots[ClientID].m_Connection.ErrorString();
}
