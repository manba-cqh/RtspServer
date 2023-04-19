#include "RtspDef.h"
#include "TcpConnection.h"

#pragma comment(lib,"ws2_32")

TcpConnection::TcpConnection()
{
}

TcpConnection::~TcpConnection()
{
	closesocket(m_tcpSocket);
	for (int i = 0; i < m_clientSockets.size(); i++) {
		closesocket(m_clientSockets[i]);
	}
	//ע��WinSocket
	WSACleanup();

	if (m_recvBuffer) {
		delete m_recvBuffer;
		m_recvBuffer = nullptr;
	}
}

int TcpConnection::init()
{
	//��ʼ��WinSocket
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		printf("WSAStartup error\n");
		return -1;
	}

	m_tcpSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (m_tcpSocket < 0) {
		printf("socket error\n");
		return -1;
	}

	int on = 1;
	setsockopt(m_tcpSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(8554);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(m_tcpSocket, (struct sockaddr*)&addr, sizeof(struct sockaddr)) < 0) {
		printf("bind error\n");
		return -1;
	}

	if (listen(m_tcpSocket, 10) < 0)
	{
		printf("listen error\n");
		return -1;
	}

	FD_ZERO(&m_readFdSet);
	FD_SET(m_tcpSocket, &m_readFdSet);
	m_recvBuffer = new char[BUF_MAX_SIZE];

	return 0;
}

void TcpConnection::getRecvInfo(SOCKET &s, std::string& data)
{
	s = m_currentClientSocket;
	data = std::string(m_recvBuffer, strlen(m_recvBuffer) + 1);
}

int TcpConnection::doSelect()
{
	m_readFdSetCopy = m_readFdSet;
	int fdNum = ::select(0, &m_readFdSetCopy, nullptr, nullptr, nullptr);
	if (fdNum == SOCKET_ERROR) {
		printf("select error\n");
		return -1;
	}

	if (fdNum == 0) {
		return 0;
	}

	for (int i = 0; i < m_readFdSet.fd_count; i++) {
		if (FD_ISSET(m_readFdSet.fd_array[i], &m_readFdSetCopy)) {
			if (m_readFdSet.fd_array[i] == m_tcpSocket) {
				SOCKADDR_IN clntAddr;
				int clntAddrSize = sizeof(clntAddr);
				SOCKET clntSock = accept(m_tcpSocket, (SOCKADDR*)&clntAddr, &clntAddrSize);
				strcpy(m_clntIp, inet_ntoa(clntAddr.sin_addr));
				FD_SET(clntSock, &m_readFdSet);
				m_clientSockets.push_back(clntSock);
				printf("new connection\n");
				return 0;
			}
			else {
				int recvSize = recv(m_readFdSet.fd_array[i], m_recvBuffer, BUF_MAX_SIZE, 0);
				if (recvSize < 0) {
					return recvSize;
				}

				//�Զ˹ر�����
				if (recvSize == 0) {
					FD_CLR(m_readFdSet.fd_array[i], &m_readFdSet);
					closesocket(m_readFdSet.fd_array[i]);
					for (auto iter = m_clientSockets.begin(); iter != m_clientSockets.end(); iter++) {
						if (*iter == m_readFdSet.fd_array[i]) {
							m_clientSockets.erase(iter);
							break;
						}
					}
					return 0;
				}

				m_currentClientSocket = m_readFdSet.fd_array[i];
				m_recvBuffer[recvSize] = '\0';
				return recvSize + 1;
			}
		}
	}
}