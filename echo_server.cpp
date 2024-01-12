#include "reactor.h"
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>

class Socket {
public:
  Socket(int fd) : fd_(fd) {}

  ~Socket() {
    if (fd_) {
      close(fd_);
    }
  }

  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  Socket(Socket&& other) : fd_(std::exchange(other.fd_, {})) {}
  Socket& operator=(Socket&& other) {
    fd_ = std::exchange(other.fd_, {});
    return *this;
  }

  operator int() { return fd_; }

private:
  int fd_;
};

Task echo(Reactor& reactor, Socket socket) {
  char data[1024];
  for (;;) {
    // Non-blocking read
    co_await reactor.event(socket, POLLIN);
    ssize_t bytes = read(socket, data, sizeof(data));
    if (bytes == 0) {
      co_return;
    }

    // Non-blocking write
    int written = 0;
    while (written < bytes) {
      ssize_t n = write(socket, data, bytes);
      if (n == 0) {
        co_return;
      }
      if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        co_await reactor.event(socket, POLLOUT);
      } else {
        written += n;
      }
    }
  }
}

Task listener(Reactor& reactor) {
  Socket server_socket = socket(AF_INET, SOCK_STREAM, 0);

  int on = 1;
  setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  // Mark server socket as non-blocking
  int flags = fcntl(server_socket, F_GETFL);
  fcntl(server_socket, F_SETFL, flags | O_NONBLOCK);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(55555);

  bind(server_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

  listen(server_socket, 50);

  for (;;) {
    co_await reactor.event(server_socket, POLLIN);
    Socket socket = accept(server_socket, nullptr, nullptr);

    // Mark client socket as non-blocking
    int flags = fcntl(socket, F_GETFL);
    fcntl(socket, F_SETFL, flags | O_NONBLOCK);

    co_spawn(reactor, echo(reactor, std::move(socket)));
  }
}

int main() {
  Reactor reactor;
  co_spawn(reactor, listener(reactor));
  reactor.run();
}
