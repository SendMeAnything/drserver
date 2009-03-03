#include "struct.h"
#include "servertable.h"
#include "usertable.h"
#include "recvmsg.h"
#include "packed.h"
#include "server.h"
#include "monitor.h"
#include "proxy.h"
#include "mylog.h"
#include "dr_agent_structures.h"
#include "DrServerManager.h"
// 011106 KBS
#include "RMTable.h"
//#include "RMDefine.h"
//< CSD-030322
#include "UserManager.h"
extern CUserManager g_mgrUser;
//> CSD-030322

void OnConnectUser(DWORD dwConnectionIndex);
void OnConnectServer(DWORD dwConnectionIndex);

char txt[512];
char szMsg[MM_MAX_PACKET_SIZE];

// 011106 KBS
extern void RMProc(DWORD dwConnectionIndex, char* pMsg, DWORD dwLength);

// Added by chan78 at 2000/12/17
bool RequestClearPayTable( DWORD dwConnectionIndex )
{
	// ���� �ش� AGENT�� ������ ����.
	LP_SERVER_DATA pServerData = g_pServerTable->GetServerData( dwConnectionIndex );

	if( !pServerData )
	{
		return false;
	}
	if( pServerData->dwServerType != SERVER_TYPE_AGENT )
	{
		return false;
	}

	// ó���� DB Demon�� ����
	LP_SERVER_DATA pDBDemon = g_pServerTable->GetServerListHead();
	DWORD dwTargetDBDemonCI = 0;
	for(; pDBDemon; pDBDemon = pDBDemon->pNextServerData )
	{
		if( pDBDemon->dwConnectionIndex && (pDBDemon->dwServerType == SERVER_TYPE_DB) )
		{
			dwTargetDBDemonCI = pDBDemon->dwConnectionIndex;
			break;
		}
	}

	if( dwTargetDBDemonCI == 0 )
	{
		return false;
	}

	// ��Ŷ����
	char szMsg[64+1];

	szMsg[0] = (BYTE)PTCL_ORDER_TO_CLEAR_PAY_TABLE;
	memcpy( szMsg+1, &pServerData->wPort, 2 );

	if( !g_pServerTable->Send( dwTargetDBDemonCI, szMsg, 1+2 ) )
	{
		MyLog( LOG_FATAL, "RequestClearPayTable() :: Failed to Send Packet(TargetCI:%d, AgentPort:%d)", dwTargetDBDemonCI, pServerData->wPort );
		return false;
	}

	// ���������� ����.
	return true;
}

void __stdcall OnAcceptUser(DWORD dwConnectionIndex)
{
	char msg[23];

	g_pProxy->dwTotalLogUser++;
	
	LP_SERVER_DATA pToAssign = g_pServerTable->GetAssignableAgentServer();
	// Max user check per Set
	if (g_pServerTable->GetNumOfUsersInServerSet() > g_pProxy->dwMaxUser)
	{
		g_pProxy->bLimitMaxUser = 0;
	}
	else g_pProxy->bLimitMaxUser = 1;
	// if proxy limit login try
	if (g_pProxy->bLimitLoginTryPerSec)
	{
		g_pProxy->bTryLoginThisSec++;
		if (g_pProxy->bLimitLoginTryPerSec < g_pProxy->bTryLoginThisSec)
			g_pProxy->bLimit = 0;
		else g_pProxy->bLimit = 1;
	} 
	else g_pProxy->bLimit = 1;
	
	if (!pToAssign || !g_pServerTable->IsUserAcceptAllowed() || g_pProxy->bLimit == 0 || g_pProxy->bLimitMaxUser == 0)
	{
		t_header buffer;
		if (g_pProxy->bLimit == 0 && g_pServerTable->IsUserAcceptAllowed() )
			buffer.type = 13005;
		else if (g_pProxy->bLimitMaxUser == 0 && g_pServerTable->IsUserAcceptAllowed() )
			buffer.type = 13005;
		else
			buffer.type = 10174;
		buffer.size = 0;

		// ���� ������ ������ ������ ������.
		g_pINet->SendToUser(dwConnectionIndex,(char*)&buffer,5,FLAG_SEND_NOT_ENCRYPTION);

		if ( g_pServerTable->IsUserAcceptAllowed() )
		{
//			MyLog( LOG_IMPORTANT, "WARNING : No Agent server are ready to service" );
		}
		else if (g_pProxy->bLimit == 0)
		{
			MyLog( LOG_JUST_DISPLAY, "INFO : Proxy LIMIT User login try per Sec");
		}
		else if (g_pProxy->bLimitMaxUser == 0)
		{
			MyLog( LOG_JUST_DISPLAY, "INFO : MAX USER LIMIT at this server set");
		}
		g_pProxy->dwFailtoAllocUserNum++;
	}
	else
	{
       // ������ �������� Agent�� IP�� �����ش�. 
		t_header buffer;
		buffer.type = 8930;
		buffer.size = 18;
		memcpy(msg,&buffer,5);
		memcpy(msg+5,pToAssign->szIPForUser,18);

		if( !g_pINet->SendToUser(dwConnectionIndex,msg,23,FLAG_SEND_NOT_ENCRYPTION) )
		{
			MyLog( LOG_NORMAL, "Failed To Send To User (%d)", dwConnectionIndex );
		}
		pToAssign->dwNumOfUsers++;
		 
		//MyLog( LOG_NORMAL, "OK : User(%d) push to %s", dwConnectionIndex, pToAssign->szIPForUser);

		// add by slowboat
		g_pServerTable->AddNumOfUsersInServerSet();
	}

	// List�� �߰�.
	DWORD dwNewUserID = g_pUserTable->AddUser( dwConnectionIndex );
	if( dwNewUserID )
	{
		USERINFO *pNewUserInfo;
		pNewUserInfo = g_pUserTable->GetUserInfo( dwNewUserID );
		if( !pNewUserInfo )
		{
			MyLog( LOG_FATAL, "OnConnectUser() :: pNewUserInfo is NULL!!!(CI:%d/dwID:%d)", dwConnectionIndex, dwNewUserID );
			g_pINet->CompulsiveDisconnectUser( dwConnectionIndex );
		}
		else
		{
			if(LocalMgr.IsAbleNation(CHINA))//021007 lsw
			{
				MyLog( 0, "CLIENT at (CI:%d, IP:%s, PORT:%d)", dwConnectionIndex, pNewUserInfo->szIP, pNewUserInfo->wPort );
			}
			g_pUserTable->DisconnectUserBySuggest( pNewUserInfo );// 5���Ŀ� ���⵵�� �Ѵ�.
		}
	}
	else
	{
		// CUserTable�� �߰����� ���� ���.
		g_pINet->CompulsiveDisconnectUser( dwConnectionIndex );
	}
	return;
}


void __stdcall OnAcceptServer(DWORD dwConnectionIndex)
{
	int k=0;
}

void __stdcall ReceivedMsgServer(DWORD dwConnectionIndex,char* pMsg,DWORD dwLength)
{
	BYTE bID;
	bID = (BYTE)pMsg[0];

	if (dwConnectionIndex == 0)
	{
#ifdef __ON_DEBUG
//		_asm int 3;
#endif
		return;
	}

	if( bID == (BYTE)PTCL_NOTIFY_SERVER_UP )
	{
		if( g_pServerTable->OnRecvServerUpMsg(dwConnectionIndex, *(WORD*)(pMsg+1)) )
		{
#ifdef __ON_DEBUG
//			_asm int 3;
#endif
		}
		return;
	}

	// 011106 KBS
	if( bID == PTCL_RM || bID == PTCL_RM_FROM_PROXY )
	{
		BYTE header;
		memcpy(&header,pMsg+1, 1);
		if( header == MSG_RM_LOGIN ) return;
		RMProc(dwConnectionIndex, pMsg, dwLength);
		return;
	}
	//



	LP_SERVER_DATA pSender = g_pServerTable->GetConnectedServerData( dwConnectionIndex );

	if( !pSender ) return;

	switch (bID)
	{
	// -------------
	// �⺻��Ŷ 
	// -------------
		// PROXY ����
	case PTCL_REQUEST_SET_SERVER_LIST:
	case PTCL_REQUEST_TO_CONNECT_SERVER_LIST:
	case PTCL_REQUEST_SET_DB_DEMON:
	case PTCL_SERVER_LIST_SETTING_RESULT:
	case PTCL_SERVER_CONNECTING_RESULT:
	case PTCL_DB_DEMON_SETTING_RESULT:
	case PTCL_REPORT_SERVER_DATAS:
	case PTCL_REPORT_SERVER_DESTROY:
	case PTCL_REPORT_SERVER_STATUS:
	case PTCL_REPORT_SERVER_CONNECTION_STATUS_CHANGE:

		// ����
	case PTCL_ORDER_DESTROY_SERVER:
	case PTCL_NOTIFY_SERVER_STATUS:
	case PTCL_SERVER_TRY_TO_CHECK_CONNECTION:
	case PTCL_SERVER_CONNECTION_OK:
	case PTCL_ORDER_TO_REPORT_SERVER_STATUS:
		{
			if( !g_pServerTable->OnRecvNegotiationMsgs( pSender, bID, pMsg+1, dwLength-1 ) )
			{
				MyLog( LOG_FATAL, "OnRecvNegotiationMsg() Failed :: (pSender(%d), bId(%d), MsgLength(%d))", pSender->wPort, bID, dwLength );
#ifdef __ON_DEBUG
//				_asm int 3;
#endif
			}
		}
		break;
	// Added by chan78 at 2001/03/16
	// -----------------------------
	case PTCL_MANAGER_QUERY:
		{
			// ���� �� ����.
			MyLog( LOG_FATAL, "PTCL_MANAGER_QUERY :: has received!!!(%d)", pSender->wPort );
			g_pServerTable->DestroyServer( FINISH_TYPE_UNKNOWN_ERROR);
		}
		break;
	// Added by chan78 at 2001/03/16
	case PTCL_MANAGER_ANSWER:
		{
			if( !AnswerToManager( (LP_MANAGER_PACKET)pMsg, dwLength ) )
			{
				MyLog( LOG_FATAL, "PTCL_MANAGER_ANSWER :: AnswerToManager() has return false(%d)", pSender->wPort );
				break;
			}
		}
		break;
	case PTCL_PROXY_TO_ACCESS:
		{	//< CSD-030509
			t_packet* pPacket = (t_packet*)(pMsg + 5);

			const char* pID = pPacket->u.server_accept_login.id;

			if (g_mgrUser.IsExistLogin(pID))
			{
				MyLog(LOG_NORMAL, "Exist Login : %s", pID);
				break;
			}

			if (g_mgrUser.IsExistLogout(pID))
			{
				MyLog(LOG_NORMAL, "Exist Logout : %s", pID);
				break;
			}
			
			g_mgrUser.AddLogin(pID);

			pMsg[0] = BYTE(PTCL_AGENT_TO_COMMIT);

			if (!g_pServerTable->Send(dwConnectionIndex, pMsg, dwLength))
			{
				MyLog(LOG_IMPORTANT, "Failed To send 'PTCL_AGENT_TO_COMMIT' to Agent");
			}

			break;
		}	//> CSD-030509
	case PTCL_PROXY_TO_LOGOUT:
		{	//< CSD-030509
			const char* pName = pMsg + 1;

			if (g_mgrUser.IsExistLogin(pName))
			{
				if (!g_mgrUser.IsExistLogout(pName))
				{
					g_mgrUser.AddLogout(pName);
				}
			}
			
			g_mgrUser.DelLogin(pName);

			break;
		}	//> CSD-030509
//<! BBD 040311	�����κ����� ������ ��û
	case PTCL_SERVERSET_USERNUM_REQUEST:
		{
			pMsg[0] = BYTE(PTCL_SERVERSET_USERNUM_REQUEST);

			unsigned short count = g_pServerTable->m_dwNumOfUsersInServerSet;
			memcpy(&(pMsg[1]), &count, sizeof(count));

			g_pServerTable->Send(dwConnectionIndex, pMsg, 1 + sizeof(count));
			break;
		}
//> BBD 040311	�����κ����� ������ ��û
	default:
		{
			MyLog( LOG_FATAL, "Unknown bID(%d) Received. From %s(wPort:%d) Size(%d) ConnectionID(%d)", bID, GetTypedServerText(pSender->dwServerType), pSender->wPort, dwLength, dwConnectionIndex);
#ifdef __ON_DEBUG
//			_asm int 3;
#endif
		}
		break;
	}
}

//Added by KBS 020330
extern inline void MgrSend(DWORD dwConnectionIndex, void* pMsg, DWORD dwLength);
//
void __stdcall ReceivedMsgUser(DWORD dwConnectionIndex,char* pMsg,DWORD dwLength)
{
	// Added by chan78 at 2001/03/16
	// ���ӵ� Ŭ���̾�Ʈ�� Packet�� ������ ���, CONTROL_CLIENT�� ���� �õ� ��Ŷ�� �ƴѰ��
	// ������ ���ظ� ��ġ���� �õ��� ����, �ʿ��� ����� ���� �� ������ ���´�.

	USERINFO *pUserInfo = NULL;

	if( !(pUserInfo = (USERINFO *)g_pINet->GetUserInfo( dwConnectionIndex )) )
	{
		MyLog( LOG_FATAL, "RecvMsgUser() :: pUserInfo is NULL!!!" );
		// �־�� �ȵǴ� ��Ȳ.
		return;
	}

	// ���� ���¿� ���� ó��.
	switch( pUserInfo->dwType )
	{
	case CLIENT_TYPE_ILLEGAL:
		{
			// �ҷ� Ŭ���̾�Ʈ�κ��� ��Ŷ�� ���ƿ��� �����Ѵ�.
			// �� ��� �ҷ� Ŭ���̾�Ʈ�� �����Ҷ� CompulsiveDisconnect() �� �� �����̰�.
			// �� �з��� �������� ó���� �Ǳ� ������ ���ƿ� �� �ִ� ��Ŷ���� ó���Ѵ�.
			// �α��� ���� ���̱� ���� �ѹ� �ҷ� Ŭ���̾�Ʈ�� ó���Ȱ� ���� �����Ѵ�.
			return;
		}
		break;
	case CLIENT_TYPE_MANAGER:			// ������ MANAGER
		{
			//Modified 020330 KBS
			BYTE bID;
			bID = (BYTE)pMsg[0];
				
			if( bID == PTCL_RM || bID == PTCL_RM_FROM_PROXY )
			{
				RMProc(dwConnectionIndex, pMsg, dwLength);
				return;
			}
			//	
			
			
	/*		
			// ���⼭ ���ڵ��Ѵ�.
			// Not Yet.

			// ���ڵ��ؼ� ���� ����...
			LP_MANAGER_PACKET pPacket = (LP_MANAGER_PACKET)pMsg;

			if( dwLength < sizeof(MANAGER_PACKET_HEADER) )
			{
				MyLog( LOG_IMPORTANT, "CLIENT_TYPE_MANAGER :: Illegal Packet(%d)", dwLength );
				MyLog( LOG_IMPORTANT, "_____ at (CI:%d, IP:%d, PORT:%d) (PacketSize:%d)", dwConnectionIndex, pUserInfo->szIP, pUserInfo->wPort );

				pUserInfo->dwType = CLIENT_TYPE_ILLEGAL;
				g_pINet->CompulsiveDisconnectUser( dwConnectionIndex );
				return;
			}

			if( !OnRecvMsgFromManager( pUserInfo, pPacket, dwLength ) )
			{
				// �ҷ� CLIENT�� �з�. �� ���� ũ��ŷ�̶�⺸�ٴ� ������ Ȯ���� ����, ��ŷ�̶�� ��û �ɰ��ϴ�.
				MyLog( LOG_IMPORTANT, "CLIENT_TYPE_MANAGER :: Illegal Packet(wCMD:%d, CRC:%d, dwLength:%d)", pPacket->h.wCMD, pPacket->h.dwCRC, dwLength );
				MyLog( LOG_IMPORTANT, "_____ at (CI:%d, IP:%d, PORT:%d) (PacketSize:%d)", dwConnectionIndex, pUserInfo->szIP, pUserInfo->wPort );

				pUserInfo->dwType = CLIENT_TYPE_ILLEGAL;
				g_pINet->CompulsiveDisconnectUser( dwConnectionIndex );
				return;
			}
*/
			// ����ó����.
			return;
		}
		break;


	case CLIENT_TYPE_MANAGER_UNDER_AUTHENTICATION:
		{
			//Modified 020330 KBS
			BYTE header;
			memcpy(&header,pMsg+1, 1);

			switch(header)
			{
			case MSG_RM_LOGIN:
				{
					
					PACKET_RM_LOGIN *packet = (PACKET_RM_LOGIN*)pMsg;

					if(g_pRMTable->CheckCertainIP(dwConnectionIndex, packet->IP))
					{
						pUserInfo->dwType = CLIENT_TYPE_MANAGER;
						pUserInfo->dwStatus = STATUS_USER_ACTIVATED;

						// �������� ��� ����Ʈ���� ���ش�.
						g_pUserTable->RemoveUserFromAwaitingDisconnectUserList( pUserInfo );

						//�α��� ���� �޼���..   �ڽ��� ������ �ѹ��� �Բ�..
						PACKET_RM_LOGIN_OK pck(g_pServerTable->m_dwServerSetNumber);
						MgrSend(dwConnectionIndex, &pck, pck.GetPacketSize());

					}
					else
					{
						//��ϵ� IP�� �ƴ��ڸ����� �α��� �� ��� 
						PACKET_RM_LOGIN_FAIL pck;
						MgrSend(dwConnectionIndex, &pck, pck.GetPacketSize());

						MyLog( LOG_IMPORTANT, "CLIENT_TYPE_MANAGER_UNDER_AUTHENTICATION :: Illegal Packet(%d)", dwLength );
						MyLog( LOG_IMPORTANT, "_____ at (CI:%d, IP:%s, PORT:%d) (PacketSize:%d)", dwConnectionIndex, pUserInfo->szIP, pUserInfo->wPort );
						pUserInfo->dwType = CLIENT_TYPE_ILLEGAL;
						g_pINet->CompulsiveDisconnectUser( dwConnectionIndex );
						return;
					}

					g_pRMTable->AddClient(dwConnectionIndex, packet);
					
					if(g_pRMTable->GetClientNum() == 1)	//RMClient�� �ϳ��� ���ӵ� ���¸� üũ ����!
					{
						StopWaitTimer();
						StartEchoTimer();			//���� �ٿ�Ƴ� �ȵƳ� üũ �۾� ����
					}
				}
				break;
			}

			//
/*

			// �̰����� SSL(Secure Socket Layer)�� �����ϰ�,

			// �ϴ� MANAGER CLIENT�� ���ɼ��� �ִٰ� ������ CLIENT �μ�.
			// �Ϸ��� ���� ������ ���� CLIENT_TYPE_MANAGER �� �����Ǹ� MANAGER �μ� ������ ������ �� �ְԵȴ�.

			// ������ ��Ŷ�� �޴´�.
			if( !OnRecvAuthMsgFromManager( pUserInfo, (LP_MANAGER_PACKET)pMsg, dwLength ) )
			{
				MyLog( LOG_IMPORTANT, "CLIENT_TYPE_MANAGER_UNDER_AUTHENTICATION :: Illegal Packet(%d)", dwLength );
				MyLog( LOG_IMPORTANT, "_____ at (CI:%d, IP:%d, PORT:%d) (PacketSize:%d)", dwConnectionIndex, pUserInfo->szIP, pUserInfo->wPort );
				pUserInfo->dwType = CLIENT_TYPE_ILLEGAL;
				g_pINet->CompulsiveDisconnectUser( dwConnectionIndex );
				return;
			}
			else
			{
				// ���� �Ǿ���.
				pUserInfo->dwType = CLIENT_TYPE_MANAGER;
				pUserInfo->dwStatus = STATUS_USER_ACTIVATED;

				// �������� ��� ����Ʈ���� ���ش�.
				g_pUserTable->RemoveUserFromAwaitingDisconnectUserList( pUserInfo );

				// 20010508 Add Proxy-OwnPort �ʿ�
				AnswerAuthPacket( pUserInfo );
			}
*/

		}
		break;
	case CLIENT_TYPE_NORMAL: // �Ϲ� ����.
		{
			// �������� Ŭ���̾�Ʈ�� PROXY�� �޽����� ������ �ʴ´�.
			// ������ MANAGER CLIENT �� ���ɼ��� �����Ƿ�,
			// ���� ó�� ���ƿ� PACKET ���� �ҷ� CLIENT �� MANAGER CLIENT�� 1�������� �з��Ѵ�.
			// --------------------------------

//			MyLog( LOG_IMPORTANT, "RecvPacket Size : %d, %d", dwLength, sizeof(MANAGER_PACKET_HEADER));

			if( dwLength <= sizeof(MANAGER_PACKET_HEADER) )
			{
				LP_MANAGER_PACKET pPacket = (LP_MANAGER_PACKET)pMsg;

				if( pPacket->h.wCMD == MANAGER_CLIENT_FIRST_PACKET_TYPE )
				{
					if( pPacket->h.dwCRC == MANAGER_CLIENT_FIRST_PACKET_CRC )
					{
						// �ϴ� MANAGER CLIENT �� ���ɼ��� �����Ѵ�.
						// pUserInfo->dwType = CLIENT_TYPE_MANAGER_UNDER_AUTHENTICATION;
						pUserInfo->dwType = CLIENT_TYPE_MANAGER_UNDER_AUTHENTICATION;	//  �ϴ� ������ ����.
						return;
					}
				}
			}
			// --------------------------------

			// �� ���ǿ��� �ɷ����� ���� CLIENT ���� �ҷ�.
			// �ҷ� ���ӽõ��� ����ϰ� ������ ���´�.
			MyLog( LOG_IMPORTANT, "--- ILLEGAL CLIENT at (CI:%d, IP:%s, PORT:%d)", dwConnectionIndex, pUserInfo->szIP, pUserInfo->wPort );
			
			pUserInfo->dwType = CLIENT_TYPE_ILLEGAL;
			g_pINet->CompulsiveDisconnectUser( dwConnectionIndex );
			return;
		}
	default:
		{
			// ������ ����� Ÿ���� �������� �ȵȴ�.
			// ����� �� �ִ� �������� �з��ϸ� �� ū ���׸� ������� �����Ƿ�
			// ������ ���δ�.
			MyLog( LOG_FATAL, "RecvMsgUser() :: UNKNOWN 'CLIENT_TYPE'(%d)", pUserInfo->dwType );
#ifdef __ON_DEBUG
//			_asm int 3;
#else
			g_pServerTable->DestroyServer( FINISH_TYPE_UNKNOWN_ERROR );
#endif
		}
	}

	return;
}

void __stdcall OnDisconnectUser(DWORD dwConnectionIndex)
{
	
	//Modified by KBS 020330
	USERINFO *pUserInfo = (USERINFO*)g_pINet->GetUserInfo( dwConnectionIndex );

	if(pUserInfo->dwType == CLIENT_TYPE_MANAGER)
	{
		g_pRMTable->RemoveClient( dwConnectionIndex );

		if(g_pRMTable->GetClientNum() == 0)	//RMClient�� ��� ���� ���� �Ǿ����� üũ �׸� 
		{
			StopWaitTimer();
			StopEchoTimer();	
		}
	}
	//
	
	// Modified by chan78 at 2001/02/27
	g_pUserTable->RemoveUser( dwConnectionIndex );

	return;
}

//Added by KBS 011220
BOOL CheckValidConnection(DWORD dwConnectionIndex, int type)
{
	RM_LISTENER_INFO* cur = NULL;
	RM_LISTENER_INFO* next = NULL;

	cur = g_pRMTable->m_ListenerTable.m_ppInfoTable[ type ];

	while (cur)
	{
		next = cur->pNextInfo;
	
		if(cur->dwConnectionIndex == dwConnectionIndex)
		{
			return TRUE;
		}
			
		cur = next;
	}
	
	return FALSE;
}
//

void __stdcall OnDisconnectServer(DWORD dwConnectionIndex)
{
	//Added by KBS 011119
	void *pVoid = g_pINet->GetServerInfo(dwConnectionIndex);

	if(pVoid)	//Listener Disconnect
	{	
		BYTE bConnectType;
		memcpy(&bConnectType,pVoid,1);

		switch((int)bConnectType)
		{

		case RM_TYPE_TOOL:
			{
				// 021008 YGI
				/*
				//Modified by KBS 011213
				g_pRMTable->RemoveClient( dwConnectionIndex );

				if(g_pRMTable->GetClientNum() == 0)	//RMClient�� ��� ���� ���� �Ǿ����� üũ �׸� 
				{
					StopWaitTimer();
					StopEchoTimer();	
				}*/
			}
			break;

		case RM_TYPE_LISTENER:
			{
				//Modified by KBS 0112
				if(CheckValidConnection( dwConnectionIndex, 1 ))
				{
					//Listener�� ���� ������..
					in_addr addr;
					addr.S_un = g_pINet->GetServerAddress( dwConnectionIndex )->sin_addr.S_un;
		
					g_pRMTable->m_ListenerTable.MoveToDisconnectStatus(inet_ntoa(addr));

					MyLog( LOG_NORMAL, "Valid Listener Disconnected :: dwConnectionIndex = %d", dwConnectionIndex );
				}
				else
				{
					char ip[20];	memset(ip,0,20);
					WORD port;
					g_pINet->GetServerAddress( dwConnectionIndex, ip, &port);
					
					MyLog( LOG_NORMAL, "Invalid Listener Disconnected :: dwConnectionIndex = %d, IP = %s, Port = %d",dwConnectionIndex, ip, port );
				}
			}
			break;
		}
	}
	else	//�Ϲ� ���� 
	{	
		//Added by KBS 011205
		LP_SERVER_DATA pServerData;
		pServerData = g_pServerTable->GetConnectedServerData( dwConnectionIndex );

		if(pServerData)
		{
			PACKET_RM_SERVER_DISCONNECT packet((BYTE)g_pServerTable->m_dwServerSetNumber, 
												pServerData->wPort);
		
			g_pRMTable->BroadcastAllRMClient((char*)&packet,packet.GetPacketSize());
		}
		//
		
		g_pServerTable->RemoveConnectedServerDataFromHashTable(dwConnectionIndex);
		
	}
	
	return;
}