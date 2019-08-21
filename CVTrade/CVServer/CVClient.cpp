#include<iostream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "CVQueueDAOs.h"
#include "CVClient.h"
#include "CVClients.h"
#include "CVNet/CVSocket.h"
#include "CVErrorMessage.h"
#include "../include/CVType.h"
#include "../include/CVGlobal.h"

using namespace std;

typedef long (*FillTandemOrder)(string& strService, char* pIP, map<string, string>& mBranchAccount, union CV_ORDER &cv_order, union CV_TS_ORDER &cv_ts_order, bool bIsProxy);
extern long FillTandemBitcoinOrderFormat(string& strService, char* pIP, map<string, string>& mBranchAccount, union CV_ORDER &cv_order, union CV_TS_ORDER &cv_ts_order, bool bIsProxy);
extern void FprintfStderrLog(const char* pCause, int nError, unsigned char* pMessage1, int nMessage1Length, unsigned char* pMessage2 = NULL, int nMessage2Length = 0);

CCVClient::CCVClient(struct TCVClientAddrInfo &ClientAddrInfo, string strService, bool bIsProxy)
{
	memset(&m_ClientAddrInfo, 0, sizeof(struct TCVClientAddrInfo));
	memcpy(&m_ClientAddrInfo, &ClientAddrInfo, sizeof(struct TCVClientAddrInfo));

	m_ClientStatus = csNone;
	m_strService = strService;
	m_pHeartbeat = NULL;
	pthread_mutex_init(&m_MutexLockOnClientStatus, NULL);
	m_nLengthOfAccountNum = sizeof(struct CV_StructAccnum);
	m_nLengthOfLogonMessage = sizeof(struct CV_StructLogon);
	m_nLengthOfLogonReplyMessage = sizeof(struct CV_StructLogonReply);
	m_nLengthOfHeartbeatMessage = sizeof(struct CV_StructHeartbeat);
	m_nLengthOfLogoutMessage = sizeof(struct CV_StructLogout);

	if(m_strService.compare("TS") == 0)
	{
		m_nLengthOfOrderMessage = sizeof(struct CV_StructOrder);
	}
	else
	{
		//Keanu : add error message.
	}

	if(bIsProxy)
		m_nLengthOfOrderMessage += (IPLEN);

	m_bIsProxy = bIsProxy;

	Start();
}

CCVClient::~CCVClient()
{
	if( m_pHeartbeat)
	{
		m_pHeartbeat->Terminate();
		delete m_pHeartbeat;
		m_pHeartbeat = NULL;
	}

	pthread_mutex_destroy(&m_MutexLockOnClientStatus);
}

void* CCVClient::Run()
{
	unsigned char uncaRecvBuf[BUFSIZE];
	unsigned char uncaMessageBuf[MAXDATA];
	unsigned char uncaOverheadMessageBuf[BUFSIZE];
	unsigned char uncaOrder[BUFSIZE];
	unsigned char uncaSendBuf[BUFSIZE];
        unsigned char uncaEscapeBuf[2];
	struct CV_StructHeartbeat HeartbeatRP;
	struct CV_StructHeartbeat HeartbeatRQ;

	memset(uncaRecvBuf, 0, sizeof(uncaRecvBuf));
	memset(uncaMessageBuf, 0, sizeof(uncaMessageBuf));
	memset(uncaOverheadMessageBuf, 0, sizeof(uncaOverheadMessageBuf));
	memset(uncaSendBuf, 0, sizeof(uncaSendBuf));
	uncaSendBuf[0] = ESCAPE;// for order reply

	HeartbeatRQ.header_bit[0] = ESCAPE;
	HeartbeatRQ.header_bit[1] = HEARTBEATREQ;
	memcpy(HeartbeatRQ.heartbeat_msg, "HBRQ", 4);

	HeartbeatRP.header_bit[0] = ESCAPE;
	HeartbeatRP.header_bit[1] = HEARTBEATREP;
	memcpy(HeartbeatRP.heartbeat_msg, "HBRP", 4);

	int nSizeOfRecvedCVMessage = 0;	
	int nSizeOfCVOrder = 0;
	int nSizeOfTSOrder = 0;
	int nSizeOfRecvSocket = 0;
	int nSizeOfSendSocket = 0;
	int nSizeOfErrorMessage = 0;
	int nTimeIntervals = HEARTBEATVAL;
	int nToRecv; 

	FillTandemOrder fpFillTandemOrder = NULL;
	union CV_ORDER cv_order;
	union CV_TS_ORDER cv_ts_order;

	CCVClients* pClients = CCVClients::GetInstance();
	assert(pClients);

	CCVErrorMessage* pErrorMessage = new CCVErrorMessage();

	if(m_strService.compare("TS") == 0)
	{
		fpFillTandemOrder = FillTandemBitcoinOrderFormat;
		nSizeOfCVOrder = sizeof(struct CV_StructOrder);
		nSizeOfTSOrder = sizeof(struct CV_StructTSOrder);
		nSizeOfRecvSocket = sizeof(struct CV_StructOrder);
		nSizeOfSendSocket = sizeof(struct CV_StructOrderReply);
		nSizeOfErrorMessage = sizeof(struct CV_StructTSOrderReply);
		uncaSendBuf[1] = ORDERREP;
	}

	m_pHeartbeat = new CCVHeartbeat(nTimeIntervals);
	assert(m_pHeartbeat);
	m_pHeartbeat->SetCallback(this);

	SetStatus(csLogoning);

	while(m_ClientStatus != csOffline)
	{
			memset(uncaEscapeBuf, 0, sizeof(uncaEscapeBuf));

			bool bRecvAll = RecvAll(uncaEscapeBuf, 2);

			if(bRecvAll == false)
			{
				FprintfStderrLog("RECV_ESC_ERROR", -1, m_uncaLogonID, sizeof(m_uncaLogonID), uncaEscapeBuf, sizeof(uncaEscapeBuf));
				continue;
			}

			if(uncaEscapeBuf[0] != ESCAPE)
			{
				FprintfStderrLog("ESCAPE_BYTE_ERROR", -1, m_uncaLogonID, sizeof(m_uncaLogonID));
				printf("Error byte = %x\n", uncaEscapeBuf[0]);
				continue;
			}
			else
			{
				nToRecv = 0;
				switch(uncaEscapeBuf[1])
				{
					case LOGREQ:
						nToRecv = sizeof(struct CV_StructLogon)-2;
				printf("\nlogin nToRecv = %d\n", nToRecv);
						break;
					case HEARTBEATREQ:
						nToRecv = sizeof(struct CV_StructHeartbeat)-2;
				printf("\nhbrq nToRecv = %d\n", nToRecv);
						break;
					case HEARTBEATREP:
						nToRecv = sizeof(struct CV_StructHeartbeat)-2;
				printf("\nhbrp nToRecv = %d\n", nToRecv);
						break;
					case ORDERREQ:
						nToRecv = sizeof(struct CV_StructOrder)-2;
				printf("\norder nToRecv = %d\n", nToRecv);
						break;
					case DISCONNMSG:
						SetStatus(csOffline);
				printf("\ndisconnect nToRecv = %d\n", nToRecv);
						break;
					default:
						FprintfStderrLog("ESCAPE_BYTE_ERROR", -1, m_uncaLogonID, sizeof(m_uncaLogonID));
						printf("\nError byte = %x\n", uncaEscapeBuf[1]);
						continue;
				}
				if(m_ClientStatus == csOffline)
					break;

				memset(uncaRecvBuf, 0, sizeof(uncaRecvBuf));
				bRecvAll = RecvAll(uncaRecvBuf, nToRecv);
				if(bRecvAll == false)
				{
					FprintfStderrLog("RECV_TRAIL_ERROR", -1, m_uncaLogonID, sizeof(m_uncaLogonID), uncaRecvBuf, nToRecv);
					continue;
				}
			}

		memcpy(uncaMessageBuf, uncaEscapeBuf, 2);
		memcpy(uncaMessageBuf+2, uncaRecvBuf, nToRecv);
		nSizeOfRecvedCVMessage = 2 + nToRecv;	
		if(nToRecv > 0)
		{
			if(uncaMessageBuf[1] == LOGREQ)//logon message
			{
				unsigned char uncaSendLogonBuf[MAXDATA];
				
				FprintfStderrLog("RECV_CV_LOGON", 0, uncaMessageBuf, sizeof(struct CV_StructLogon));

				if(m_ClientStatus == csLogoning)
				{
					struct CV_StructLogon logon_type;
					bool bLogon;
					memset(&logon_type, 0, sizeof(struct CV_StructLogon));
					memcpy(&logon_type, uncaMessageBuf, sizeof(struct CV_StructLogon));

					struct CV_StructLogonReply logon_reply;
					memset(&logon_reply, 0, m_nLengthOfLogonReplyMessage);
					memset(m_uncaLogonID, 0, sizeof(m_uncaLogonID));
					memcpy(m_uncaLogonID, logon_type.logon_id, sizeof(logon_type.logon_id));
#if 0
					bLogon = LogonAuth(logon_type.logon_id, logon_type.password, logon_reply);//logon & get logon reply data
#else
					printf("ID:%.20s\n", uncaMessageBuf+2);
					if( (strcmp((char*)uncaMessageBuf+2, "P123069984") == 0 && strcmp((char*)uncaMessageBuf+22, "testtest") == 0 )
					    || (strcmp((char*)uncaMessageBuf+2, "brianlee") == 0 && strcmp((char*)uncaMessageBuf+22, "ilovebrianlee") == 0))
						bLogon = true;
					else
						bLogon = false;
					
					bLogon = true;
					printf("PW:%.30s\n", uncaMessageBuf+22);
					printf("SA:%.2s\n", uncaMessageBuf+52);
					printf("VS:%.10s\n", uncaMessageBuf+54);
					printf("login success.\n");
#endif
					memset(uncaSendLogonBuf, 0, sizeof(uncaSendLogonBuf));
					logon_reply.header_bit[0] = ESCAPE;
					logon_reply.header_bit[1] = LOGREP;
					uncaSendLogonBuf[0] = ESCAPE;
					uncaSendLogonBuf[1] = LOGREP;

					if(bLogon) {
						memcpy(logon_reply.status_code, "OK", 2);//to do
						memcpy(logon_reply.backup_ip, "192.168.101.211", 15);
						memcpy(logon_reply.error_code, "00", 4);//to do
						sprintf(logon_reply.error_message, "login success.");
					}
					else {
						memcpy(logon_reply.status_code, "NG", 2);//to do
						memcpy(logon_reply.backup_ip, "192.168.101.211", 15);
						memcpy(logon_reply.error_code, "01", 4);//to do
						sprintf(logon_reply.error_message, "login fail.");
					}
					memcpy(uncaSendLogonBuf, &logon_reply.header_bit[0], m_nLengthOfLogonReplyMessage);
					bool bSendData = SendData(uncaSendLogonBuf, m_nLengthOfLogonReplyMessage);

					if(bSendData == true)
					{
						FprintfStderrLog("SEND_LOGON_REPLY", 0, uncaSendLogonBuf, m_nLengthOfLogonReplyMessage);
					}
					else
					{
						FprintfStderrLog("SEND_LOGON_REPLY_ERROR", -1, uncaSendLogonBuf, m_nLengthOfLogonReplyMessage);
						perror("SEND_SOCKET_ERROR");
					}

					U_ByteSint bytesint;
					memset(&bytesint,0,16);

					if(bLogon)//success
					{
						SetStatus(csOnline);
						m_pHeartbeat->Start();
					}
					else//logon failed
					{
						FprintfStderrLog("LOGON_ON_FAILED", 0, reinterpret_cast<unsigned char*>(m_ClientAddrInfo.caIP), sizeof(m_ClientAddrInfo.caIP));
					}
				}
				else if(m_ClientStatus == csOnline)//repeat logon
				{
					struct CV_StructLogonReply logon_reply;
					unsigned char uncaSendRelogBuf[MAXDATA];

					memset(&logon_reply, 0, m_nLengthOfLogonReplyMessage);
					memset(uncaSendRelogBuf, 0, MAXDATA);
					logon_reply.header_bit[0] = ESCAPE;
					logon_reply.header_bit[1] = 0x01;
					memcpy(logon_reply.status_code, "NG", 2);//to do
					memcpy(logon_reply.backup_ip, "192.168.101.211", 15);
					memcpy(logon_reply.error_code, "02", 4);//to do
					sprintf(logon_reply.error_message, "Account has logon.");
					memcpy(uncaSendRelogBuf, &logon_reply.header_bit[0], m_nLengthOfLogonReplyMessage);

					bool bSendData = SendData(uncaSendRelogBuf, m_nLengthOfLogonReplyMessage);
					if(bSendData == true)
					{
						FprintfStderrLog("SEND_REPEAT_LOGON", 0, uncaSendRelogBuf, m_nLengthOfLogonReplyMessage);
					}
					else
					{
						FprintfStderrLog("SEND_REPEAT_LOGON_ERROR", -1, uncaSendRelogBuf, m_nLengthOfLogonReplyMessage);
						perror("SEND_SOCKET_ERROR");
					}
				}
				else
				{
					break;
				}
			}
			else if(uncaMessageBuf[1] == HEARTBEATREQ)//heartbeat message
			{
				if(memcmp(uncaMessageBuf + 2, HeartbeatRQ.heartbeat_msg, 4) == 0)
				{
					FprintfStderrLog("RECV_CV_HBRQ", 0, NULL, 0);

					bool bSendData = SendData((unsigned char*)&HeartbeatRP, sizeof(HeartbeatRP));
					if(bSendData == true)
					{
						FprintfStderrLog("SEND_CV_HBRP", 0, (unsigned char*)&HeartbeatRP, sizeof(HeartbeatRP));
					}
					else
					{
						FprintfStderrLog("SEND_CV_HBRP_ERROR", -1, (unsigned char*)&HeartbeatRP, sizeof(HeartbeatRP));
						perror("SEND_SOCKET_ERROR");
					}
				}
				else
				{
					FprintfStderrLog("RECV_CV_HBRQ_ERROR", -1, uncaMessageBuf + 2, 4);
				}
			}
			else if(uncaMessageBuf[1] == HEARTBEATREP)//heartbeat message
			{
				if(memcmp(uncaMessageBuf + 2, HeartbeatRP.heartbeat_msg, 4) == 0)
				{
					FprintfStderrLog("RECV_CV_HBRP", 0, NULL, 0);
				}
				else
				{
					FprintfStderrLog("RECV_CV_HBRP_ERROR", -1, uncaMessageBuf + 2, 4);
				}
			}


			else if(uncaMessageBuf[1] == DISCONNMSG)
			{
				SetStatus(csOffline);
				FprintfStderrLog("RECV_CV_DISCONNECT", 0, 0, 0);
				break;
			}
			else if(uncaMessageBuf[1] == ORDERREQ)
			{
				if(m_bIsProxy)
					nSizeOfRecvedCVMessage -= (IPLEN);//ip
				FprintfStderrLog("RECV_CV_ORDER", 0, uncaMessageBuf, nSizeOfRecvedCVMessage);

				if(m_ClientStatus == csLogoning)
				{
					struct CV_StructOrderReply replymsg;
                                        int errorcode = -LG_ERROR;
                                        memset(&replymsg, 0, sizeof(struct CV_StructOrderReply));
                                        replymsg.header_bit[0] = 0x1b;
                                        replymsg.header_bit[1] = ORDERREP;

                                        memcpy(&replymsg.original, &cv_order, nSizeOfCVOrder);
                                        sprintf((char*)&replymsg.error_code, "%.4d", errorcode);

                                        memcpy(&replymsg.reply_msg, pErrorMessage->GetErrorMessage(LG_ERROR),
                                                strlen(pErrorMessage->GetErrorMessage(LG_ERROR)));

					int nSendData = SendData((unsigned char*)&replymsg, sizeof(struct CV_StructOrderReply));

					if(nSendData)
					{
						FprintfStderrLog("SEND_LOGON_FIRST", 0, (unsigned char*)&replymsg, sizeof(struct CV_StructOrderReply));
					}
					else
					{
						FprintfStderrLog("SEND_LOGON_FIRST_ERROR", -1, (unsigned char*)&replymsg, sizeof(struct CV_StructOrderReply));
						perror("SEND_SOCKET_ERROR");
					}
					continue;
				}

				CCVQueueDAO* pQueueDAO = CCVQueueDAOs::GetInstance()->GetDAO();

				if(pQueueDAO)
				{
					memset(&cv_order, 0, sizeof(union CV_ORDER));
					memcpy(&cv_order, uncaMessageBuf, nSizeOfCVOrder);
					memset(&cv_ts_order, 0, sizeof(union CV_TS_ORDER));
					long lOrderNumber = 0;
					lOrderNumber = fpFillTandemOrder(m_strService, m_ClientAddrInfo.caIP,
									m_mBranchAccount, cv_order, cv_ts_order, m_bIsProxy ? true : false);

					if(lOrderNumber < 0)//error
					{
						int errorcode = -lOrderNumber;
						struct CV_StructOrderReply replymsg;

						memset(&replymsg, 0, sizeof(struct CV_StructOrderReply));
						replymsg.header_bit[0] = 0x1b;
						replymsg.header_bit[1] = ORDERREP;

						memcpy(&replymsg.original, &cv_order, nSizeOfCVOrder);
						sprintf((char*)&replymsg.error_code, "%.4d", errorcode);

						memcpy(&replymsg.reply_msg, pErrorMessage->GetErrorMessage(lOrderNumber),
							strlen(pErrorMessage->GetErrorMessage(lOrderNumber)));

						int nSendData = SendData((unsigned char*)&replymsg, sizeof(struct CV_StructOrderReply));
						if(nSendData)
						{
							FprintfStderrLog("SEND_FILL_TANDEM_ORDER_CODE", -lOrderNumber, (unsigned char*)&replymsg, sizeof(struct CV_StructOrderReply));
						}
						else
						{
							FprintfStderrLog("SEND_FILL_TANDEM_ORDER_CODE_ERROR", lOrderNumber, (unsigned char*)&replymsg, sizeof(struct CV_StructOrderReply));
							perror("SEND_SOCKET_ERROR");
						}
						continue;
					}

					try
					{
						if(pClients->GetClientFromHash(lOrderNumber) != NULL)
						{
							throw "Inuse";
						}
						else
						{
							pClients->InsertClientToHash(lOrderNumber, this);
						}
					}
					catch(char const* pExceptionMessage)
					{
						cout << "OrderNumber = " << lOrderNumber << " " << pExceptionMessage << endl;
					}

					memset(uncaOrder, 0, sizeof(uncaOrder));
					memcpy(uncaOrder, &cv_ts_order, nSizeOfTSOrder);
					int nResult = pQueueDAO->SendData(uncaOrder, nSizeOfTSOrder);


					if(nResult == 0)
					{
						FprintfStderrLog("SEND_Q", 0, uncaOrder, nSizeOfTSOrder);
						struct CVOriginalOrder newOriginalOrder;
						memset(newOriginalOrder.uncaBuf, 0, sizeof(newOriginalOrder.uncaBuf));
						memcpy(newOriginalOrder.uncaBuf, &cv_order, nSizeOfCVOrder);
						m_mOriginalOrder.insert(std::pair<long, struct CVOriginalOrder>(lOrderNumber, newOriginalOrder));
					}
					else if(nResult == -1)
					{
						FprintfStderrLog("SEND_Q_ERROR", -1, uncaOrder, nSizeOfTSOrder);
						pClients->RemoveClientFromHash(lOrderNumber);
						perror("SEND_Q_ERROR");
					}
				}
				else
				{
					FprintfStderrLog("GET_QDAO_ERROR", -1, uncaOrder, nSizeOfTSOrder);
				}
			}
		}//recv tcp message
	}
	delete pErrorMessage;
	m_pHeartbeat->Terminate();
	pClients->MoveOnlineClientToOfflineVector(this);
	return NULL;
}

bool CCVClient::SendData(const unsigned char* pBuf, int nSize)
{
	return SendAll(pBuf, nSize);
}

bool CCVClient::SendAll(const unsigned char* pBuf, int nToSend)
{
	int nSend = 0;
	int nSended = 0;

	do
	{
		nToSend -= nSend;

		nSend = send(m_ClientAddrInfo.nSocket, pBuf + nSended, nToSend, 0);

		if(nSend == -1)
		{
			break;
		}

		if(nSend < nToSend)
		{
			nSended += nSend;
		}
		else
		{
			break;
		}
	}
	while(nSend != nToSend);

	return nSend == nToSend ? true : false;
}

bool CCVClient::RecvAll(unsigned char* pBuf, int nToRecv)
{
        int nRecv = 0;
        int nRecved = 0;

        do
        {
                nToRecv -= nRecv;
                nRecv = recv(m_ClientAddrInfo.nSocket, pBuf + nRecved, nToRecv, 0);

                if(nRecv > 0)
                {
                        if(m_pHeartbeat)
                                m_pHeartbeat->TriggerGetClientReplyEvent();
                        else
                                FprintfStderrLog("HEARTBEAT_NULL_ERROR", -1, m_uncaLogonID, sizeof(m_uncaLogonID));

                        FprintfStderrLog(NULL, 0, m_uncaLogonID, sizeof(m_uncaLogonID), pBuf + nRecved, nRecv);
                        nRecved += nRecv;
                }
                else if(nRecv == 0)
                {
                        SetStatus(csOffline);
                        FprintfStderrLog("RECV_CV_CLOSE", 0, m_uncaLogonID, sizeof(m_uncaLogonID));
                        break;
                }
                else if(nRecv == -1)
                {
                        SetStatus(csOffline);
                        FprintfStderrLog("RECV_CV_ERROR", -1, m_uncaLogonID, sizeof(m_uncaLogonID), (unsigned char*)strerror(errno), strlen(strerror(errno)));
                        break;
                }
                else
                {
                        SetStatus(csOffline);
                        FprintfStderrLog("RECV_CV_ELSE_ERROR", -1, m_uncaLogonID, sizeof(m_uncaLogonID), (unsigned char*)strerror(errno), strlen(strerror(errno)));
                        break;
                }
        }
        while(nRecv != nToRecv);

        return nRecv == nToRecv ? true : false;
}


void CCVClient::SetStatus(TCVClientStauts csStatus)
{
	pthread_mutex_lock(&m_MutexLockOnClientStatus);//lock

	m_ClientStatus = csStatus;

	pthread_mutex_unlock(&m_MutexLockOnClientStatus);//unlock
}

TCVClientStauts CCVClient::GetStatus()
{
	return m_ClientStatus;
}

void CCVClient::GetOriginalOrder(long nOrderNumber, int nOrderSize, union CV_ORDER_REPLY &cv_order_reply)
{	
	memcpy(&cv_order_reply, m_mOriginalOrder[nOrderNumber].uncaBuf, nOrderSize);
}

bool CCVClient::LogonAuth(char* pID, char* ppassword, struct CV_StructLogonReply &logon_reply)
{
	CCVSocket* pSocket = new CCVSocket();
	pSocket->Connect("pass.cryptovix.com.tw", "80");

	char caBuf[MAXDATA];
	unsigned char uncaSendBuf[MAXDATA];
	unsigned char uncaRecvBuf[MAXDATA];

	memset(caBuf, 0, MAXDATA);
	memset(uncaSendBuf, 0, MAXDATA);
	memset(uncaRecvBuf, 0, MAXDATA);

	char pass_ip[50];
	memset(pass_ip, 0, 50);
	sprintf(pass_ip, "pass.cryptovix.com.tw");

	char cust_id[11];
	memset(cust_id, 0,11);
	memcpy(cust_id, pID, 10);

	char en_id_pass[50];
	memset(en_id_pass, 0, 50);
	memcpy(en_id_pass, ppassword, 50);

	sprintf(caBuf,"GET http://%s/PasswordGW/Gateway.asp?IDNO=%s&Func=2&PswdType=0&Password=%s&IP=%s HTTP/1.0\n\n",
			pass_ip, cust_id, en_id_pass, m_ClientAddrInfo.caIP);

	memcpy(uncaSendBuf, caBuf ,MAXDATA);

	pSocket->Send(uncaSendBuf, strlen(caBuf));

	pSocket->Recv(uncaRecvBuf,MAXDATA);
	cout << uncaRecvBuf << endl;
	delete pSocket;


	char caRecvBuf[MAXDATA];

	char* pFirstToken = NULL;
	char* pToken = NULL;

	char caItem[30][512];
	int nItemCount;

	char castatus_code[5], caHttpMessage[512];

	memset(caRecvBuf, 0, MAXDATA);
	memset(caItem, 0, sizeof(caItem));
	memset(castatus_code, 0, 5);
	memset(caHttpMessage, 0, 512);

	memcpy(caRecvBuf, uncaRecvBuf, sizeof(caRecvBuf));

	pFirstToken = strstr(caRecvBuf, "200 OK");
	if(pFirstToken == NULL)
	{
		memcpy(logon_reply.error_code, "M999", 4);
		sprintf(logon_reply.error_message, "Check PASS return error#");
		return false;
	}

	pFirstToken = strstr(caRecvBuf, "\r\n\r\n");

	if(pFirstToken == NULL)
	{
	}
	else
	{
		pToken = strtok(pFirstToken, ",");
		nItemCount = 0;
		while(1)
		{
			if(pToken == NULL)
				break;
			strcpy(caItem[nItemCount], pToken);
			nItemCount++;
			pToken = strtok(NULL, ",");
		}

		pToken = strtok(caItem[0], "=");
		pToken = strtok(NULL, "=");
		sprintf(castatus_code, "%04d", atol(pToken));

		for(int i=1;i<nItemCount;i++)
		{
			if(strncmp(caItem[i], "Msg", 3) == 0)
			{
				pToken = strtok(caItem[i], "=");
				pToken = strtok(NULL, "=");
				sprintf(caHttpMessage, "%s", pToken);
			}
		}

		if(strncmp(castatus_code, "0000", 4) == 0 || strncmp(castatus_code, "0001", 4) == 0)
		{
			memcpy(logon_reply.status_code, castatus_code, 4);
			memcpy(logon_reply.error_code, "M000", 4);
			memcpy(logon_reply.error_message, caHttpMessage, sizeof(logon_reply.error_message));
			return true;
		}
		else if (strncmp(castatus_code, "7997", 4) == 0)
		{ /* first login */ 
			memcpy(logon_reply.status_code, castatus_code, 4);
			memcpy(logon_reply.error_code, "M151", 4);
			sprintf(logon_reply.error_message, "�K�X���~#");
			return false;
		}
		else if (strncmp(castatus_code, "7996", 4) == 0)
		{ /* first login */
			memcpy(logon_reply.status_code, castatus_code, 4);
			memcpy(logon_reply.error_code, "M155", 4);
			memcpy(logon_reply.error_message, caHttpMessage, sizeof(logon_reply.error_message));
			return false;
		}
		else if(strncmp(castatus_code, "7993", 4) == 0)
		{
			memcpy(logon_reply.status_code, castatus_code, 4);
			memcpy(logon_reply.error_code, "M156", 4);
			memcpy(logon_reply.error_message, caHttpMessage, sizeof(logon_reply.error_message));
			//SetStatus(csOnline);
			return true;
		}
		else if(strncmp(castatus_code, "7992", 4) == 0)
		{
			memcpy(logon_reply.status_code, castatus_code, 4);
			memcpy(logon_reply.error_code, "M156", 4);
			memcpy(logon_reply.error_message, caHttpMessage, sizeof(logon_reply.error_message));
			//SetStatus(csOnline);
			return true;
		}
		else if (strncmp(castatus_code, "7998", 4) == 0)
		{
			memcpy(logon_reply.status_code, castatus_code, 4);
			memcpy(logon_reply.error_code, "M152", 4);
			memcpy(logon_reply.error_message, caHttpMessage, sizeof(logon_reply.error_message));
			return false;
		}
		else if (strncmp(castatus_code, "1999", 4) == 0)
		{
			memcpy(logon_reply.status_code, castatus_code, 4);
			memcpy(logon_reply.error_code, "M999", 4);
			sprintf(logon_reply.error_message, "M999 pass server busy#");
			return false;
		}
		else if (strncmp(castatus_code, "8992", 4) == 0)
		{
			memcpy(logon_reply.status_code, castatus_code, 4);
			memcpy(logon_reply.error_code, "M153", 4);
			memcpy(logon_reply.error_message, caHttpMessage, sizeof(logon_reply.error_message));
			return false;
		}
		else if (strncmp(castatus_code, "8994", 4) == 0)
		{
			memcpy(logon_reply.status_code, castatus_code, 4);
			memcpy(logon_reply.error_code, "M153", 4);
			memcpy(logon_reply.error_message, caHttpMessage, sizeof(logon_reply.error_message));
			return false;
		}
		else if (strncmp(castatus_code, "8995", 4) == 0)
		{
			memcpy(logon_reply.status_code, castatus_code, 4);
			memcpy(logon_reply.error_code, "M153", 4);
			memcpy(logon_reply.error_message, caHttpMessage, sizeof(logon_reply.error_message));
			return false;
		}
		else if (strncmp(castatus_code, "8996", 4) == 0)
		{
			memcpy(logon_reply.status_code, castatus_code, 4);
			memcpy(logon_reply.error_code, "M153", 4);
			memcpy(logon_reply.error_message, caHttpMessage, sizeof(logon_reply.error_message));
			return false;
		}
		else if (strncmp(castatus_code, "8997", 4) == 0)
		{
			memcpy(logon_reply.status_code, castatus_code, 4);
			memcpy(logon_reply.error_code, "M153", 4);
			memcpy(logon_reply.error_message, caHttpMessage, sizeof(logon_reply.error_message));
			return false;
		}
		else if (strncmp(castatus_code, "8998", 4)==0)
		{
			memcpy(logon_reply.status_code, castatus_code, 4);
			memcpy(logon_reply.error_code, "M153", 4);
			memcpy(logon_reply.error_message, caHttpMessage, sizeof(logon_reply.error_message));
			return false;
		}
		else
		{
			memcpy(logon_reply.status_code, castatus_code, 4);
			memcpy(logon_reply.error_code, "M153", 4);
			memcpy(logon_reply.error_message, caHttpMessage, sizeof(logon_reply.error_message));
			return false;
		}
	}
}

void CCVClient::GetAccount(char* pID, char* psource, char* pVersion, vector<struct AccountMessage> &vAccountMessage)
{
	CCVSocket* pSocket = new CCVSocket();
	pSocket->Connect("aptrade.capital.com.tw", "4000");

	unsigned char uncaSendBuf[MAXDATA];
	char caSendBuf[MAXDATA];
	unsigned char uncaRecvBuf[MAXDATA];
	vector<struct AccountRecvBuf> vAccountRecvBuf;
	char* caRecvBuf = NULL;

	sprintf(caSendBuf, "B real_no_pass 01000 %10.10s %c %c 0 0 0 0##", pID, psource, pVersion);
	memset(uncaSendBuf, 0, sizeof(uncaSendBuf));
	memcpy(uncaSendBuf, caSendBuf, MAXDATA);
	pSocket->Send(uncaSendBuf, strlen(caSendBuf));


	int nRecv = 0;
	int nRecved = 0;
	do
	{
		struct AccountRecvBuf account_recv_buf;
		memset(&account_recv_buf, 0, sizeof(struct AccountRecvBuf));

		nRecv = pSocket->Recv(account_recv_buf.uncaRecvBuf, MAXDATA);
		if(nRecv > 0)
		{
			account_recv_buf.nRecved = nRecv;
			vAccountRecvBuf.push_back(account_recv_buf);
			nRecved += nRecv;
		}
	}
	while(nRecv != 0);

	caRecvBuf = new char[nRecved];

	memset(caRecvBuf, 0, nRecved);

	int nIndex = 0;
	for(vector<struct AccountRecvBuf>::iterator iter = vAccountRecvBuf.begin(); iter != vAccountRecvBuf.end(); iter++)
	{
		memcpy(caRecvBuf + nIndex, iter->uncaRecvBuf, iter->nRecved);
		nIndex += iter->nRecved;
	}
	delete pSocket;

	vector<struct AccountItem> vAccountItem;
	
	char* pch = NULL;
	pch = strtok(caRecvBuf, "#");
	while(pch != NULL)
	{
		fprintf(stderr, "%s\n",pch);
		if(strstr(pch, m_strService.c_str()))
		{
			struct AccountItem account_item;
			memset(&account_item, 0, sizeof(struct AccountItem));

			strcpy(account_item.caItem, pch);
			vAccountItem.push_back(account_item);
		}
		pch = strtok(NULL, "#");
	}
	delete [] caRecvBuf;

	char caItem[10][128];
	int nItemCount = 0;
	for(vector<struct AccountItem>::iterator iter = vAccountItem.begin(); iter != vAccountItem.end(); iter++)
	{
		memset(caItem, 0, sizeof(caItem));

		pch = strtok(iter->caItem, ",");
		while(pch != NULL)
		{
			strcpy(caItem[nItemCount], pch);
			nItemCount++;
			pch = strtok(NULL, ",");
		}
		nItemCount = 0;

		struct AccountMessage account_message;
		memset(&account_message, 0, sizeof(struct AccountMessage));

		string strBranch(caItem[2]);
		memcpy(account_message.caMessage, caItem[2], 10);

		string strAccount(caItem[3]);
		memcpy(account_message.caMessage + 10, caItem[3], 10);
		if(m_strService.compare("OS") == 0)
		{
			if(strlen(caItem[3]) == 7)
			{
				strAccount.insert(0, 1, '0');
				memcpy(account_message.caMessage + 11, account_message.caMessage + 10, 7);
				memset(account_message.caMessage + 10, '0', 1);
			}
		}

		string strSubAccount(caItem[5]);
		memcpy(account_message.caMessage + 20, caItem[5], 10);
		if(m_strService.compare("OS") == 0)
		{
			if(strlen(caItem[5]) == 7)
			{
				strSubAccount.insert(0, 1, '0');
				memcpy(account_message.caMessage + 21, account_message.caMessage + 20, 7);
				memset(account_message.caMessage + 20, '0', 1);
			}
		}

		vAccountMessage.push_back(account_message);

		string strBranchAccount = m_strService + strBranch + strAccount + strSubAccount;
		m_mBranchAccount.insert(std::pair<string, string>(strBranchAccount, strBranchAccount));
	}
}

int CCVClient::GetClientSocket()
{
	return m_ClientAddrInfo.nSocket;
}
