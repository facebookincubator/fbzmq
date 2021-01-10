/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>

#include <boost/serialization/strong_typedef.hpp>

#include <folly/Expected.h>
#if FOLLY_HAS_COROUTINES
#include <folly/experimental/coro/Baton.h>
#include <folly/experimental/coro/Task.h>
#endif
#include <fbzmq/zmq/Common.h>
#include <fbzmq/zmq/Context.h>
#include <fbzmq/zmq/Message.h>
#include <folly/fibers/Baton.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventHandler.h>

namespace fbzmq {

// Forward declaration of SocketMonitor
class SocketMonitor;

/**
 * Strong type definitions for various socket related attributes
 */
BOOST_STRONG_TYPEDEF(std::string, SocketUrl)
BOOST_STRONG_TYPEDEF(std::string, IdentityString)
BOOST_STRONG_TYPEDEF(std::string, PublicKey)
BOOST_STRONG_TYPEDEF(bool, NonblockingFlag)

/**
 * Used to specify socket mode as part of type signature
 */
enum SocketMode { ZMQ_CLIENT, ZMQ_SERVER, UNKNOWN };

/**
 * Forward declaration of socket. Defined via `SocketImpl`.
 * e.g of SocketType are ZMQ_PUB, ZMQ_ROUTER etc.
 */
template <int SocketType, int SocketMode = UNKNOWN>
class Socket;

namespace detail {

/**
 * This is not expected to be used directly. Rather, the descdendant template
 * Socket<> should be used instead
 * Refer to `examples/` or `ZmqSocketTest` for examples on how to use Socket
 *
 * If EventBase is set, then read/writes will be performed in a synchronous
 * fashion from callers point of view. However in the background wait for
 * socket readable/writable state will happen asynchronously on EventBase.
 * This will be particularly useful in context of fibers or co-routines.
 * NOTE: Socket must be set to `non-blocking` mode for using this feature.
 */
class SocketImpl : public folly::EventHandler {
 public:
  // Socket is created either in blocking or non-blocking modes.
  // KeyPair could be null, which means no crypto is enabled for
  // this socket.
  SocketImpl(
      // generated by descendants
      int type,
      bool isServer,
      // provided by user
      Context& ctx,
      folly::Optional<IdentityString> identity = folly::none,
      folly::Optional<KeyPair> keyPair = folly::none,
      NonblockingFlag isNonblocking = NonblockingFlag{false},
      folly::EventBase* evb = nullptr);

  SocketImpl(
      // generated by descendants
      int type,
      bool isServer,
      // the pointer to context
      void* ctxPtr,
      folly::Optional<IdentityString> identity = folly::none,
      folly::Optional<KeyPair> keyPair = folly::none,
      NonblockingFlag isNonblocking = NonblockingFlag{false},
      folly::EventBase* evb = nullptr);

  /**
   * context-less socket, initialized with nullptr. This socket could be
   * copy-initialized later on, otherwise it will remain empty and useless
   */
  SocketImpl(int type, bool isServer);

  ~SocketImpl() noexcept;

  /**
   * non-copyable
   */
  SocketImpl(SocketImpl const&) = delete;
  SocketImpl& operator=(SocketImpl const&) = delete;

  /**
   * movable
   */
  SocketImpl(SocketImpl&&) noexcept;
  SocketImpl& operator=(SocketImpl&&) noexcept;

  /**
   * Socket option APIs
   */
  folly::Expected<folly::Unit, Error> setSockOpt(
      int option, const void* optval, size_t len) noexcept;
  folly::Expected<folly::Unit, Error> getSockOpt(
      int option, void* optval, size_t* len) noexcept;

  /**
   * Convenient API to set TCP keep alive settings on a socket. We often need
   * to set multiple socket options to turn keep alive settings.
   */
  folly::Expected<folly::Unit, Error> setKeepAlive(
      int keepAlive /* SO_KEEPALIVE */,
      int keepAliveIdle = -1 /* TCP_KEEPCNT(or TCP_KEEPALIVE on some OS) */,
      int keepAliveCnt = -1 /* TCP_KEEPCNT */,
      int keepAliveIntvl = -1 /* TCP_KEEPINTVL */) noexcept;

  /**
   * Close socket
   */
  void close() noexcept;

#if FOLLY_HAS_COROUTINES
  /**
   * Receive message in a co-routine. Receive message immediately if one
   * available. If not suspends current coroutine using the EventLoop as
   * the scheduler. The coroutine will sleep until underlying ZMQ socket
   * has a message, read message and return.
   */
  folly::coro::Task<folly::Expected<Message, Error>> recvOneCoro();

  /**
   * Send message in a co-routine. Suspends current coroutine if underlying
   * socket is not writable
   */
  folly::coro::Task<folly::Expected<size_t, Error>> sendOneCoro(Message msg);
#endif

  /**
   * Send/receive methods
   */

  /**
   * Receive single message, atomically. Will or will not block depending on
   * socket operations mode. default timeout is indefinite
   */
  folly::Expected<Message, Error> recvOne(
      folly::Optional<std::chrono::milliseconds> timeout =
          folly::none) noexcept;

  /**
   * Static version for receiving multipart message. Will receive as many as
   * specified in the argument list, e.g.
   *  socket.recvMultiple(msg1, msg2, msg3);
   *
   * Wait indefinitely if timeout is not provided (set to folly::none)
   *
   * Returns error on unexpected condition (e.g. if there is more messages
   * passed in arguments that available on wire). At the same time, all
   * messages prior to the error will be received.
   */
  template <typename Msg>
  folly::Expected<folly::Unit, Error>
  recvMultipleTimeout(
      folly::Optional<std::chrono::milliseconds> timeout, Msg& msg) {
    auto tmp = recvOne(timeout);
    if (not tmp.hasValue()) {
      return folly::makeUnexpected(Error(tmp.error()));
    }
    msg = std::move(tmp.value());
    if (not hasMore()) {
      return folly::Unit();
    }
    // more message when we dont expect 'em
    return folly::makeUnexpected(Error(EPROTO));
  }

  template <typename First, typename... Rest>
  folly::Expected<folly::Unit, Error>
  recvMultiple(First& msg, Rest&... msgs) {
    return recvMultipleTimeout(folly::none, msg, msgs...);
  }

  template <typename First, typename... Rest>
  folly::Expected<folly::Unit, Error>
  recvMultipleTimeout(
      folly::Optional<std::chrono::milliseconds> timeout,
      First& msg,
      Rest&... msgs) {
    auto tmp = recvOne(timeout);
    if (not tmp.hasValue()) {
      return folly::makeUnexpected(Error(tmp.error()));
    }
    msg = std::move(tmp.value());
    if (not hasMore()) {
      return folly::makeUnexpected(Error(EPROTO));
    }
    // if the first message arrives, the rest shall have arrived, so no timeout
    return recvMultipleTimeout(
        folly::Optional<std::chrono::milliseconds>{}, msgs...);
  }

  /**
   * Receive multipart message as a whole. If the first message receive fails,
   * this will return an error all subsequent messages are not checked for
   * errors. Unlike static version, this will receive all messages available on
   * the wire (stops when hasMore() == false) default timeout is indefinite.
   */
  folly::Expected<std::vector<Message>, Error> recvMultiple(
      folly::Optional<std::chrono::milliseconds> timeout = folly::none);

  /**
   * Receive all pending messages on socket (till socket return EAGAIN)
   * This will return an error if one of the message recv returns unexpected
   * error.
   */
  folly::Expected<std::vector<Message>, Error> drain(
      folly::Optional<std::chrono::milliseconds> timeout = folly::none);

  /**
   * Send has two modes: first one ships standalone message the second one sets
   * the "more" flag, allowing for atomic message chaining
   */

  folly::Expected<size_t, Error> sendOne(Message msg) noexcept;

  folly::Expected<size_t, Error> sendMore(Message msg) noexcept;

  /**
   * Static version of sendMultiple. Ships messages passed as variadic parameter
   * pack. Stuff is passed by value, so you may want to use std::move()
   */

  template <bool hasMore = false, typename Msg>
  folly::Expected<size_t, Error>
  sendMultiple(Msg msg) {
    if (hasMore) {
      return sendMore(msg);
    } else {
      return sendOne(msg);
    }
  }

  template <bool hasMore = false, typename First, typename... Rest>
  folly::Expected<size_t, Error>
  sendMultiple(First msg, Rest... msgs) {
    auto res = sendMore(msg);
    if (not res.hasValue()) {
      return folly::makeUnexpected(res.error());
    }
    auto other = sendMultiple<hasMore, Rest...>(msgs...);
    if (not other.hasValue()) {
      return folly::makeUnexpected(other.error());
    }
    return res.value() + other.value();
  }

  template <typename... Msgs>
  folly::Expected<size_t, Error>
  sendMultipleMore(Msgs... msgs) {
    return sendMultiple<true, Msgs...>(msgs...);
  }

  /**
   * Dynamic version of sendMultiple. Sends messages one after another - returns
   * first error that occured.
   */
  folly::Expected<size_t, Error> sendMultiple(
      std::vector<Message> const& msgs, bool hasMore = false);

  /**
   * Convenience methods to recv/send thrift objects as messages
   */

  template <typename ThriftType, typename Serializer>
  folly::Expected<ThriftType, Error>
  recvThriftObj(
      Serializer& serializer,
      folly::Optional<std::chrono::milliseconds> timeout =
          folly::none) noexcept {
    auto maybeMessage = recvOne(timeout);

    return maybeMessage.hasError()
        ? folly::makeUnexpected(maybeMessage.error())
        : maybeMessage.value().readThriftObj<ThriftType>(serializer);
  }

  template <typename ThriftType, typename Serializer>
  folly::Expected<size_t, Error>
  sendThriftObj(const ThriftType& obj, Serializer& serializer) noexcept {
    auto msg = Message::fromThriftObj(obj, serializer);
    return msg.hasError() ? folly::makeUnexpected(msg.error())
                          : sendOne(msg.value());
  }

  /**
   * Return true if there are more parts of a message pending on the socket
   */
  bool hasMore() noexcept;

  /**
   * Return true if socket is blocking or not
   */
  bool
  isNonBlocking() const {
    return baseFlags_ & ZMQ_DONTWAIT;
  }

  /**
   * Return assocaited key pair if any
   */
  folly::Optional<KeyPair>
  getKeyPair() const {
    return keyPair_;
  }

  /**
   * "safer" pointer to raw object, mainly for poll. you will need
   * to explicitly cast it to void* if you need to hehehehe
   */
  uintptr_t
  operator*() {
    return reinterpret_cast<uintptr_t>(ptr_);
  }

 protected:
  /**
   * The below methods will be made open in sub-classes
   */

  folly::Expected<folly::Unit, Error> bind(SocketUrl) noexcept;

  folly::Expected<folly::Unit, Error> unbind(SocketUrl) noexcept;

  folly::Expected<folly::Unit, Error> connect(SocketUrl) noexcept;

  folly::Expected<folly::Unit, Error> disconnect(SocketUrl) noexcept;

  folly::Expected<folly::Unit, Error> addServerKey(
      SocketUrl, PublicKey) noexcept;

  folly::Expected<folly::Unit, Error> delServerKey(SocketUrl) noexcept;

 private:
  friend class fbzmq::SocketMonitor;

#if FOLLY_HAS_COROUTINES
  folly::coro::Task<void> coroWaitImpl(bool isReadElseWrite) noexcept;
#endif
  bool fiberWaitImpl(
      bool isReadElseWrite,
      folly::Optional<std::chrono::milliseconds> timeout) noexcept;

  /**
   * Utility function to initialize handler
   */
  void initHandlerHelper() noexcept;

  /**
   * EventHandler callback. Unblocks read/write wait
   */
  void handlerReady(uint16_t events) noexcept override;

  /**
   * low-level send method
   */
  folly::Expected<size_t, Error> send(Message msg, int flags) noexcept;

  /**
   * low-level recv method
   */
  folly::Expected<Message, Error> recvAsync(
      folly::Optional<std::chrono::milliseconds> timeout) noexcept;
  folly::Expected<Message, Error> recv(int flags) noexcept;

  /**
   * Crypto stuff
   */

  /**
   * generate and apply certificate to the socket
   */
  folly::Expected<folly::Unit, Error> applyKeyPair(
      const KeyPair& keyPair) noexcept;

  /**
   * attach server key to the socket
   */
  void setCurveServerSocketKey(const std::string& publicKey) noexcept;

  // used to store ZMQ_DONTWAIT
  int baseFlags_{0};
  bool isSendingMore_{false};

  // pointer to socket object. alas, this can not be const
  // since we update it in move constructor
  void* ptr_{nullptr};

  // point to the raw context we run under. mainly needed to pass to
  // SocketMonitor object
  void* ctxPtr_{nullptr};

  // the crypto key pair.
  folly::Optional<KeyPair> keyPair_;

  // public keys for use with servers
  std::unordered_map<std::string /* server url */, std::string /* pub key */>
      serverKeys_;

  //
  // Asynchronous read/write primitives
  //

  // Event loop for epoll and async wait on socket events
  folly::EventBase* evb_{nullptr};

  // Current event of interest (EventHandler::READ, EventHandler::WRITE or both)
  uint16_t waitEvents_{0};

#if FOLLY_HAS_COROUTINES
  // Wait synchronization primitive for socket events on a co-routine
  folly::coro::Baton coroReadBaton_;
  folly::coro::Baton coroWriteBaton_;
#endif
  // Wait synchronization primitive for socket events on a folly::Fiber
  folly::fibers::Baton fiberReadBaton_;
  folly::fibers::Baton fiberWriteBaton_;
};

/**
 * Two subclasses: one exposes binding methods, the other exposes the connection
 * methods. Those are intended to be used with server and client sockets
 * respectively. Those should not be used directly.
 */

class ServerSocketImpl : public SocketImpl {
 public:
  using SocketImpl::SocketImpl;

  /**
   * Make bind/unbind methods public
   */

  folly::Expected<folly::Unit, Error>
  bind(SocketUrl url) noexcept {
    return SocketImpl::bind(std::move(url));
  }

  folly::Expected<folly::Unit, Error>
  unbind(SocketUrl url) noexcept {
    return SocketImpl::unbind(std::move(url));
  }
};

class ClientSocketImpl : public SocketImpl {
 public:
  using SocketImpl::SocketImpl;

  /**
   * Make connect/disconnect methods public
   */

  folly::Expected<folly::Unit, Error>
  connect(SocketUrl url) noexcept {
    return SocketImpl::connect(std::move(url));
  }

  folly::Expected<folly::Unit, Error>
  disconnect(SocketUrl url) noexcept {
    return SocketImpl::disconnect(std::move(url));
  }

  /**
   * Make adding server crypto key public
   */

  folly::Expected<folly::Unit, Error>
  addServerKey(SocketUrl url, PublicKey key) noexcept {
    return SocketImpl::addServerKey(std::move(url), std::move(key));
  }

  folly::Expected<folly::Unit, Error>
  delServerKey(SocketUrl url) noexcept {
    return SocketImpl::delServerKey(std::move(url));
  }
};

} // namespace detail

/**
 * Define specializations for SERVER/CLIENT sockets
 */

template <int SocketType>
class Socket<SocketType, ZMQ_SERVER> : public detail::ServerSocketImpl {
 public:
  template <typename... Args>
  explicit Socket(Args&&... args)
      : ServerSocketImpl(SocketType, true, std::forward<Args>(args)...) {}
};

template <int SocketType>
class Socket<SocketType, ZMQ_CLIENT> : public detail::ClientSocketImpl {
 public:
  template <typename... Args>
  explicit Socket(Args&&... args)
      : ClientSocketImpl(SocketType, false, std::forward<Args>(args)...) {}
};

} // namespace fbzmq
