#pragma once

#include <cinttypes>
#include <coroutine>
#include <exception>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <utility>

class Task {
public:
  struct promise_type {
    std::coroutine_handle<> cont_;

    Task get_return_object() {
      return Task(std::coroutine_handle<promise_type>::from_promise(*this));
    }
    std::suspend_always initial_suspend() { return {}; }
    void unhandled_exception() { std::terminate(); }
    void return_void() {}
    auto final_suspend() noexcept {
      struct Awaiter {
        bool await_ready() noexcept { return false; }
        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<promise_type> coro) noexcept {
          return coro.promise().cont_;
        }
        void await_resume() noexcept {}
      };
      return Awaiter{};
    }
  };

  auto operator co_await() {
    struct Awaiter {
      std::coroutine_handle<promise_type> coro_;

      bool await_ready() { return !coro_; }
      std::coroutine_handle<> await_suspend(std::coroutine_handle<> cont) {
        coro_.promise().cont_ = cont;
        return coro_;
      }
      void await_resume() {}
    };
    return Awaiter{coro_};
  }

  explicit Task(std::coroutine_handle<promise_type> coro) : coro_(coro) {}

  ~Task() {
    if (coro_) {
      coro_.destroy();
    }
  }

  Task(Task&& other) : coro_(std::exchange(other.coro_, {})) {}

private:
  std::coroutine_handle<promise_type> coro_;
};

struct DetachedTask {
  struct promise_type {
    void return_void() {}
    std::suspend_never initial_suspend() { return {}; }
    void unhandled_exception() { std::terminate(); }
    std::suspend_never final_suspend() noexcept { return {}; }
    DetachedTask get_return_object() { return {}; }
  };
};

class Reactor {
public:
  Reactor() {
    for (auto& x : fds_) {
      x.fd = -1;
    }
  }

  auto event(int fd, short events) {
    struct Awaitable {
      Reactor* reactor_;
      int fd_;
      short events_;

      bool await_ready() { return false; }
      void await_suspend(std::coroutine_handle<> coro) {
        reactor_->add_event(fd_, events_, coro);
      }
      void await_resume() {}
    };
    return Awaitable{this, fd, events};
  }

  void run() {
    while (nevents_ > 0) {
      poll(fds_, nfds, -1);

      for (nfds_t i = 0; i < nfds; i++) {
        if (fds_[i].revents != 0) {
          if (fds_[i].revents & POLLIN || fds_[i].revents & POLLOUT) {
            fds_[i].fd = -1;
            handles_[i].resume();
            nevents_--;
          }
        }
      }
    }
  }

private:
  void add_event(int fd, short events, std::coroutine_handle<> coro) {
    fds_[fd].fd = fd;
    fds_[fd].events = events;
    handles_[fd] = coro;
    nevents_++;
  }

  static constexpr nfds_t nfds = 128;
  pollfd fds_[nfds];
  std::coroutine_handle<> handles_[nfds];
  int nevents_ = 0;
};

template <typename Awaitable>
void co_spawn(Reactor& reactor, Awaitable&& awaitable) {
  [](Reactor& reactor, Awaitable awaitable) -> DetachedTask {
    int fd = eventfd(0, 0);
    struct FDClose {
      int fd_;
      ~FDClose() { close(fd_); }
    } fd_close(fd);

    // Signal event
    std::uint64_t n = 1;
    write(fd, &n, sizeof(std::uint64_t));

    co_await reactor.event(fd, POLLIN);
    co_await awaitable;
  }(reactor, std::forward<Awaitable>(awaitable));
}
