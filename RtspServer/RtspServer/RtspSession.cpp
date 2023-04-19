#include <thread>

#include "RtspDef.h"
#include "RtspSession.h"
#include "rtp.h"



static int createUdpSocket()
{
	int sockfd;
	int on = 1;

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
		return -1;

	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));

	return sockfd;
}

static int bindSocketAddr(int sockfd, const char* ip, int port)
{
	struct sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(ip);

	if (bind(sockfd, (struct sockaddr*)&addr, sizeof(struct sockaddr)) < 0)
		return -1;

	return 0;
}

static inline int startCode3(char* buf)
{
	if (buf[0] == 0 && buf[1] == 0 && buf[2] == 1)
		return 1;
	else
		return 0;
}

static inline int startCode4(char* buf)
{
	if (buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 1)
		return 1;
	else
		return 0;
}

static char* findNextStartCode(char* buf, int len)
{
	int i;

	if (len < 3)
		return NULL;

	for (i = 0; i < len - 3; ++i)
	{
		if (startCode3(buf) || startCode4(buf))
			return buf;

		++buf;
	}

	if (startCode3(buf))
		return buf;

	return NULL;
}

static int getFrameFromH264File(FILE* fp, char* frame, int size) {
	int rSize, frameSize;
	char* nextStartCode;

	if (fp == nullptr)
		return -1;

	rSize = fread(frame, 1, size, fp);

	if (!startCode3(frame) && !startCode4(frame))
		return -1;

	nextStartCode = findNextStartCode(frame + 3, rSize - 3);
	if (!nextStartCode)
	{
		//lseek(fd, 0, SEEK_SET);
		//frameSize = rSize;
		return -1;
	}
	else
	{
		frameSize = (nextStartCode - frame);
		fseek(fp, frameSize - rSize, SEEK_CUR);

	}

	return frameSize;
}

static int rtpSendH264Frame(int serverRtpSockfd, const char* ip, int16_t port,
	struct RtpPacket* rtpPacket, char* frame, uint32_t frameSize)
{

	uint8_t naluType; // nalu第一个字节
	int sendBytes = 0;
	int ret;

	naluType = frame[0];

	printf("frameSize=%d \n", frameSize);

	if (frameSize <= RTP_MAX_PKT_SIZE) // nalu长度小于最大包长：单一NALU单元模式
	{

		//*   0 1 2 3 4 5 6 7 8 9
		//*  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		//*  |F|NRI|  Type   | a single NAL unit ... |
		//*  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

		memcpy(rtpPacket->payload, frame, frameSize);
		ret = rtpSendPacketOverUdp(serverRtpSockfd, ip, port, rtpPacket, frameSize);
		if (ret < 0)
			return -1;

		rtpPacket->rtpHeader.seq++;
		sendBytes += ret;
		if ((naluType & 0x1F) == 7 || (naluType & 0x1F) == 8) // 如果是SPS、PPS就不需要加时间戳
			goto out;
	}
	else // nalu长度小于最大包场：分片模式
	{

		//*  0                   1                   2
		//*  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
		//* +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		//* | FU indicator  |   FU header   |   FU payload   ...  |
		//* +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+



		//*     FU Indicator
		//*    0 1 2 3 4 5 6 7
		//*   +-+-+-+-+-+-+-+-+
		//*   |F|NRI|  Type   |
		//*   +---------------+



		//*      FU Header
		//*    0 1 2 3 4 5 6 7
		//*   +-+-+-+-+-+-+-+-+
		//*   |S|E|R|  Type   |
		//*   +---------------+


		int pktNum = frameSize / RTP_MAX_PKT_SIZE;       // 有几个完整的包
		int remainPktSize = frameSize % RTP_MAX_PKT_SIZE; // 剩余不完整包的大小
		int i, pos = 1;

		// 发送完整的包
		for (i = 0; i < pktNum; i++)
		{
			rtpPacket->payload[0] = (naluType & 0x60) | 28;
			rtpPacket->payload[1] = naluType & 0x1F;

			if (i == 0) //第一包数据
				rtpPacket->payload[1] |= 0x80; // start
			else if (remainPktSize == 0 && i == pktNum - 1) //最后一包数据
				rtpPacket->payload[1] |= 0x40; // end

			memcpy(rtpPacket->payload + 2, frame + pos, RTP_MAX_PKT_SIZE);
			ret = rtpSendPacketOverUdp(serverRtpSockfd, ip, port, rtpPacket, RTP_MAX_PKT_SIZE + 2);
			if (ret < 0)
				return -1;

			rtpPacket->rtpHeader.seq++;
			sendBytes += ret;
			pos += RTP_MAX_PKT_SIZE;
		}

		// 发送剩余的数据
		if (remainPktSize > 0)
		{
			rtpPacket->payload[0] = (naluType & 0x60) | 28;
			rtpPacket->payload[1] = naluType & 0x1F;
			rtpPacket->payload[1] |= 0x40; //end

			memcpy(rtpPacket->payload + 2, frame + pos, remainPktSize + 2);
			ret = rtpSendPacketOverUdp(serverRtpSockfd, ip, port, rtpPacket, remainPktSize + 2);
			if (ret < 0)
				return -1;

			rtpPacket->rtpHeader.seq++;
			sendBytes += ret;
		}
	}
	rtpPacket->rtpHeader.timestamp += 90000 / 25;
out:

	return sendBytes;

}

int clntRtpPort = -1;
int clntRtpPort1 = -1;
int serverRtpSockfd = -1, serverRtcpSockfd = -1;
std::string ClntIp;

RtspSession::RtspSession()
{

}

RtspSession::~RtspSession()
{

}

void RtspSession::doConversation(SOCKET s, std::string data, std::string clntIp)
{
	ClntIp = clntIp;
	std::thread t = std::thread(RtspSession::conversationFunc, s, data);
	t.detach();
}

void RtspSession::conversationFunc(SOCKET s, std::string data)
{
	char sendBuffer[1000];
	char method[40];
	char url[40];
	char version[40];
	int cseq;
	int clientRtpPort, clientRtcpPort;
	bool sendRtpFrameEnable = false;

	const char *sep = "\n";
	char* firstLine = strtok((char*)data.c_str(), sep);
	while (firstLine) {
		if (strstr(firstLine, "OPTIONS") ||
			strstr(firstLine, "DESCRIBE") ||
			strstr(firstLine, "SETUP") ||
			strstr(firstLine, "PLAY") ||
			strstr(firstLine, "TEARDOWN")) {
			if (sscanf(firstLine, "%s %s %s\r\n", &method, &url, &version) != 3) {
				printf("parse  error\n");
			}
		}
		else if (strstr(firstLine, "CSeq")) {
			if (sscanf(firstLine, "CSeq: %d\r\n", &cseq) != 1) {
				printf("parse CSeq error\n");
			}
		}
		else if (!strncmp(firstLine, "Transport:", strlen("Transport:"))) {
			//vlc
			if (sscanf(firstLine, "Transport: RTP/AVP;unicast;client_port=%d-%d\r\n",
				&clientRtpPort, &clientRtcpPort) != 2) {
				//ffplay
				if (sscanf(firstLine, "Transport: RTP/AVP/UDP;unicast;client_port=%d-%d\r\n",
					&clientRtpPort, &clientRtcpPort) != 2) {
					printf("parse Transport error \n");
				}
			}
			if (clntRtpPort == -1) {
				clntRtpPort = clientRtpPort;
			}
			else {
				clntRtpPort1 = clientRtpPort;
			}
		}

		firstLine = strtok(nullptr, sep);
	}

	if (!strcmp(method, "OPTIONS")) {
		if (handleOptionReq(sendBuffer, cseq)) {
			printf("handle OPTIONS error\n");
		}
	}
	else if (!strcmp(method, "DESCRIBE")) {
		if (handleDescribeReq(sendBuffer, cseq, url)) {
			printf("handle DESCRIBE ERROR\n");
		}
	}
	else if (!strcmp(method, "SETUP")) {
		if (handleSetupReq(sendBuffer, cseq, clientRtpPort)) {
			printf("handle DESCRIBE ERROR\n");
		}

		serverRtpSockfd = createUdpSocket();
		serverRtcpSockfd = createUdpSocket();

		if (serverRtpSockfd < 0 || serverRtcpSockfd < 0)
		{
			printf("failed to create udp socket\n");
			return;
		}

		if (bindSocketAddr(serverRtpSockfd, "0.0.0.0", SERVER_RTP_PORT) < 0 ||
			bindSocketAddr(serverRtcpSockfd, "0.0.0.0", SERVER_RTP_PORT + 1) < 0)
		{
			printf("failed to bind addr\n");
			return;
		}
	}
	else if (!strcmp(method, "PLAY")) {
		if (handlePlayReq(sendBuffer, cseq)) {
			printf("handle PLAY ERROR\n");
		}
		else {
			sendRtpFrameEnable = true;
		}
	}
	else if (!strcmp(method, "TEARDOWN")) {
	}

	send(s, sendBuffer, strlen(sendBuffer), 0);

	if (!strcmp(method, "PLAY") && sendRtpFrameEnable) {
		sendRtpFrame();
	}
}

int RtspSession::handleOptionReq(char* result, int cseq)
{
	sprintf(result, "RTSP/1.0 200 OK\r\n"
		"CSeq: %d\r\n"
		"Public: OPTIONS, DESCRIBE, SETUP, PLAY\r\n"
		"\r\n",
		cseq);

	return 0;
}

int RtspSession::handleDescribeReq(char* result, int cseq, char *url)
{
	char sdp[500];
	char localIp[100];

	sscanf(url, "rtsp://%[^:]:", localIp);

	sprintf(sdp, "v=0\r\n"
		"o=- 9%ld 1 IN IP4 %s\r\n"
		"t=0 0\r\n"
		"a=control:*\r\n"
		"m=video 0 RTP/AVP 96\r\n"
		"a=rtpmap:96 H264/90000\r\n"
		"a=control:track0\r\n",
		time(NULL), localIp);

	sprintf(result, "RTSP/1.0 200 OK\r\nCSeq: %d\r\n"
		"Content-Base: %s\r\n"
		"Content-type: application/sdp\r\n"
		"Content-length: %zu\r\n\r\n"
		"%s",
		cseq,
		url,
		strlen(sdp),
		sdp);

	return 0;
}

int RtspSession::handleSetupReq(char* result, int cseq, int clientRtpPort)
{
	sprintf(result, "RTSP/1.0 200 OK\r\n"
		"CSeq: %d\r\n"
		"Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=%d-%d\r\n"
		"Session: 66334873\r\n"
		"\r\n",
		cseq,
		clientRtpPort,
		clientRtpPort + 1,
		SERVER_RTP_PORT,
		SERVER_RTP_PORT + 1);

	return 0;
}

int RtspSession::handlePlayReq(char* result, int cseq)
{
	sprintf(result, "RTSP/1.0 200 OK\r\n"
		"CSeq: %d\r\n"
		"Range: npt=0.000-\r\n"
		"Session: 66334873; timeout=10\r\n\r\n",
		cseq);

	return 0;
}

int RtspSession::sendRtpFrame()
{
	int frameSize, startCode;
	char* frame = (char*)malloc(500000);
	struct RtpPacket* rtpPacket = (struct RtpPacket*)malloc(500000);
	FILE* fp = fopen("test.h264", "rb");
	if (!fp) {
		printf("读取 %s 失败\n", "test.h264");
		return -1;
	}
	rtpHeaderInit(rtpPacket, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_H264, 0,
		0, 0, 0x88923423);

	printf("start play\n");
	printf("client ip:%s\n", ClntIp.c_str());
	printf("client port:%d\n", clntRtpPort);

	int port;
	if (clntRtpPort1 == -1) {
		port = clntRtpPort;
	}
	else{
		port = clntRtpPort1;
	}

	while (true) {
		frameSize = getFrameFromH264File(fp, frame, 500000);
		if (frameSize < 0)
		{
			printf("读取%s结束,frameSize=%d \n", "test.h264", frameSize);
			break;
		}

		if (startCode3(frame))
			startCode = 3;
		else
			startCode = 4;

		frameSize -= startCode;
		rtpSendH264Frame(serverRtpSockfd, ClntIp.c_str(), port,
			rtpPacket, frame + startCode, frameSize);

		Sleep(40);
		//usleep(40000);//1000/25 * 1000
	}
	free(frame);
	free(rtpPacket);
}