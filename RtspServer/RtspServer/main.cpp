#include "TcpConnection.h"
#include "RtspSession.h"

int main()
{
	TcpConnection tcpConnection;
	RtspSession rtspSession;
	if (tcpConnection.init() < 0) {
		return -1;
	}

	while (1) {
		int recvSize = tcpConnection.doSelect();
		if (recvSize > 0) {
		}
	}
}