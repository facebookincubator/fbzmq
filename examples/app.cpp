// Create context
fbzmq::Context context{1};

// Create eventloop
fbzmq::ZmqEventLoop evl;

// Create thrift serializer
apache::thrift::CompactSerializer serializer;

// Create REP and PUB server sockets
fbzmq::Socket<ZMQ_REP, ZMQ_SERVER> reqSocket;
fbzmq::Socket<ZMQ_PUB, ZMQ_SERVER> pubSocket;
reqSocket.bind("tcp://[::1]:12345");
pubSocket.bind("tcp://[::1]:12346");

// Attach callback on reqSocket
evl.addSocket(RawZmqSocketPtr{*reqSocket}, ZMQ_POLLIN, [&](int evts) noexcept {
  // It must be POLLIN event
  CHECK(evts | ZMQ_POLLIN);

  // Read request
  auto request = reqSocket.recvThriftObj<thrift::Request>(serializer);

  // Send response
  thrift::Response response;
  response.request = request;
  response.success = true;
  reqSocket.sendThriftObj(response, serializer);
});

// Create periodic timeout to send publications and schedule it every 10s
auto timeout = fbzmq::ZmqTimeout::make(&evl, [&]() noexcept {
  thrift::User user;
  user.name = "TestPerson";
  user.email = "test@person.com";
  pubSocket.sendThriftObj(nodeData, serializer);
});
timeout->scheduleTimeout(std::chrono::seconds(10), true /* periodic */);

// Let the magic begin
evl.run()