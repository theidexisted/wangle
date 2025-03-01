/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <wangle/acceptor/Acceptor.h>
#include <wangle/acceptor/AcceptorHandshakeManager.h>

namespace wangle {

// helper to return a string of the given transport's client IP and port
std::string describeAddresses(const folly::AsyncTransport* transport) {
  folly::SocketAddress peer;
  try {
    transport->getPeerAddress(&peer);
  } catch (...) {
    // ignore
  }
  folly::SocketAddress local;
  try {
    transport->getLocalAddress(&local);
  } catch (...) {
    // ignore
  }
  return folly::to<std::string>(
      "(peer=", peer.describe(), ", local=", local.describe(), ")");
}

void AcceptorHandshakeManager::start(
    folly::AsyncSSLSocket::UniquePtr sock) noexcept {
  acceptor_->getConnectionManager()->addConnection(this, true);
  startHelper(std::move(sock));
  startHandshakeTimeout();
}

void AcceptorHandshakeManager::connectionReady(
    folly::AsyncTransport::UniquePtr transport,
    std::string nextProtocol,
    SecureTransportType secureTransportType,
    folly::Optional<SSLErrorEnum> sslErr) noexcept {
  if (sslErr) {
    acceptor_->updateSSLStats(
        transport.get(),
        timeSinceAcceptMs(),
        sslErr.value(),
        folly::make_exception_wrapper<SSLException>(
            sslErr.value(),
            timeSinceAcceptMs(),
            transport->getRawBytesReceived()));
  }
  acceptor_->getConnectionManager()->removeConnection(this);
  // We pass TransportInfo by reference even though we're about to destroy it,
  // so lets hope that anything saving it makes a copy!
  acceptor_->sslConnectionReady(
      std::move(transport),
      std::move(clientAddr_),
      std::move(nextProtocol),
      secureTransportType,
      tinfo_);
  destroy();
}

void AcceptorHandshakeManager::connectionError(
    folly::AsyncTransport* transport,
    folly::exception_wrapper ex,
    folly::Optional<SSLErrorEnum> sslErr) noexcept {
  if (sslErr) {
    acceptor_->updateSSLStats(
        transport, timeSinceAcceptMs(), sslErr.value(), ex);
  }
  acceptor_->getConnectionManager()->removeConnection(this);
  acceptor_->sslConnectionError(std::move(ex));
  destroy();
}

std::chrono::milliseconds AcceptorHandshakeManager::timeSinceAcceptMs() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - acceptTime_);
}

void AcceptorHandshakeManager::startHandshakeTimeout() {
  auto handshake_timeout = acceptor_->getSSLHandshakeTimeout();
  acceptor_->getConnectionManager()->scheduleTimeout(this, handshake_timeout);
}

} // namespace wangle
