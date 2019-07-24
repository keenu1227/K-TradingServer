#include <iostream>
#include <cstring>
#include <cstdio>
#include <vector>
#include <assert.h>
#include <glib.h>
#include <unistd.h>

#include "CVServer.h"
#include "CVWebConns.h"
#include "CVQueueNodes.h"

using namespace std;

void InitialGlobal();

extern void FprintfStderrLog(const char* pCause, int nError, int nData, const char* pFile = NULL, int nLine = 0,
                             unsigned char* pMessage1 = NULL, int nMessage1Length = 0, 
                             unsigned char* pMessage2 = NULL, int nMessage2Length = 0);

void ReadConfigFile(string strConfigFileName, string strSection, struct TSKConfig &struConfig);
void ReadClientConfigFile(string strConfigFileName, string& strListenPort, string& strHeartBeatTime, string &strEPIDNum);
void ReadQueueDAOConfigFile(string strConfigFileName,string&, int&, key_t&, key_t&, key_t&, key_t&);
#define DECLARE_CONFIG_DATA(CONFIG)\
	struct TSKConfig stru##CONFIG;\
	memset(&stru##CONFIG, 0, sizeof(struct TSKConfig));\
	;

int main()
{
        int nNumberOfQueueDAO;
        key_t kQueueDAOWriteStartKey;
        key_t kQueueDAOWriteEndKey;
        key_t kQueueDAOReadStartKey;
        key_t kQueueDAOReadEndKey;
	string strService;

	InitialGlobal();
        setbuf(stdout, NULL);
        signal(SIGPIPE, SIG_IGN );
        srand(time(NULL));

	DECLARE_CONFIG_DATA(TSConfig);

	string strListenPort, strHeartBeatTime, strEPIDNum;
	ReadClientConfigFile("../ini/CVQuote.ini", strListenPort, strHeartBeatTime, strEPIDNum);

	int nService = 0;
	ReadConfigFile("../ini/CVQuote.ini", "EXCHANGE", struTSConfig);

	//Web connection service.
	CSKServers* pServers = NULL;
	try
	{
		pServers = CSKServers::GetInstance();
		if(pServers == NULL)
			throw "GET_WEB_ERROR";

		pServers->SetConfiguration(&struTSConfig);
		pServers->StartUpServers();
	}
	catch (const char* pErrorMessage)
	{
		FprintfStderrLog(pErrorMessage, -1, 0, __FILE__, __LINE__);
	}

	//Queue init.
        ReadQueueDAOConfigFile("../ini/CVQuote.ini", strService, nNumberOfQueueDAO, kQueueDAOWriteStartKey, kQueueDAOWriteEndKey, kQueueDAOReadStartKey, kQueueDAOReadEndKey);
	//printf("%d, %d, %d, %d\n", kQueueDAOWriteStartKey, kQueueDAOWriteEndKey, kQueueDAOReadStartKey, kQueueDAOReadEndKey);
        CSKQueueDAOs* pQueueDAOs = CSKQueueDAOs::GetInstance();
        assert(pQueueDAOs);

        pQueueDAOs->SetConfiguration(strService, nNumberOfQueueDAO, kQueueDAOWriteStartKey, kQueueDAOWriteEndKey, kQueueDAOReadStartKey, kQueueDAOReadEndKey);
        pQueueDAOs->StartUpDAOs();

	//Server connection service.
	CSKClients* pClients = NULL;
	try
	{
		pClients = CSKClients::GetInstance();
		if(pClients == NULL)
			throw "GET_SERVER_MANAGEMENT_ERROR";

		pClients->SetConfiguration(strListenPort, strHeartBeatTime, strEPIDNum, nService);

		//Disconnection check.
		while(1)
		{
			pClients->CheckClientVector();
			sleep(1);
		}
	}
	catch (const char* pErrorMessage)
	{
		FprintfStderrLog(pErrorMessage, -1, 0, __FILE__, __LINE__);
	}

	return 0;
}

void ReadConfigFile(string strConfigFileName, string strSection, struct TSKConfig &struConfig)
{
	GKeyFile *keyfile;
	GKeyFileFlags flags;
	GError *error = NULL;

	keyfile = g_key_file_new();
	flags = GKeyFileFlags(G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS);

	assert(g_key_file_load_from_file(keyfile, strConfigFileName.c_str(), flags, &error));
	struConfig.nServerCount = g_key_file_get_integer(keyfile, strSection.c_str(), "ServerCount", NULL);//todo->check?

	char caWeb[5];
	char caQstr[7];
	char caName[7];

	for(int i=0;i<struConfig.nServerCount;i++)
	{
		memset(caWeb, 0, sizeof(caWeb));
		memset(caQstr, 0, sizeof(caQstr));
		memset(caName, 0, sizeof(caName));

		sprintf(caWeb, "WEB%02d", i+1);
		sprintf(caQstr, "QSTR%02d", i+1);
		sprintf(caName, "NAME%02d", i+1);

		struct TSKServerInfo* pstruServerInfo = new struct TSKServerInfo;//destruct
		pstruServerInfo->strWeb  = g_key_file_get_string(keyfile, strSection.c_str(), caWeb, NULL);
		pstruServerInfo->strQstr = g_key_file_get_string(keyfile, strSection.c_str(), caQstr, NULL);
		pstruServerInfo->strName = g_key_file_get_string(keyfile, strSection.c_str(), caName, NULL);
		printf("Connect web: %s\n", pstruServerInfo->strWeb.c_str());
		printf("Query strnig: %s\n", pstruServerInfo->strQstr.c_str());
		printf("Exchange: %s\n", pstruServerInfo->strName.c_str());

		struConfig.vServerInfo.push_back(pstruServerInfo);
	}
}

void ReadClientConfigFile(string strConfigFileName, string& strListenPort, string& strHeartBeatTime, string& strEPIDNum)
{
	GKeyFile *keyfile;
	GKeyFileFlags flags;
	GError *error = NULL;
	keyfile = g_key_file_new();
	flags = GKeyFileFlags(G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS);
	assert(g_key_file_load_from_file(keyfile, strConfigFileName.c_str(), flags, &error));
	strEPIDNum       = g_key_file_get_string(keyfile, "SERVER", "EpidNum",     NULL);
	strListenPort    = g_key_file_get_string(keyfile, "SERVER", "ListenPort",    NULL);
	strHeartBeatTime = g_key_file_get_string(keyfile, "SERVER", "HeartBeatTime", NULL);
}

void ReadQueueDAOConfigFile(string strConfigFileName, string& strService, int& nNumberOfQueueDAO, key_t& kQueueDAOWriteStartKey, key_t& kQueueDAOWriteEndKey, key_t& kQueueDAOReadStartKey, key_t& kQueueDAOReadEndKey)
{
        GKeyFile *keyfile;
        GKeyFileFlags flags;
        GError *error = NULL;

        keyfile = g_key_file_new();
        flags = GKeyFileFlags(G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS);

        assert(g_key_file_load_from_file(keyfile, strConfigFileName.c_str(), flags, &error));

        strService = g_key_file_get_string(keyfile, "QUEUE", "Service", NULL);
        nNumberOfQueueDAO = g_key_file_get_integer(keyfile, "QUEUE", "NumberOfQueueDAO", NULL);
        kQueueDAOWriteStartKey = g_key_file_get_integer(keyfile, "QUEUE", "QueueNodeWriteStartKey", NULL);
        kQueueDAOWriteEndKey = g_key_file_get_integer(keyfile, "QUEUE", "QueueNodeWriteEndKey", NULL);
        kQueueDAOReadStartKey = g_key_file_get_integer(keyfile, "QUEUE", "QueueNodeReadStartKey", NULL);
        kQueueDAOReadEndKey = g_key_file_get_integer(keyfile, "QUEUE", "QueueNodeReadEndKey", NULL);
}

