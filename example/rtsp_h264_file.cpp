// RTSP Server

#include "xop/RtspServer.h"
#include "net/Timer.h"
#include <thread>
#include <memory>
#include <iostream>
#include <string>
#include <codec_api.h>
#include <opencv2/opencv.hpp>

void SendFrameThread(xop::RtspServer* rtsp_server, xop::MediaSessionId session_id);

int main(int argc, char **argv)
{	
	std::string suffix = "live";
	std::string ip = "127.0.0.1";
	std::string port = "554";
	std::string rtsp_url = "rtsp://" + ip + ":" + port + "/" + suffix;
	
	std::shared_ptr<xop::EventLoop> event_loop(new xop::EventLoop());
	std::shared_ptr<xop::RtspServer> server = xop::RtspServer::Create(event_loop.get());

	if (!server->Start("0.0.0.0", atoi(port.c_str()))) {
		printf("RTSP Server listen on %s failed.\n", port.c_str());
		return 0;
	}

	server->SetAuthConfig("-_-", "admin", "12345");

	xop::MediaSession *session = xop::MediaSession::CreateNew("live"); 
	session->AddSource(xop::channel_0, xop::H264Source::CreateNew()); 
	//session->StartMulticast(); 
	session->AddNotifyConnectedCallback([] (xop::MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port){
		printf("RTSP client connect, ip=%s, port=%hu \n", peer_ip.c_str(), peer_port);
	});
   
	session->AddNotifyDisconnectedCallback([](xop::MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port) {
		printf("RTSP client disconnect, ip=%s, port=%hu \n", peer_ip.c_str(), peer_port);
	});

	xop::MediaSessionId session_id = server->AddSession(session);
         
	std::thread t1(SendFrameThread, server.get(), session_id);
	t1.detach(); 

	std::cout << "Play URL: " << rtsp_url << std::endl;

	while (1) 
	{
		xop::Timer::Sleep(100);
	}

	getchar();
	return 0;
}

void SendFrameThread(xop::RtspServer* rtsp_server, xop::MediaSessionId session_id)
{       
	int buf_size = 2000000;
	std::unique_ptr<uint8_t> frame_buf(new uint8_t[buf_size]);

	ISVCEncoder* encoder_ = nullptr;
	SFrameBSInfo info;
	SSourcePicture pic;
	SEncParamBase param;
	int rv;
	cv::Mat rawImage;
	cv::Mat imageYuv, imageYuvMini;
	cv::Mat imageYuvCh[3], imageYuvMiniCh[3];

	rv = WelsCreateSVCEncoder(&encoder_);
	assert(0 == rv);
	assert(encoder_ != nullptr);

	rawImage = cv::imread("test.jpg", cv::IMREAD_COLOR);

	int width = rawImage.cols;
	int height = rawImage.rows;

	memset(&param, 0, sizeof(SEncParamBase));
	param.iUsageType = CAMERA_VIDEO_REAL_TIME;
	param.fMaxFrameRate = 40;
	param.iPicWidth = width;
	param.iPicHeight = height;
	param.iTargetBitrate = 1000000;

	encoder_->Initialize(&param);

	memset(&info, 0, sizeof(SFrameBSInfo));
	memset(&pic, 0, sizeof(SSourcePicture));
	pic.iPicWidth = width;
	pic.iPicHeight = height;
	pic.iColorFormat = videoFormatI420;
	cv::cvtColor(rawImage, imageYuv, cv::COLOR_BGR2YUV);
	cv::split(imageYuv, imageYuvCh);
	cv::resize(imageYuv, imageYuvMini, cv::Size(width / 2, height / 2));
	cv::split(imageYuvMini, imageYuvMiniCh);
	pic.iStride[0] = imageYuvCh[0].step;
	pic.iStride[1] = imageYuvMiniCh[1].step;
	pic.iStride[2] = imageYuvMiniCh[2].step;
	pic.pData[0] = imageYuvCh[0].data;
	pic.pData[1] = imageYuvMiniCh[1].data;
	pic.pData[2] = imageYuvMiniCh[2].data;

	rv = encoder_->EncodeFrame(&pic, &info);
	assert(rv == cmResultSuccess);

	while(1) 
	{
		if (info.eFrameType != videoFrameTypeSkip /*&& cbk != nullptr*/)
		{
			//output bitstream
			for (int iLayer = 0; iLayer < info.iLayerNum; iLayer++)
			{
				SLayerBSInfo* pLayerBsInfo = &info.sLayerInfo[iLayer];

				int iLayerSize = 0;
				int iNalIdx = pLayerBsInfo->iNalCount - 1;
				do
				{
					iLayerSize += pLayerBsInfo->pNalLengthInByte[iNalIdx];
					--iNalIdx;
				} while (iNalIdx >= 0);

				unsigned char* outBuf = pLayerBsInfo->pBsBuf;

				xop::AVFrame videoFrame = { 0 };
				videoFrame.type = 0;
				videoFrame.size = iLayerSize;
				videoFrame.timestamp = xop::H264Source::GetTimestamp();
				videoFrame.buffer.reset(new uint8_t[videoFrame.size]);
				memcpy(videoFrame.buffer.get(), outBuf, videoFrame.size);
				rtsp_server->PushFrame(session_id, xop::channel_0, videoFrame);
			}
		}
		xop::Timer::Sleep(10); 
	};
}


