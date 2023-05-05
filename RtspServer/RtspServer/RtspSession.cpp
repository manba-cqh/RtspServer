#include <thread>

extern "C" {
#include "base64/base64.h"
}

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

	uint8_t naluType; // nalu��һ���ֽ�
	int sendBytes = 0;
	int ret;

	naluType = frame[0];

	printf("frameSize=%d \n", frameSize);

	if (frameSize <= RTP_MAX_PKT_SIZE) // nalu����С������������һNALU��Ԫģʽ
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
		if ((naluType & 0x1F) == 7 || (naluType & 0x1F) == 8) // �����SPS��PPS�Ͳ���Ҫ��ʱ���
			goto out;
	}
	else // nalu����С������������Ƭģʽ
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


		int pktNum = frameSize / RTP_MAX_PKT_SIZE;       // �м��������İ�
		int remainPktSize = frameSize % RTP_MAX_PKT_SIZE; // ʣ�಻�������Ĵ�С
		int i, pos = 1;

		// ���������İ�
		for (i = 0; i < pktNum; i++)
		{
			rtpPacket->payload[0] = (naluType & 0x60) | 28;
			rtpPacket->payload[1] = naluType & 0x1F;

			if (i == 0) //��һ������
				rtpPacket->payload[1] |= 0x80; // start
			else if (remainPktSize == 0 && i == pktNum - 1) //���һ������
				rtpPacket->payload[1] |= 0x40; // end

			memcpy(rtpPacket->payload + 2, frame + pos, RTP_MAX_PKT_SIZE);
			ret = rtpSendPacketOverUdp(serverRtpSockfd, ip, port, rtpPacket, RTP_MAX_PKT_SIZE + 2);
			if (ret < 0)
				return -1;

			rtpPacket->rtpHeader.seq++;
			sendBytes += ret;
			pos += RTP_MAX_PKT_SIZE;
		}

		// ����ʣ�������
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



struct AdtsHeader {
	unsigned int syncword;  //12 bit ͬ���� '1111 1111 1111'��һ��ADTS֡�Ŀ�ʼ
	uint8_t id;        //1 bit 0����MPEG-4, 1����MPEG-2��
	uint8_t layer;     //2 bit ����Ϊ0
	uint8_t protectionAbsent;  //1 bit 1����û��CRC��0������CRC
	uint8_t profile;           //1 bit AAC����MPEG-2 AAC�ж�����3��profile��MPEG-4 AAC�ж�����6��profile��
	uint8_t samplingFreqIndex; //4 bit ������
	uint8_t privateBit;        //1bit ����ʱ����Ϊ0������ʱ����
	uint8_t channelCfg;        //3 bit ��������
	uint8_t originalCopy;      //1bit ����ʱ����Ϊ0������ʱ����
	uint8_t home;               //1 bit ����ʱ����Ϊ0������ʱ����

	uint8_t copyrightIdentificationBit;   //1 bit ����ʱ����Ϊ0������ʱ����
	uint8_t copyrightIdentificationStart; //1 bit ����ʱ����Ϊ0������ʱ����
	unsigned int aacFrameLength;               //13 bit һ��ADTS֡�ĳ��Ȱ���ADTSͷ��AACԭʼ��
	unsigned int adtsBufferFullness;           //11 bit �����������ȣ�0x7FF˵�������ʿɱ������������Ҫ���ֶΡ�CBR������Ҫ���ֶΣ���ͬ������ʹ�������ͬ�������ʹ����Ƶ�����ʱ����Ҫע�⡣

	/* number_of_raw_data_blocks_in_frame
	 * ��ʾADTS֡����number_of_raw_data_blocks_in_frame + 1��AACԭʼ֡
	 * ����˵number_of_raw_data_blocks_in_frame == 0
	 * ��ʾ˵ADTS֡����һ��AAC���ݿ鲢����˵û�С�(һ��AACԭʼ֡����һ��ʱ����1024���������������)
	 */
	uint8_t numberOfRawDataBlockInFrame; //2 bit
};

static int parseAdtsHeader(uint8_t* in, struct AdtsHeader* res) {
	static int frame_number = 0;
	memset(res, 0, sizeof(*res));

	if ((in[0] == 0xFF) && ((in[1] & 0xF0) == 0xF0))
	{
		res->id = ((uint8_t)in[1] & 0x08) >> 3;//�ڶ����ֽ���0x08������֮�󣬻�õ�13λbit��Ӧ��ֵ
		res->layer = ((uint8_t)in[1] & 0x06) >> 1;//�ڶ����ֽ���0x06������֮������1λ����õ�14,15λ����bit��Ӧ��ֵ
		res->protectionAbsent = (uint8_t)in[1] & 0x01;
		res->profile = ((uint8_t)in[2] & 0xc0) >> 6;
		res->samplingFreqIndex = ((uint8_t)in[2] & 0x3c) >> 2;
		res->privateBit = ((uint8_t)in[2] & 0x02) >> 1;
		res->channelCfg = ((((uint8_t)in[2] & 0x01) << 2) | (((unsigned int)in[3] & 0xc0) >> 6));
		res->originalCopy = ((uint8_t)in[3] & 0x20) >> 5;
		res->home = ((uint8_t)in[3] & 0x10) >> 4;
		res->copyrightIdentificationBit = ((uint8_t)in[3] & 0x08) >> 3;
		res->copyrightIdentificationStart = (uint8_t)in[3] & 0x04 >> 2;

		res->aacFrameLength = (((((unsigned int)in[3]) & 0x03) << 11) |
			(((unsigned int)in[4] & 0xFF) << 3) |
			((unsigned int)in[5] & 0xE0) >> 5);

		res->adtsBufferFullness = (((unsigned int)in[5] & 0x1f) << 6 |
			((unsigned int)in[6] & 0xfc) >> 2);
		res->numberOfRawDataBlockInFrame = ((uint8_t)in[6] & 0x03);

		return 0;
	}
	else
	{
		printf("failed to parse adts header\n");
		return -1;
	}
}

static int rtpSendAACFrame(int socket, const char* ip, int16_t port,
	struct RtpPacket* rtpPacket, uint8_t* frame, uint32_t frameSize) {
	//����ĵ���https://blog.csdn.net/yangguoyu8023/article/details/106517251/
	int ret;

	rtpPacket->payload[0] = 0x00;
	rtpPacket->payload[1] = 0x10;
	rtpPacket->payload[2] = (frameSize & 0x1FE0) >> 5; //��8λ
	rtpPacket->payload[3] = (frameSize & 0x1F) << 3; //��5λ

	memcpy(rtpPacket->payload + 4, frame, frameSize);

	ret = rtpSendPacketOverUdp(socket, ip, port, rtpPacket, frameSize + 4);
	if (ret < 0)
	{
		printf("failed to send rtp packet\n");
		return -1;
	}

	rtpPacket->rtpHeader.seq++;

	/*
	 * �������Ƶ����44100
	 * һ��AACÿ��1024������Ϊһ֡
	 * ����һ����� 44100 / 1024 = 43֡
	 * ʱ���������� 44100 / 43 = 1025
	 * һ֡��ʱ��Ϊ 1 / 43 = 23ms
	 */
	rtpPacket->rtpHeader.timestamp += 1025;

	return 0;
}

RtspSession::RtspSession() : m_playingStatus(PLAY_NONE), m_isAauthorized(false), m_setupReqSeq(-1)
{

}

RtspSession::~RtspSession()
{

}

std::string RtspSession::doConversation(std::string data, std::string clntIp, RTSP_OPTIONS& rtspOption)
{
	m_clientIp = clntIp;

	char sendBuffer[SEND_BUFFER_SIZE];
	char method[40];
	char url[40];
	char version[40];
	int cseq;
	int clientRtpPort, clientRtcpPort;

	const char *sep = "\n";
	char* firstLine = strtok((char*)data.c_str(), sep);
	while (firstLine) {
		if (strstr(firstLine, "OPTIONS") ||
			strstr(firstLine, "DESCRIBE") ||
			strstr(firstLine, "SETUP") ||
			strstr(firstLine, "PLAY") ||
			strstr(firstLine, "PAUSE") ||
			strstr(firstLine, "TEARDOWN")) {
			if (strstr(firstLine, "SETUP")) {
				m_setupReqSeq++;
			}
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
			if (m_setupReqSeq == 0) {
				//vlc
				if (sscanf(firstLine, "Transport: RTP/AVP;unicast;client_port=%d-%d\r\n",
					&m_clientRtpUdpPortForVideo, &m_clientRtcpDupPortForVideo) != 2) {
					//ffplay
					if (sscanf(firstLine, "Transport: RTP/AVP/UDP;unicast;client_port=%d-%d\r\n",
						&m_clientRtpUdpPortForVideo, &m_clientRtcpDupPortForVideo) != 2) {
						printf("parse Transport error \n");
					}
				}
			}
			else {
				//vlc
				if (sscanf(firstLine, "Transport: RTP/AVP;unicast;client_port=%d-%d\r\n",
					&m_clientRtpUdpPortForAudio, &m_clientRtcpDupPortForAudio) != 2) {
					//ffplay
					if (sscanf(firstLine, "Transport: RTP/AVP/UDP;unicast;client_port=%d-%d\r\n",
						&m_clientRtpUdpPortForAudio, &m_clientRtcpDupPortForAudio) != 2) {
						printf("parse Transport error \n");
					}
				}
			}
		}
		//������֤��ֻ��ͨ��base64���ܣ��Ժ����ժҪ��֤md5У��
		else if (!strncmp(firstLine, "Authorization:", strlen("Authorization"))) {
			if (strstr(firstLine, "Basic") != nullptr) {
				char* encodedStr = firstLine + strlen("Authorization: Basic ");
				char *decodedStr = decode(encodedStr);
				if (!strcmp(decodedStr, "admin:1234qwer")) {
					m_isAauthorized = true;
				}
			}
		}

		firstLine = strtok(nullptr, sep);
	}

	if (!strcmp(method, "OPTIONS")) {
		if (handleOptionReq(sendBuffer, cseq)) {
			printf("handle OPTIONS error\n");
		}
		rtspOption = RESP_OPTION;
	}
	else if (!strcmp(method, "DESCRIBE")) {
		if (handleDescribeReq(sendBuffer, cseq, url)) {
			printf("handle DESCRIBE ERROR\n");
		}
		rtspOption = RTSP_DESCRIBE;
	}
	else if (!strcmp(method, "SETUP")) {
		if (handleSetupReq(sendBuffer, cseq, m_clientRtpUdpPortForVideo)) {
			printf("handle DESCRIBE ERROR\n");
		}

		rtspOption = RTSP_SETUP;

		if (m_setupReqSeq == 0) {
			m_rtpUdpSockForVideo = createUdpSocket();
			m_rtcpUdpSockForVideo = createUdpSocket();

			if (m_rtpUdpSockForVideo < 0 || m_rtcpUdpSockForVideo < 0)
			{
				printf("failed to create udp socket\n");
				return "";
			}

			if (bindSocketAddr(m_rtpUdpSockForVideo, "0.0.0.0", SERVER_RTP_PORT) < 0 ||
				bindSocketAddr(m_rtcpUdpSockForVideo, "0.0.0.0", SERVER_RTP_PORT + 1) < 0)
			{
				printf("failed to bind addr\n");
				return "";
			}
		}
		else {
			m_rtpUdpSockForAudio = createUdpSocket();
			m_rtcpUdpSockForAudio = createUdpSocket();

			if (m_rtpUdpSockForAudio < 0 || m_rtcpUdpSockForAudio < 0)
			{
				printf("failed to create udp socket\n");
				return "";
			}

			if (bindSocketAddr(m_rtpUdpSockForAudio, "0.0.0.0", SERVER_RTP_PORT + 2) < 0 ||
				bindSocketAddr(m_rtcpUdpSockForAudio, "0.0.0.0", SERVER_RTP_PORT + 1 + 2) < 0)
			{
				printf("failed to bind addr\n");
				return "";
			}
		}
		
	}
	else if (!strcmp(method, "PLAY")) {
		if (handlePlayReq(sendBuffer, cseq)) {
			printf("handle PLAY ERROR\n");
		}
		else {
			//��������rtp������
			std::thread t = std::thread(&RtspSession::sendRtpFrame, this, this);
			t.detach();
		}

		rtspOption = RTSP_PLAY;
	}
	else if (!strcmp(method, "PAUSE")) {
		if (handlePauseReq(sendBuffer, cseq)) {
			printf("handle PAUSE ERROR\n");
		}
		rtspOption = RTSP_PAUSE;
	}
	else if (!strcmp(method, "TEARDOWN")) {
		if (handleTeardownReq(sendBuffer, cseq)) {
			printf("handle TEARDOWN ERROR\n");
		}
		rtspOption = RTSP_TEARDOWN;
	}

	return std::string(sendBuffer);
}

int RtspSession::handleOptionReq(char* result, int cseq)
{
	sprintf(result, "RTSP/1.0 200 OK\r\n"
		"CSeq: %d\r\n"
		"Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN\r\n"
		"\r\n",
		cseq);

	return 0;
}

int RtspSession::handleDescribeReq(char* result, int cseq, char *url)
{
	if (m_isAauthorized) {
		char sdp[500];
		char localIp[100];

		sscanf(url, "rtsp://%[^:]:", localIp);

		sprintf(sdp, "v=0\r\n"
			"o=- 9%ld 1 IN IP4 %s\r\n"
			"t=0 0\r\n"
			"a=control:*\r\n"
			"m=video 0 RTP/AVP 96\r\n"
			"a=rtpmap:96 H264/90000\r\n"
			"a=control:track0\r\n"
			"m=audio 0 RTP/AVP 97\r\n"
			"a=rtpmap:97 mpeg4-generic/44100/2\r\n"
			"a=fmtp:97 profile-level-id=1;mode=AAC-hbr;sizelength=13;indexlength=3;indexdeltalength=3;config=1210;\r\n"
			"a=control:track1\r\n",

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
	}
	else {
		sprintf(result, "RTSP/1.0 401 Unauthorized\r\n"
			"CSeq: %d\r\n"
			"WWW-Authenticate: Basic realm=\"RTSPD\"\r\n\r\n",
			cseq);
	}

	return 0;
}

int RtspSession::handleSetupReq(char* result, int cseq, int clientRtpPort)
{
	int rtpPort = SERVER_RTP_PORT;
	if (cseq == 4) {
		rtpPort += 2;
	}

	sprintf(result, "RTSP/1.0 200 OK\r\n"
		"CSeq: %d\r\n"
		"Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=%d-%d\r\n"
		"Session: 66334873\r\n"
		"\r\n",
		cseq,
		clientRtpPort,
		clientRtpPort + 1,
		rtpPort,
		rtpPort + 1);

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

int RtspSession::handlePauseReq(char* result, int cseq)
{
	sprintf(result, "RTSP / 1.0 200 OK"
		"CSeq : %d"
		"Session : 66334873\r\n\r\n",
		cseq);

	return 0;
}

int RtspSession::handleTeardownReq(char* result, int cseq)
{
	sprintf(result, "RTSP/1.0 200 OK"
		"CSeq: %d\r\n\r\n",
		cseq);

	return 0;
}

int RtspSession::sendRtpFrame(void *obj)
{
	RtspSession* rtspSessionObj = (RtspSession*)obj;

	while (rtspSessionObj->m_playingStatus != PLAY_START) {
		Sleep(40);
	}

	std::thread videoThread([&]() {
		int frameSize, startCode;
		char* frame = (char*)malloc(500000);
		struct RtpPacket* rtpPacket = (struct RtpPacket*)malloc(500000);
		FILE* fp = fopen("test.h264", "rb");
		if (!fp) {
			printf("��ȡ %s ʧ��\n", "test.h264");
			return -1;
		}
		rtpHeaderInit(rtpPacket, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_H264, 0,
			0, 0, 0x88923423);

		printf("start play\n");
		printf("client ip:%s\n", m_clientIp.c_str());
		printf("client port:%d\n", m_clientRtpUdpPortForVideo);
	
		while (true) {
			if (rtspSessionObj->m_playingStatus == PLAY_STOP) {
				return 0;
			}

			if (rtspSessionObj->m_playingStatus != PLAY_START) {
				Sleep(40);
				continue;
			}

			frameSize = getFrameFromH264File(fp, frame, 500000);
			if (frameSize < 0)
			{
				printf("��ȡ%s����,frameSize=%d \n", "test.h264", frameSize);
				break;
			}

			if (startCode3(frame))
				startCode = 3;
			else
				startCode = 4;

			frameSize -= startCode;
			rtpSendH264Frame(m_rtpUdpSockForVideo, m_clientIp.c_str(), m_clientRtpUdpPortForVideo,
				rtpPacket, frame + startCode, frameSize);

			Sleep(20);
			//usleep(40000);//1000/25 * 1000
		}
		free(frame);
		free(rtpPacket);
	}
	);

	std::thread audioThread([&]() {
		struct AdtsHeader adtsHeader;
		struct RtpPacket* rtpPacket;
		uint8_t* frame;
		int ret;

		FILE* fp = fopen("test.aac", "rb");
		if (!fp) {
			printf("��ȡ %s ʧ��\n", "test.aac");
		}

		frame = (uint8_t*)malloc(5000);
		rtpPacket = (struct RtpPacket*)malloc(5000);

		rtpHeaderInit(rtpPacket, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_AAC, 1, 0, 0, 0x32411);

		while (true)
		{
			ret = fread(frame, 1, 7, fp);
			if (ret <= 0)
			{
				printf("fread err\n");
				break;
			}
			printf("fread ret=%d \n", ret);

			if (parseAdtsHeader(frame, &adtsHeader) < 0)
			{
				printf("parseAdtsHeader err\n");
				break;
			}
			ret = fread(frame, 1, adtsHeader.aacFrameLength - 7, fp);
			if (ret <= 0)
			{
				printf("fread err\n");
				break;
			}

			rtpSendAACFrame(m_rtcpUdpSockForVideo, m_clientIp.c_str(), m_clientRtpUdpPortForAudio,
				rtpPacket, frame, adtsHeader.aacFrameLength - 7);

			Sleep(23);
			//usleep(23223);//1000/43.06 * 1000
		}

		free(frame);
		free(rtpPacket);
	});
	videoThread.join();
	audioThread.join();

	return 0;
}