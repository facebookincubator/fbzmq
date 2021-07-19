# fbzmq

[![Build Status](https://travis-ci.org/facebook/fbzmq.svg?branch=master)](https://travis-ci.org/facebook/fbzmq)

`fbzmq` provides a framework for writing services in C++ while leveraging the
awesomeness of `libzmq` (message passing semantics). At a high level it provides
- Lightweight C++ wrapper over `libzmq` which leverages newer C++ constructs
  and stricter type checking. Most notably it provides the ability to
  send/receive `thrift objects` as messages over wire without worrying about
  wire encoding/decoding protocols.
- Powerful `Async Framework` with EventLoop, Timeouts, SignalHandler and more
  to enable developers to write asynchronous applications efficiently.
- Suite of monitoring tools that make it easy to add logging and counters to
  your service.

## Examples
Here is a simple example demonstrating some powerful abstractions of `fbzmq`
which makes writing asynchronous applications easier on top of `libzmq`

<!-- MARKDOWN-AUTO-DOCS:START (CODE:src=./examples/app.cpp) -->
<!-- The below code snippet is automatically added from ./examples/app.cpp -->
```cpp
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
```
<!-- MARKDOWN-AUTO-DOCS:END -->

## Requirements
We have tried `fbzmq` on `Ubuntu-14.04`, `Ubuntu-16.04` and `CentOS-7`. This
should work on all Linux based platforms without any issues.

* Compiler supporting C++14 or higher
* libzmq-4.0.6 or greater

## Building fbzmq

### Repo Directory Structure

At the top level of this repo are the `build` and `fbzmq` directories. Under the
former is a tool, `fbcode_builder`, that contains scripts for generating a
docker context to build the project. The `fbzmq` directory contains the
source for the project.

### Dependencies
* `cmake`
* `gflags`
* `gtest`
* `libsodium`
* `libzmq`
* `zstd`
* `folly`
* `fbthrift`

### One Step Build - Ubuntu-16.04

We've provided a script, `build/build_fbzmq.sh`, well tested on
Ubuntu-16.04, to install all necessary dependencies, compile fbzmq and install
C++ binaries as well as python tools. Please modify the script as needed for
your platform. Also, note that some library dependencies require a newer version
than provided by the default package manager on the system and hence we are
compiling them from source instead of installing via the package manager. Please
see the script for those instances and the required versions.

### Build using Docker

Learn more [here.](https://github.com/facebook/fbzmq/blob/master/build/fbcode_builder/README.md)


### Build Steps
```shell
// Step into `build` directory
cd build

//  Install dependencies and fbzmq
sudo bash ./build_fbzmq.sh

make test
```

## Installing fbzmq
`fbzmq` builds a static library and install steps installs library as well all
header files to `/usr/local/lib/` and `/usr/local/include/` (under fbzmq sub
directory)

```shell
sudo make install
```

## Build and Install python libraries

```shell
cd fbzmq/fbzmq/py
python setup.py build
sudo python setup.py install
```

## How fbzmq works
`zmq/*` are a straight forward C++ wrappers over raw libzmq objects (like
zmq_msg_t, zmq_ctx_t, etc)  in C. ZmqEventLoop is an event loop which allows
you to attach different event callbacks on sockets as well as schedule timeouts.
Internally it maintains a dynamic poll-list which is updated only when new
`socket/fd` is added/removed and uses `zmq_poll` APIs for polling the list. It
also maintains internal heap of timeouts ordered on their expiry times.

## Full documentation
To understand how to use library, take a look at `examples/` directory. All of
our code is very well documented and refer to appropriate header files for up to
date and detailed documentation of various APIs/functionalities.

## License
fbzmq is MIT-licensed.
