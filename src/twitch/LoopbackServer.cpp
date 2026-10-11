#include "twitch/LoopbackServer.h"

#include <cstring>

#include "core/StringUtil.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
static void closeSock(socket_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
static void closeSock(socket_t s) { ::close(s); }
#endif

namespace vt {

namespace {

const char* statusText(int status) {
  switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    default: return "OK";
  }
}

void sendAll(socket_t s, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
    auto n = ::send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0);
    if (n <= 0) return;
    sent += static_cast<size_t>(n);
  }
}

// Reads until the end of the request headers (we never need a body).
bool readRequest(socket_t s, std::string& out) {
  char buf[2048];
  for (int i = 0; i < 32 && out.find("\r\n\r\n") == std::string::npos; ++i) {
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(s, &rf);
    timeval tv{2, 0};
    if (select(static_cast<int>(s) + 1, &rf, nullptr, nullptr, &tv) <= 0) return false;
    auto n = ::recv(s, buf, sizeof(buf), 0);
    if (n <= 0) return false;
    out.append(buf, static_cast<size_t>(n));
    if (out.size() > 64 * 1024) return false;
  }
  return out.find("\r\n\r\n") != std::string::npos;
}

}  // namespace

LoopbackServer::~LoopbackServer() { close(); }

bool LoopbackServer::listen(int port, std::string& error) {
  close();
#ifdef _WIN32
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
  // IPv4 loopback (required).
  socket_t s4 = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s4 == kInvalidSocket) {
    error = "socket() failed";
    return false;
  }
  sockaddr_in a4{};
  a4.sin_family = AF_INET;
  a4.sin_port = htons(static_cast<uint16_t>(port));
  a4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#ifndef _WIN32
  int yes = 1;
  setsockopt(s4, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
  if (::bind(s4, reinterpret_cast<sockaddr*>(&a4), sizeof(a4)) != 0 || ::listen(s4, 8) != 0) {
    closeSock(s4);
    error = "port " + std::to_string(port) + " is already in use";
    return false;
  }
  sockets_.push_back(static_cast<intptr_t>(s4));

  // IPv6 loopback (optional): browsers may resolve "localhost" to ::1 first.
  socket_t s6 = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
  if (s6 != kInvalidSocket) {
    int v6only = 1;
    setsockopt(s6, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6only), sizeof(v6only));
    sockaddr_in6 a6{};
    a6.sin6_family = AF_INET6;
    a6.sin6_port = htons(static_cast<uint16_t>(port));
    a6.sin6_addr = in6addr_loopback;
    if (::bind(s6, reinterpret_cast<sockaddr*>(&a6), sizeof(a6)) == 0 && ::listen(s6, 8) == 0) {
      sockets_.push_back(static_cast<intptr_t>(s6));
    } else {
      closeSock(s6);
    }
  }
  return true;
}

bool LoopbackServer::serve(const Handler& handler, const std::atomic<bool>& cancel, std::chrono::seconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!cancel && std::chrono::steady_clock::now() < deadline) {
    fd_set rf;
    FD_ZERO(&rf);
    socket_t maxFd = 0;
    for (auto raw : sockets_) {
      auto s = static_cast<socket_t>(raw);
      FD_SET(s, &rf);
      if (s > maxFd) maxFd = s;
    }
    timeval tv{0, 250000};
    int ready = select(static_cast<int>(maxFd) + 1, &rf, nullptr, nullptr, &tv);
    if (ready <= 0) continue;

    for (auto raw : sockets_) {
      auto ls = static_cast<socket_t>(raw);
      if (!FD_ISSET(ls, &rf)) continue;
      socket_t c = ::accept(ls, nullptr, nullptr);
      if (c == kInvalidSocket) continue;

      std::string raw_req;
      Reply reply;
      bool done = false;
      if (!readRequest(c, raw_req)) {
        closeSock(c);
        continue;
      }
      // Request line: METHOD SP TARGET SP VERSION
      auto lineEnd = raw_req.find("\r\n");
      auto parts = split(std::string_view(raw_req).substr(0, lineEnd), ' ');
      if (parts.size() < 2) {
        reply.status = 400;
        reply.body = "bad request";
      } else {
        Request req;
        req.method = parts[0];
        auto q = parts[1].find('?');
        req.path = parts[1].substr(0, q);
        if (q != std::string::npos) req.query = parseQuery(std::string_view(parts[1]).substr(q + 1));
        reply = handler(req, done);
      }
      std::string resp = "HTTP/1.1 " + std::to_string(reply.status) + " " + statusText(reply.status) +
                         "\r\nContent-Type: " + reply.contentType +
                         "\r\nContent-Length: " + std::to_string(reply.body.size()) +
                         "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" + reply.body;
      sendAll(c, resp);
      closeSock(c);
      if (done) return true;
    }
  }
  return false;
}

void LoopbackServer::close() {
  for (auto raw : sockets_) closeSock(static_cast<socket_t>(raw));
  sockets_.clear();
}

}  // namespace vt
