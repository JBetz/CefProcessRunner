#pragma once

#include <rpc.h>
#include "SDL3_net/SDL_net.h"
#include "include/cef_base.h"
#include "process_handler.h"
#include "rpc.hpp"
#include "thread_safe_queue.hpp"

class BrowserHandler;

class BrowserProcessHandler : public ProcessHandler, public CefBrowserProcessHandler {
 public:
  BrowserProcessHandler();
  ~BrowserProcessHandler();

  // Accessors.
  NET_Server* GetSocketServer();
  CefRefPtr<CefBrowser> GetBrowser(int browserId);
  CefRefPtr<BrowserHandler> GetBrowserHandler(int browserId);
  void OpenClientProcessHandle(int processId);
  void SetClientMessageWindowHandle(HWND messageWindowHandle);
  std::optional<HANDLE> GetClientProcessHandle();
  std::optional<HWND> GetClientMessageWindowHandle();

  // CefBrowserProcessHandler methods.
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override;
  void OnContextInitialized() override;
  
  // Incoming RPC messages.
  void CreateBrowserRpc(const Client_CreateBrowser& request);
  
  // Outgoing RPC messages.
  void SendMessage(std::string payload);
  template<typename T> T WaitForResponse(UUID id);
  
  // RPC threads, need to be static.
  static int RpcServerThread(void* browserProcessHandlerPtr);
  static int RpcWorkerThread(void* browserProcessHandlerPtr);

 private:
  std::optional<HANDLE> clientProcessHandle;
  std::optional<HWND> clientMessageWindowHandle;
  ThreadSafeQueue<std::string> incomingMessageQueue;
  ThreadSafeQueue<std::string> outgoingMessageQueue;
  SDL_Mutex* responseMapMutex = nullptr;
  std::map<UUID, std::unique_ptr<ResponseEntry>> responseEntries;
  std::map<int, CefRefPtr<BrowserHandler>> browserHandlers;

  NET_Server* socketServer;

  IMPLEMENT_REFCOUNTING(BrowserProcessHandler);
  DISALLOW_COPY_AND_ASSIGN(BrowserProcessHandler);
};