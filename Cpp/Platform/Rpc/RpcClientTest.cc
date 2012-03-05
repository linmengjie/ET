#include <boost/threadpool.hpp>
#include <gtest/gtest.h>
#include <glog/logging.h>
#include <gflags/gflags.h>
#include "Rpc/RpcClient.h"
#include "Thread/CountBarrier.h"
#include "Rpc/RpcController.h"
#include "Rpc/Echo.pb.h"

namespace Egametang {

class RpcServerTest: public RpcCommunicator
{
public:
	CountBarrier& barrier;
	int32 num;
	boost::asio::ip::tcp::acceptor acceptor;

public:
	RpcServerTest(boost::asio::io_service& ioService, int port, CountBarrier& barrier):
		RpcCommunicator(ioService), acceptor(ioService),
		barrier(barrier), num(0)
	{
		boost::asio::ip::address address;
		address.from_string("127.0.0.1");
		boost::asio::ip::tcp::endpoint endpoint(address, port);
		acceptor.open(endpoint.protocol());
		acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
		acceptor.bind(endpoint);
		acceptor.listen();
		acceptor.async_accept(socket,
				boost::bind(&RpcServerTest::OnAsyncAccept, this,
						boost::asio::placeholders::error));
	}

	void OnAsyncAccept(const boost::system::error_code& err)
	{
		if (err)
		{
			return;
		}
		RpcMetaPtr meta = boost::make_shared<RpcMeta>();
		StringPtr message = boost::make_shared<std::string>();
		RecvMeta(meta, message);
	}

	void Stop()
	{
		acceptor.close();
		socket.close();
	}

	virtual void OnRecvMessage(RpcMetaPtr meta, StringPtr message)
	{
		EchoRequest request;
		request.ParseFromString(*message);

		num = request.num();

		EchoResponse response;
		response.set_num(num);

		StringPtr responseMessage = boost::make_shared<std::string>();
		response.SerializeToString(responseMessage.get());
		RpcMetaPtr responseMeta = boost::make_shared<RpcMeta>();
		responseMeta->id = meta->id;
		responseMeta->size = responseMessage->size();
		SendMeta(responseMeta, responseMessage);
	}
	virtual void OnSendMessage(RpcMetaPtr meta, StringPtr message)
	{
		barrier.Signal();
	}
};

class RpcClientTest: public testing::Test
{
protected:
	int port;

public:
	RpcClientTest(): port(10002)
	{
	}
	virtual ~RpcClientTest()
	{
	}
};

static void IOServiceRun(boost::asio::io_service* ioService)
{
	ioService->run();
}

TEST_F(RpcClientTest, Echo)
{
	boost::asio::io_service ioServer;
	boost::asio::io_service ioClient;

	CountBarrier barrier(2);
	RpcServerTest server(ioServer, port, barrier);
	RpcClientPtr client = boost::make_shared<RpcClient>(ioClient, "127.0.0.1", port);
	EchoService_Stub service(client.get());

	boost::threadpool::fifo_pool threadPool(2);
	threadPool.schedule(boost::bind(&IOServiceRun, &ioServer));
	threadPool.schedule(boost::bind(&IOServiceRun, &ioClient));

	EchoRequest request;
	request.set_num(100);

	EchoResponse response;

	ASSERT_EQ(0, response.num());
	service.Echo(NULL, &request, &response,
			google::protobuf::NewCallback(&barrier, &CountBarrier::Signal));
	barrier.Wait();
	client->Stop();
	server.Stop();
	ioServer.stop();
	ioClient.stop();
	// 必须主动让client和server stop才能wait线程
	threadPool.wait();

	ASSERT_EQ(100, response.num());
}

} // namespace Egametang


int main(int argc, char* argv[])
{
	testing::InitGoogleTest(&argc, argv);
	google::InitGoogleLogging(argv[0]);
	google::ParseCommandLineFlags(&argc, &argv, true);
	return RUN_ALL_TESTS();
}