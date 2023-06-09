#pragma once
#include <stdio.h>
#include <string>
#include <WinSock2.h>
#include <string>
#include <map>

#include "RtspSession.h"

class TcpConnection
{
public:
	TcpConnection();
	~TcpConnection();

	int init();
	void getRecvInfo(SOCKET &s, std::string &data);
	int doSelect();
	std::string getClntIp() { return std::string(m_clntIp); }

private:
	SOCKET m_tcpSocket;
	std::map<SOCKET, RtspSession*> m_clientSocketsMap;
	SOCKET m_currentClientSocket;
	char *m_recvBuffer;
	char m_clntIp[40];

	//����selectģ��
	fd_set m_readFdSet;
	fd_set m_readFdSetCopy;
};
