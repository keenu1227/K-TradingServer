#ifndef SKTANDEM_H_
#define SKTANDEM_H_

#include "../../include/CVTIGNode.h"
#include "../../include/CVTIGService.h"
#include <vector>
#include <string>

using namespace std;

class CSKTandem
{
	private:
		vector<struct TSKTIGNode*> m_vNode;
		vector<struct TSKTIGService*> m_vService;

	protected:
		int GetServiceCount(char* pService);
		int GetServiceFirstIndex(char* pService);

	public:
		CSKTandem();
		virtual ~CSKTandem();

		const TSKTIGNode* GetNode(int nIndex);
		const TSKTIGService* GetService(int nIndex, char* pService);
};
#endif
