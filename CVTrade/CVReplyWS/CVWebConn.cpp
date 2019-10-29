#include <iostream>
#include <unistd.h>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <sys/time.h>
#include <fstream>
#include <curl/curl.h>

#include "CVWebConns.h"
#include "CVWebConn.h"
#include "CVGlobal.h"
#include "Include/CVTSFormat.h"
#include "CVQueueNodes.h"

using namespace std;

extern void FprintfStderrLog(const char* pCause, int nError, int nData, const char* pFile = NULL, int nLine = 0, 
			     unsigned char* pMessage1 = NULL, int nMessage1Length = 0, unsigned char* pMessage2 = NULL, int nMessage2Length = 0);

static size_t getResponse(char *contents, size_t size, size_t nmemb, void *userp)
{
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
}


CCVServer::CCVServer(string strWeb, string strQstr, string strName, TCVRequestMarket rmRequestMarket)
{
	m_shpClient = NULL;
	m_pHeartbeat = NULL;

	m_strWeb = strWeb;
	m_strQstr = strQstr;
	m_strName = strName;
	m_ssServerStatus = ssNone;

	m_rmRequestMarket = rmRequestMarket;
	pthread_mutex_init(&m_pmtxServerStatusLock, NULL);

	try
	{
		m_pClientSocket = new CCVClientSocket(this);
		m_pClientSocket->Connect( m_strWeb, m_strQstr, m_strName, CONNECT_WEBSOCK);//start
	}
	catch (exception& e)
	{
		FprintfStderrLog("SERVER_NEW_SOCKET_ERROR", -1, 0, NULL, 0, (unsigned char*)e.what(), strlen(e.what()));
	}
}

CCVServer::~CCVServer() 
{
	m_shpClient = NULL;
	if(m_pClientSocket)
	{
		SetStatus(ssBreakdown);
		delete m_pClientSocket;
		m_pClientSocket = NULL;
	}

	if(m_pHeartbeat)
	{
		delete m_pHeartbeat;
		m_pHeartbeat = NULL;
	}

	pthread_mutex_destroy(&m_pmtxServerStatusLock);
}

void* CCVServer::Run()
{
	sprintf(m_caPthread_ID, "%020lu", Self());

	CCVClients* pClients = NULL;
	try
	{
		pClients = CCVClients::GetInstance();

		if(pClients == NULL)
			throw "GET_CLIENTS_ERROR";
	}
	catch(const char* pErrorMessage)
	{
		FprintfStderrLog(pErrorMessage, -1, 0, __FILE__, __LINE__);
		return NULL;
	}
	while(m_ssServerStatus != ssBreakdown)
	{
		try
		{
			SetStatus(ssNone);
			m_cfd.run();
			SetStatus(ssBreakdown);
			break;
		}
		catch (exception& e)
		{
			break;;
		}
	}
 	return NULL;
}

void CCVServer::OnConnect()
{
	unsigned char msg[BUFFERSIZE];

	if(m_ssServerStatus == ssNone)
	{
		try {
			m_cfd.set_access_channels(websocketpp::log::alevel::all);
			m_cfd.clear_access_channels(websocketpp::log::alevel::frame_payload|websocketpp::log::alevel::frame_header|websocketpp::log::alevel::control);
			m_cfd.set_error_channels(websocketpp::log::elevel::all);

			m_cfd.init_asio();

			try
			{
				m_pHeartbeat = new CCVHeartbeat(this);
				memset(msg, BUFFERSIZE, 0);
				sprintf((char*)msg, "add heartbeat in %s service.", m_strName.c_str());
				FprintfStderrLog("NEW_HEARTBEAT_CREATE", -1, 0, __FILE__, __LINE__, msg, strlen((char*)msg));
				m_heartbeat_count = 0;
			}
			catch (exception& e)
			{
				FprintfStderrLog("NEW_HEARTBEAT_ERROR", -1, 0, __FILE__, __LINE__,
					(unsigned char*)m_caPthread_ID, sizeof(m_caPthread_ID), (unsigned char*)e.what(), strlen(e.what()));
				SetStatus(ssBreakdown);
				return;
			}

			if(m_strName == "ORDER_REPLY") {
				sprintf((char*)msg, "set timer to %d sec.", HEARTBEAT_INTERVAL_MIN);
				FprintfStderrLog("HEARTBEAT_TIMER_CONFIG", -1, 0, __FILE__, __LINE__, msg, strlen((char*)msg));
				m_pHeartbeat->SetTimeInterval(HEARTBEAT_INTERVAL_MIN);
				m_cfd.set_message_handler(bind(&OnData_Order_Reply,&m_cfd,::_1,::_2));
			}

			else {
				FprintfStderrLog("Exchange config error", -1, 0);	
			}
			string uri = m_strWeb + m_strQstr;

			m_cfd.set_tls_init_handler(std::bind(&CB_TLS_Init, m_strWeb.c_str(), ::_1));

			websocketpp::lib::error_code errcode;

			m_conn = m_cfd.get_connection(uri, errcode);

			if (errcode) {
				cout << "could not create connection because: " << errcode.message() << endl;
				SetStatus(ssBreakdown);
				return;
			}

			m_cfd.connect(m_conn);
			m_cfd.get_alog().write(websocketpp::log::alevel::app, "Connecting to " + uri);

		}  catch (websocketpp::exception const & ecp) {
			cout << ecp.what() << endl;
		}

		Start();
	}
	else if(m_ssServerStatus == ssReconnecting)
	{
		if(m_strName == "ORDER_REPLY") {
			m_cfd.set_message_handler(bind(&OnData_Order_Reply,&m_cfd,::_1,::_2));

		}
		string uri = m_strWeb + m_strQstr;

		m_cfd.set_tls_init_handler(std::bind(&CB_TLS_Init, m_strWeb.c_str(), ::_1));

		websocketpp::lib::error_code errcode;

		m_conn = m_cfd.get_connection(uri, errcode);

		if (errcode) {
			cout << "could not create connection because: " << errcode.message() << endl;
			SetStatus(ssBreakdown);
			return;
		}

		m_cfd.connect(m_conn);
		m_cfd.get_alog().write(websocketpp::log::alevel::app, "Connecting to " + uri);

		if(m_pHeartbeat)
		{
			m_pHeartbeat->TriggerGetReplyEvent();//reset heartbeat
		}
		else
		{
			FprintfStderrLog("HEARTBEAT_NULL_ERROR", -1, 0, __FILE__, __LINE__, (unsigned char*)m_caPthread_ID, sizeof(m_caPthread_ID));
		}
		SetStatus(ssNone);
		FprintfStderrLog("RECONNECT_SUCCESS", 0, 0, NULL, 0, (unsigned char*)m_caPthread_ID, sizeof(m_caPthread_ID));
	}
	else
	{
		FprintfStderrLog("SERVER_STATUS_ERROR", -1, m_ssServerStatus, __FILE__, __LINE__, (unsigned char*)m_caPthread_ID, sizeof(m_caPthread_ID));
	}
}

void CCVServer::OnDisconnect()
{
	sleep(5);

	m_pClientSocket->Connect( m_strWeb, m_strQstr, m_strName, CONNECT_WEBSOCK);//start & reset heartbeat
}

void CCVServer::Binance_Update(json* jtable)
{
        char update_str[MAXDATA];
        string response, exchagne_data[30];

	if((*jtable)["e"] == "ORDER_TRADE_UPDATE")
	{
		exchagne_data[0] = ((*jtable)["accountId"].dump());
		exchagne_data[0] = exchagne_data[0].substr(0, exchagne_data[0].length());

		exchagne_data[1] = ((*jtable)["o"]["i"].dump());
		exchagne_data[1] = exchagne_data[1].substr(0, exchagne_data[1].length());

		exchagne_data[2] = ((*jtable)["o"]["s"].dump());
		exchagne_data[2] = exchagne_data[2].substr(1, exchagne_data[2].length()-2);

		exchagne_data[3] = ((*jtable)["o"]["X"].dump());
		exchagne_data[3] = exchagne_data[3].substr(1, exchagne_data[3].length()-2);

		exchagne_data[4] = ((*jtable)["o"]["c"].dump());
		exchagne_data[4] = exchagne_data[4].substr(1, exchagne_data[4].length()-2);

		exchagne_data[5] = ((*jtable)["o"]["ap"].dump());
		exchagne_data[5] = exchagne_data[5].substr(1, exchagne_data[5].length()-2);

		exchagne_data[6] = ((*jtable)["o"]["q"].dump());
		exchagne_data[6] = exchagne_data[6].substr(1, exchagne_data[6].length()-2);

		exchagne_data[7] = ((*jtable)["o"]["z"].dump());
		exchagne_data[7] = exchagne_data[7].substr(1, exchagne_data[7].length()-2);

		exchagne_data[8] = ((*jtable)["o"]["l"].dump());
		exchagne_data[8] = exchagne_data[8].substr(1, exchagne_data[8].length()-2);

		exchagne_data[9] = ((*jtable)["o"]["f"].dump());
		exchagne_data[9] = exchagne_data[9].substr(1, exchagne_data[9].length()-2);

		exchagne_data[10] = ((*jtable)["o"]["o"].dump());
		exchagne_data[10] = exchagne_data[10].substr(1, exchagne_data[10].length()-2);

		exchagne_data[11] = ((*jtable)["o"]["S"].dump());
		exchagne_data[11] = exchagne_data[10].substr(1, exchagne_data[10].length()-2);

		exchagne_data[12] = ((*jtable)["o"]["sp"].dump());
		exchagne_data[12] = exchagne_data[10].substr(1, exchagne_data[10].length()-2);

		exchagne_data[13] = ((*jtable)["o"]["T"].dump());
		exchagne_data[13] = exchagne_data[10].substr(0, exchagne_data[10].length());

		sprintf(update_str, "http://tm1.cryptovix.com.tw:2011/mysql?db=cryptovix&query=update%%20binance_order_history%%20set%%20order_status=%%27%s%%27,match_qty=%%27%s%%27,match_price=%%27%s%%27,update_user=%%27reply.server%%27%20where%%20order_no=%%27%s%%27", exchagne_data[3].c_str(), exchagne_data[7].c_str(), exchagne_data[5].c_str(), exchagne_data[1].c_str());

		printf("=============\n%s\n=============\n", update_str);
		CURL *curl = curl_easy_init();
		curl_easy_setopt(curl, CURLOPT_URL, update_str);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, getResponse);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
		curl_easy_perform(curl);
		curl_easy_cleanup(curl);
		
	}
}

void CCVServer::Bitmex_Update(json* jtable)
{
        char update_match_str[MAXDATA], insert_str[MAXDATA], update_order_str[MAXDATA];
        string response, exchagne_data[30];
	if((*jtable)["table"] == "execution")
	{
		exchagne_data[0] = ((*jtable)["data"][0]["account"].dump());
		exchagne_data[0] = exchagne_data[0].substr(0, exchagne_data[0].length());

		exchagne_data[1] = ((*jtable)["data"][0]["execID"].dump());
		exchagne_data[1] = exchagne_data[1].substr(1, exchagne_data[1].length()-2);

		exchagne_data[2] = ((*jtable)["data"][0]["symbol"].dump());
		exchagne_data[2] = exchagne_data[2].substr(1, exchagne_data[2].length()-2);

		exchagne_data[3] = ((*jtable)["data"][0]["side"].dump());
		exchagne_data[3] = exchagne_data[3].substr(1, exchagne_data[3].length()-2);

		exchagne_data[4] = ((*jtable)["data"][0]["lastPx"].dump());
		exchagne_data[4] = exchagne_data[4].substr(0, exchagne_data[4].length());

		exchagne_data[5] = ((*jtable)["data"][0]["lastQty"].dump());
		exchagne_data[5] = exchagne_data[5].substr(0, exchagne_data[5].length());

		exchagne_data[6] = ((*jtable)["data"][0]["cumQty"].dump());
		exchagne_data[6] = exchagne_data[6].substr(0, exchagne_data[6].length());

		exchagne_data[7] = ((*jtable)["data"][0]["leavesQty"].dump());
		exchagne_data[7] = exchagne_data[7].substr(0, exchagne_data[7].length());

		exchagne_data[8] = ((*jtable)["data"][0]["execType"].dump());
		exchagne_data[8] = exchagne_data[8].substr(1, exchagne_data[8].length()-2);

		exchagne_data[9] = ((*jtable)["data"][0]["transactTime"].dump());
		exchagne_data[9] = exchagne_data[9].substr(1, 19);

		exchagne_data[10] = ((*jtable)["data"][0]["commission"].dump());
		exchagne_data[10] = exchagne_data[10].substr(0, exchagne_data[10].length());

		exchagne_data[11] = ((*jtable)["data"][0]["execComm"].dump());
		exchagne_data[11] = exchagne_data[11].substr(0, exchagne_data[11].length());

		exchagne_data[12] = ((*jtable)["data"][0]["orderID"].dump());
		exchagne_data[12] = exchagne_data[12].substr(1, exchagne_data[12].length()-2);

		exchagne_data[13] = ((*jtable)["data"][0]["price"].dump());
		exchagne_data[13] = exchagne_data[13].substr(0, exchagne_data[13].length());

		exchagne_data[14] = ((*jtable)["data"][0]["orderQty"].dump());
		exchagne_data[14] = exchagne_data[14].substr(0, exchagne_data[14].length());

		exchagne_data[15] = ((*jtable)["data"][0]["ordType"].dump());
		exchagne_data[15] = exchagne_data[15].substr(1, exchagne_data[15].length()-2);

		exchagne_data[16] = ((*jtable)["data"][0]["ordStatus"].dump());
		exchagne_data[16] = exchagne_data[16].substr(1, exchagne_data[16].length()-2);

		exchagne_data[17] = ((*jtable)["data"][0]["currency"].dump());
		exchagne_data[17] = exchagne_data[17].substr(1, exchagne_data[17].length()-2);

		exchagne_data[18] = ((*jtable)["data"][0]["settlCurrency"].dump());
		exchagne_data[18] = exchagne_data[18].substr(1, exchagne_data[18].length()-2);

		exchagne_data[19] = ((*jtable)["data"][0]["clOrdID"].dump());
		exchagne_data[19] = exchagne_data[19].substr(1, exchagne_data[19].length()-2);

		exchagne_data[20] = ((*jtable)["data"][0]["text"].dump());
		exchagne_data[20] = exchagne_data[20].substr(1, exchagne_data[20].length()-2);

		sprintf(insert_str, "http://tm1.cryptovix.com.tw:2011/mysql?db=cryptovix&query=insert%%20into%%20bitmex_match_history%%20set%%20exchange=%27BITMEX%27,account=%%27%s%%27,match_no=%%27%s%%27,symbol=%%27%s%%27,side=%%27%s%%27,match_cum_qty=%%27%s%%27,remaining_qty=%%27%s%%27,match_type=%%27%s%%27,match_time=%%27%s%%27,order_no=%%27%s%%27,order_qty=%%27%s%%27,order_type=%%27%s%%27,order_status=%%27%s%%27,quote_currency=%%27%s%%27,settlement_currency=%%27%s%%27,serial_no=%%27%s%%27,remark=%%27%s%%27", exchagne_data[0].c_str(), exchagne_data[1].c_str(), exchagne_data[2].c_str(), exchagne_data[3].c_str(), exchagne_data[6].c_str(), exchagne_data[7].c_str(), exchagne_data[8].c_str(), exchagne_data[9].c_str(), exchagne_data[12].c_str(), exchagne_data[14].c_str(), exchagne_data[15].c_str(), exchagne_data[16].c_str(), exchagne_data[17].c_str(), exchagne_data[18].c_str(), exchagne_data[19].c_str(), exchagne_data[20].c_str());

		if(exchagne_data[4] != "null")
			sprintf(insert_str, "%s,match_price=%%27%s%%27", insert_str, exchagne_data[4].c_str());
		if(exchagne_data[5] != "null")
			sprintf(insert_str, "%s,match_qty=%%27%s%%27", insert_str, exchagne_data[5].c_str());
		if(exchagne_data[10] != "null")
			sprintf(insert_str, "%s,commission_rate=%%27%s%%27", insert_str, exchagne_data[10].c_str());
		if(exchagne_data[11] != "null")
			sprintf(insert_str, "%s,commission=%%27%s%%27", insert_str, exchagne_data[11].c_str());
		if(exchagne_data[13] != "null")
			sprintf(insert_str, "%s,order_price=%%27%s%%27", insert_str, exchagne_data[13].c_str());

		sprintf(insert_str, "%s,insert_user=%%27reply.server%%27,update_user=%%27reply.server%%27", insert_str);

		sprintf(update_match_str, "http://tm1.cryptovix.com.tw:2011/mysql?db=cryptovix&query=update%%20bitmex_match_history%%20set%%20match_cum_qty=%%27%s%%27,remaining_qty=%%27%s%%27,match_type=%%27%s%%27,match_time=%%27%s%%27,order_status=%%27%s%%27,remark=%%27%s%%27", exchagne_data[6].c_str(), exchagne_data[7].c_str(), exchagne_data[8].c_str(), exchagne_data[9].c_str(), exchagne_data[16].c_str(), exchagne_data[20].c_str());

		if(exchagne_data[4] != "null")
			sprintf(update_match_str, "%s,match_price=%%27%s%%27", update_match_str, exchagne_data[4].c_str());
		if(exchagne_data[5] != "null")
			sprintf(update_match_str, "%s,match_qty=%%27%s%%27", update_match_str, exchagne_data[5].c_str());
		if(exchagne_data[10] != "null")
			sprintf(update_match_str, "%s,commission_rate=%%27%s%%27", update_match_str, exchagne_data[10].c_str());
		if(exchagne_data[11] != "null")
			sprintf(update_match_str, "%s,commission=%%27%s%%27", update_match_str, exchagne_data[11].c_str());
		sprintf(update_match_str, "%s,update_user=%%27reply.server%%27", update_match_str);
		sprintf(update_match_str, "%s%%20where%%20serial_no=%%27%s%%27", update_match_str, exchagne_data[19].c_str());

		sprintf(update_order_str, "http://tm1.cryptovix.com.tw:2011/mysql?db=cryptovix&query=update%%20bitmex_order_history%%20set%%20remaining_qty=%%27%s%%27,order_status=%%27%s%%27,remark=%%27%s%%27", exchagne_data[7].c_str(), exchagne_data[16].c_str(), exchagne_data[20].c_str());
		if(exchagne_data[4] != "null")
			sprintf(update_order_str, "%s,match_price=%%27%s%%27", update_order_str, exchagne_data[4].c_str());
		if(exchagne_data[5] != "null")
			sprintf(update_order_str, "%s,match_qty=%%27%s%%27", update_order_str, exchagne_data[5].c_str());
		sprintf(update_order_str, "%s,update_user=%%27reply.server%%27", update_order_str);
		sprintf(update_order_str, "%s%%20where%%20serial_no=%%27%s%%27", update_order_str, exchagne_data[19].c_str());

		CURL *curl = curl_easy_init();

		for(int i=0 ; i<strlen(insert_str) ; i++)
		{
			if(insert_str[i] == ' ')
				insert_str[i] = '+';
		}
		for(int i=0 ; i<strlen(update_match_str) ; i++)
		{
			if(update_match_str[i] == ' ')
				update_match_str[i] = '+';
		}
		for(int i=0 ; i<strlen(update_order_str) ; i++)
		{
			if(update_order_str[i] == ' ')
				update_order_str[i] = '+';
		}

		if(exchagne_data[16] == "New") {
			printf("=============insert:\n%s\n=============\n", insert_str);
			curl_easy_setopt(curl, CURLOPT_URL, insert_str);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, getResponse);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
			curl_easy_perform(curl);
		}
		else {
			printf("=============update match:\n%s\n=============\n", update_match_str);
			curl_easy_setopt(curl, CURLOPT_URL, update_match_str);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, getResponse);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
			curl_easy_perform(curl);
			printf("=============update order:\n%s\n=============\n", update_order_str);
			curl_easy_setopt(curl, CURLOPT_URL, update_order_str);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, getResponse);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
			curl_easy_perform(curl);
		}
		curl_easy_cleanup(curl);
	}
}

void CCVServer::OnData_Order_Reply(client* c, websocketpp::connection_hdl con, client::message_ptr msg)
{
#ifdef DEBUG
	printf("[on_message_bitmex]\n");
#endif
	static char netmsg[BUFFERSIZE];
	static char timemsg[9];
	string time_str, symbol_str;
	string strname = "ORDER_REPLY";
	string str = msg->get_payload();
	static CCVServer* pServer = CCVServers::GetInstance()->GetServerByName(strname);
	pServer->m_heartbeat_count = 0;
	pServer->m_pHeartbeat->TriggerGetReplyEvent();

	//cout << setw(4) << str << endl;

	if(str[0] == '{') {
		json jtable = json::parse(str.c_str());

		if(jtable["table"] != "margin") {

			cout << setw(4) << jtable << endl;

			if(jtable["exchange"] == "BINANCE") {
				pServer->Binance_Update(&jtable);
			}
			else if(jtable["exchange"] == "BITMEX") {
				pServer->Bitmex_Update(&jtable);
			}
			else {
				FprintfStderrLog("UNKNOWN EXCHANGE", -1, 0, jtable["exchange"].dump().c_str(), jtable["exchange"].dump().length(),  NULL, 0);
			}
		}
			
	}

	CCVQueueDAO* pQueueDAO = CCVQueueDAOs::GetInstance()->GetDAO();
	//pQueueDAO->SendData(netmsg, strlen(netmsg));
}

void CCVServer::OnHeartbeatLost()
{
	FprintfStderrLog("HEARTBEAT LOST", -1, 0, m_strName.c_str(), m_strName.length(),  NULL, 0);
	SetStatus(ssBreakdown);
}

void CCVServer::OnHeartbeatRequest()
{
	FprintfStderrLog("HEARTBEAT REQUEST", -1, 0, m_strName.c_str(), m_strName.length(),  NULL, 0);
	char replymsg[BUFFERSIZE];
	memset(replymsg, 0, BUFFERSIZE);

	if(m_heartbeat_count <= HTBT_COUNT_LIMIT)
	{
		auto msg = m_conn->send("ping");
		sprintf(replymsg, "%s send PING message and response (%s)\n", m_strName.c_str(), msg.message().c_str());
		FprintfStderrLog("PING/PONG protocol", -1, 0, replymsg, strlen(replymsg),  NULL, 0);

		if(msg.message() != "SUCCESS" && msg.message() != "Success")
		{
			FprintfStderrLog("Server PING/PONG Fail", -1, 0, m_strName.c_str(), m_strName.length(),  NULL, 0);
			SetStatus(ssBreakdown);
		}
		else
		{
			m_heartbeat_count++;
		}
	}
	else
	{
		SetStatus(ssBreakdown);
	}
}

void CCVServer::OnHeartbeatError(int nData, const char* pErrorMessage)
{
	SetStatus(ssBreakdown);
}

bool CCVServer::RecvAll(const char* pWhat, unsigned char* pBuf, int nToRecv)
{
	return false;
}

bool CCVServer::SendAll(const char* pWhat, const unsigned char* pBuf, int nToSend)
{
	return false;
}

void CCVServer::ReconnectSocket()
{
	if(m_pClientSocket)
	{
		sleep(5);
		SetStatus(ssReconnecting);
		m_pClientSocket->Connect( m_strWeb, m_strQstr, m_strName, CONNECT_WEBSOCK);//start
	}
	else
	{
		printf("m_pClientSocket fail\n");
		SetStatus(ssBreakdown);
	}
}

void CCVServer::SetCallback(shared_ptr<CCVClient>& shpClient)
{
	m_shpClient = shpClient;
}

void CCVServer::SetRequestMessage(unsigned char* pRequestMessage, int nRequestMessageLength)
{
}

void CCVServer::SetStatus(TCVServerStatus ssServerStatus)
{
	pthread_mutex_lock(&m_pmtxServerStatusLock);

	m_ssServerStatus = ssServerStatus;

	pthread_mutex_unlock(&m_pmtxServerStatusLock);
}

TCVServerStatus CCVServer::GetStatus()
{
	return m_ssServerStatus;
}

context_ptr CCVServer::CB_TLS_Init(const char * hostname, websocketpp::connection_hdl) {
	context_ptr ctx = websocketpp::lib::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12 );
	return ctx;
}

void CCVServer::OnRequest()
{
}

void CCVServer::OnRequestError(int, const char*)
{
}

void CCVServer::OnData(unsigned char*, int)
{
}
