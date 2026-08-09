#pragma once

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace io {

// Dedicated Boost.Asio io_context running on its own thread.
class CIoService {
public:
  CIoService();
  ~CIoService();

  CIoService(const CIoService &) = delete;
  CIoService &operator=(const CIoService &) = delete;

  bool Start(LPCWSTR wszThreadName);
  void Stop();

  boost::asio::io_context &GetContext();
  bool IsRunning() const;

  template <typename THandler> void Post(THandler &&handler) {
    if (m_bRunning) {
      boost::asio::post(m_ioContext, std::forward<THandler>(handler));
    }
  }

private:
  void RunLoop();

  boost::asio::io_context m_ioContext;
  std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> m_pWork;
  std::thread m_thread;
  std::atomic<bool> m_bRunning;
  WCHAR m_wszThreadName[64];
};

} // namespace io
