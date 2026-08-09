#include "io/io_service.h"

namespace io {

CIoService::CIoService() : m_bRunning(false) {
  m_wszThreadName[0] = L'\0';
}

CIoService::~CIoService() {
  Stop();
}

bool CIoService::Start(LPCWSTR wszThreadName) {
  if (m_bRunning) {
    return true;
  }

  if (wszThreadName != nullptr) {
    wcsncpy_s(m_wszThreadName, wszThreadName, _TRUNCATE);
  } else {
    m_wszThreadName[0] = L'\0';
  }

  m_pWork = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(boost::asio::make_work_guard(m_ioContext));
  m_bRunning = true;
  m_thread = std::thread(&CIoService::RunLoop, this);
  return true;
}

void CIoService::Stop() {
  if (!m_bRunning) {
    return;
  }

  m_bRunning = false;
  m_pWork.reset();
  m_ioContext.stop();

  if (m_thread.joinable()) {
    m_thread.join();
  }

  m_ioContext.restart();
}

boost::asio::io_context &CIoService::GetContext() {
  return m_ioContext;
}

bool CIoService::IsRunning() const {
  return m_bRunning;
}

void CIoService::RunLoop() {
  if (m_wszThreadName[0] != L'\0') {
    typedef HRESULT(WINAPI * SetThreadDescriptionFn)(HANDLE, PCWSTR);
    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    if (hKernel != nullptr) {
      auto pfnSetThreadDescription = reinterpret_cast<SetThreadDescriptionFn>(GetProcAddress(hKernel, "SetThreadDescription"));
      if (pfnSetThreadDescription != nullptr) {
        pfnSetThreadDescription(GetCurrentThread(), m_wszThreadName);
      }
    }
  }

  m_ioContext.run();
}

} // namespace io
