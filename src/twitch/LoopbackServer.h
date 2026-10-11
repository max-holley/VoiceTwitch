#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace vt {

// Tiny single-threaded HTTP/1.1 server bound to localhost only, used to
// receive the OAuth redirect from the user's browser.
class LoopbackServer {
 public:
  struct Request {
    std::string method;
    std::string path;
    std::map<std::string, std::string> query;
  };
  struct Reply {
    int status = 200;
    std::string contentType = "text/html; charset=utf-8";
    std::string body;
  };
  // Set `done` to stop serving after this reply is sent.
  using Handler = std::function<Reply(const Request&, bool& done)>;

  LoopbackServer() = default;
  ~LoopbackServer();
  LoopbackServer(const LoopbackServer&) = delete;
  LoopbackServer& operator=(const LoopbackServer&) = delete;

  // Binds 127.0.0.1:port (and [::1]:port when available).
  bool listen(int port, std::string& error);
  // Returns true if the handler signalled completion, false on cancel/timeout.
  bool serve(const Handler& handler, const std::atomic<bool>& cancel, std::chrono::seconds timeout);
  void close();

 private:
  std::vector<intptr_t> sockets_;
};

}  // namespace vt
