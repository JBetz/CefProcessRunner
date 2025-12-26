#include <iostream>
#include <stdio.h>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <string>
#include <variant>
#include <windows.h>

#include <include/base/cef_callback.h>
#include <include/cef_task.h>
#include <include/cef_parser.h>
#include <include/base/cef_bind.h>
#include <include/wrapper/cef_closure_task.h>
#include <SDL3/sdl.h>
#include <SDL3_net/SDL_net.h>
#include <json.hpp>

#include "browser_handler.h"
#include "browser_process_handler.h"
#include "rpc.hpp"
#include "thread_safe_queue.hpp"
#include "guid_ext.hpp"

using json = nlohmann::json;

const char kEvalMessage[] = "Eval";

BrowserProcessHandler::BrowserProcessHandler()
    : clientProcessHandle(std::nullopt),
      incomingMessageQueue(),
      outgoingMessageQueue(),
      responseMapMutex(SDL_CreateMutex()),
      socketServer(NULL),
      browserHandlers(),
      isShuttingDown(false) {}

BrowserProcessHandler::~BrowserProcessHandler() {
  SDL_DestroyMutex(responseMapMutex);
  responseMapMutex = nullptr;
}

NET_Server* BrowserProcessHandler::GetSocketServer() {
  return socketServer;
}

CefRefPtr<CefBrowser> BrowserProcessHandler::GetBrowser(int browserId) {
  auto it = browserHandlers.find(browserId);
  if (it != browserHandlers.end()) {
    return it->second->GetBrowser();
  }
  return nullptr;
}

CefRefPtr<BrowserHandler> BrowserProcessHandler::GetBrowserHandler(int browserId) {
  auto it = browserHandlers.find(browserId);
  if (it != browserHandlers.end()) {
    return it->second;
  }
  return nullptr;
}

void BrowserProcessHandler::RemoveBrowserHandler(int browserId) {
  auto it = browserHandlers.find(browserId);
  if (it != browserHandlers.end()) {
    browserHandlers.erase(it);
  }

  // If we're in the middle of an explicit shutdown and there are no more
  // browser handlers, finish the shutdown sequence.
  if (isShuttingDown && browserHandlers.empty()) {
    CefPostTask(TID_UI, base::BindOnce([]() { CefQuitMessageLoop(); }));
  }
}

CefRefPtr<CefBrowserProcessHandler> BrowserProcessHandler::GetBrowserProcessHandler() {
  return this;
}

std::optional<HANDLE> BrowserProcessHandler::GetClientProcessHandle() {
  return this->clientProcessHandle;
}

std::optional<HWND> BrowserProcessHandler::GetClientMessageWindowHandle() {
  return this->clientMessageWindowHandle;
}

void BrowserProcessHandler::OpenClientProcessHandle(int processId) {
  HANDLE handle = OpenProcess(PROCESS_DUP_HANDLE, FALSE, processId);
  if (handle == NULL) {
    SDL_Log("OpenProcess(%u) failed: %lu", (unsigned)processId, GetLastError());
  } else {
    this->clientProcessHandle = std::make_optional(handle);
  }
}

void BrowserProcessHandler::SetClientMessageWindowHandle(HWND windowHandle) {
  this->clientMessageWindowHandle = std::make_optional(windowHandle);
}

void BrowserProcessHandler::OnContextInitialized() {
  this->CefBrowserProcessHandler::OnContextInitialized();

  if (!SDL_Init(SDL_INIT_EVENTS)) {
    SDL_Log(SDL_GetError());
    abort();
  }

  if (!NET_Init()) {
    SDL_Log(SDL_GetError());
    abort();
  }
  
  socketServer = NET_CreateServer(NULL, 3000);
  if (socketServer == NULL) {
    SDL_Log(SDL_GetError());
    abort();
  }

  SDL_Log("Server started on port 3000");

  SDL_Thread* thread = SDL_CreateThread(RpcServerThread, "CefRpcServer", this);
  if (thread == NULL) {
    SDL_Log("Failed creating RPC server thread: %s", SDL_GetError());
    abort();
  }

  SDL_Thread* workerThread = SDL_CreateThread(RpcWorkerThread, "CefRpcWorker", this);
  if (workerThread == NULL) {
    SDL_Log("Failed creating RPC worker thread: %s", SDL_GetError());
    abort();
  }
}

void BrowserProcessHandler::Client_CreateBrowserRpc(const UUID& requestId, const CefString& url, const CefRect& rectangle) {
  CefWindowInfo windowInfo;
  windowInfo.SetAsWindowless(nullptr);  // no OS parent
  windowInfo.windowless_rendering_enabled = true;
  windowInfo.shared_texture_enabled = true;
  windowInfo.bounds = rectangle;

  CefBrowserSettings browserSettings;
  browserSettings.windowless_frame_rate = 30;

  CefRefPtr<CefRequestContext> requestContext =
      CefRequestContext::CreateContext(CefRequestContextSettings(), nullptr);
  CefRefPtr<CefDictionaryValue> extraInfo = CefDictionaryValue::Create();

  CefRefPtr<BrowserHandler> handler = new BrowserHandler(this, rectangle);

  CefRefPtr<CefBrowser> browser = CefBrowserHost::CreateBrowserSync(
      windowInfo,
      handler,
      url,
      browserSettings,
      extraInfo, 
      requestContext);

  handler->SetBrowser(browser);

  int browserId = -1;
  if (browser) {
    browserId = browser->GetIdentifier();
    browserHandlers[browserId] = handler;
    SDL_Log("Created browser on UI thread; id=%d url=%s", browserId,
            url.c_str());
    Client_CreateBrowserResponse response;
    response.requestId = requestId;
    response.browserId = browserId;
    json j = response;
    outgoingMessageQueue.push(j.dump());
  } else {
    SDL_Log("CreateBrowserSync returned null");
  }
}

void BrowserProcessHandler::Client_ShutdownRpc() {
  isShuttingDown = true;
  if (browserHandlers.empty()) {
    SDL_Log("No browser handlers during shutdown.");
    CefQuitMessageLoop();
  } else {
    SDL_Log("%d browser handlers during shutdown.", browserHandlers.size());
    for (const auto& pair : browserHandlers) {
      CefRefPtr<CefBrowser> browser = pair.second->GetBrowser();
      browser->GetHost()->CloseBrowser(true);
    }
  }
}

void BrowserProcessHandler::Browser_CloseRpc(const CefRefPtr<CefBrowser> browser, bool forceClose) {
  browser->GetHost()->CloseBrowser(forceClose);
}

void BrowserProcessHandler::Browser_TryCloseRpc(const CefRefPtr<CefBrowser> browser, const UUID& requestId) {
  bool canClose = browser->GetHost()->TryCloseBrowser();
  Browser_TryCloseResponse response;
  response.requestId = requestId;
  response.canClose = canClose;
  json jsonResponse = response;
  outgoingMessageQueue.push(jsonResponse.dump());
}

void BrowserProcessHandler::SendMessage(std::string payload) {
  outgoingMessageQueue.push(payload);
}

template<typename T> T BrowserProcessHandler::WaitForResponse(UUID requestId) {
  std::unique_ptr<ResponseEntry> entry = std::make_unique<ResponseEntry>();

  // Insert into map under map mutex
  SDL_LockMutex(responseMapMutex);
  responseEntries[requestId] = std::move(entry);
  ResponseEntry* e = responseEntries[requestId].get();
  SDL_UnlockMutex(responseMapMutex);

  // Wait for signal
  SDL_LockMutex(e->mutex);
  while (!e->ready) {
    SDL_WaitCondition(e->cond, e->mutex);
  }
  SDL_UnlockMutex(e->mutex);

  std::string payload;
  SDL_LockMutex(responseMapMutex);
  auto it = responseEntries.find(requestId);
  if (it != responseEntries.end()) {
    ResponseEntry* entryPtr = it->second.get();
    SDL_LockMutex(entryPtr->mutex);
    payload = std::move(entryPtr->payload);
    SDL_UnlockMutex(entryPtr->mutex);
    responseEntries.erase(it);
  }
  SDL_UnlockMutex(responseMapMutex);

  try {
    json j = json::parse(payload);
    return j.get<RpcHeader>().arguments.get<T>();
  } catch (const nlohmann::json::parse_error& e_parse) {
    // Log parse error, location and a truncated payload preview (safe length)
    size_t previewLen = std::min<size_t>(payload.size(), 256);
    std::string preview = payload.substr(0, previewLen);
    SDL_Log("WaitForResponse: JSON parse_error: %s at byte=%u payload_preview='%s'",
            e_parse.what(), static_cast<unsigned int>(e_parse.byte), preview.c_str());
    throw; // rethrow so caller can handle the failure
  } catch (const std::exception& e) {
    SDL_Log("WaitForResponse: JSON exception: %s", e.what());
    throw;
  }
}

int BrowserProcessHandler::RpcServerThread(void* browserProcessHandlerPtr) {
  CefRefPtr<BrowserProcessHandler> browserProcessHandler =
      base::WrapRefCounted<BrowserProcessHandler>(static_cast<BrowserProcessHandler*>(browserProcessHandlerPtr));
  NET_Server* socketServer = browserProcessHandler->GetSocketServer();
  NET_StreamSocket* streamSocket = nullptr;

  SDL_Log("Network thread running");

  // Keep a persistent receive buffer so partial reads are preserved.
  std::vector<uint8_t> recvBuffer;
  uint8_t temp[1024];

  while (true) {
    if (!streamSocket) {
      if (!NET_AcceptClient(socketServer, &streamSocket)) {
        SDL_Log("Accept error: %s", SDL_GetError());
        SDL_Delay(1000);
        continue;
      }

      if (streamSocket) {
        SDL_Log("Client connected!");
        recvBuffer.clear();
      } else {
        SDL_Delay(100);
        continue;
      }
    }

    int received = NET_ReadFromStreamSocket(streamSocket, temp, sizeof(temp));
    if (received > 0) {
      recvBuffer.insert(recvBuffer.end(), temp, temp + received);
    } else if (received < 0) {
      SDL_Log("Read error: %s", SDL_GetError());
      streamSocket = nullptr;
      recvBuffer.clear();
      SDL_Delay(100);
      continue;
    }

    // Process as many full framed messages as available in recvBuffer.
    while (recvBuffer.size() >= 4) {
      uint32_t msgLen;
      memcpy(&msgLen, recvBuffer.data(), 4);

      if (recvBuffer.size() < 4 + msgLen)
        break;  // not enough bytes yet, wait for more reads

      std::string jsonMsg(recvBuffer.begin() + 4,
          recvBuffer.begin() + 4 + msgLen);
      browserProcessHandler->incomingMessageQueue.push(jsonMsg);  // Worker thread will parse JSON

      recvBuffer.erase(recvBuffer.begin(), recvBuffer.begin() + 4 + msgLen);
    }

    std::string outMsg;
    while (browserProcessHandler->outgoingMessageQueue.try_pop(outMsg)) {
        uint32_t len = static_cast<uint32_t>(outMsg.size());
        // Build single contiguous buffer: [len(4 bytes)] [payload]
        std::vector<uint8_t> sendBuf;
        sendBuf.resize(4 + outMsg.size());
        memcpy(sendBuf.data(), &len, 4);
        if (!outMsg.empty()) {
          memcpy(sendBuf.data() + 4, outMsg.data(), outMsg.size());
        }

        int total = static_cast<int>(sendBuf.size());
        if (!NET_WriteToStreamSocket(streamSocket, sendBuf.data(), total)) {
          SDL_Log("NET_WriteToStreamSocket failed or connection closed: %s", SDL_GetError());
          NET_DestroyStreamSocket(streamSocket);
          streamSocket = nullptr;
          break;
        }
        std::optional<HWND> clientMessageWindowHandle = browserProcessHandler
            ->GetClientMessageWindowHandle();
        if (clientMessageWindowHandle.has_value()) {
          NET_WaitUntilStreamSocketDrained(streamSocket, -1);
          PostMessageW(clientMessageWindowHandle.value(), WM_USER, 0, 0);
        }
    }
    SDL_Delay(1);  // tiny yield
  }
  return 0;
}

int BrowserProcessHandler::RpcWorkerThread(void* browserProcessHandlerPtr) {
  CefRefPtr<BrowserProcessHandler> browserProcessHandler = base::WrapRefCounted<BrowserProcessHandler>(static_cast<BrowserProcessHandler*>(browserProcessHandlerPtr));
  while (true) {
    std::string msg = browserProcessHandler->incomingMessageQueue.pop();

    json jsonRequest;
    try {
      jsonRequest = json::parse(msg);
    } catch (const nlohmann::json::parse_error& e_parse) {
      size_t previewLen = std::min<size_t>(msg.size(), 256);
      std::string preview = msg.substr(0, previewLen);
      SDL_Log("RpcWorkerThread: JSON parse_error: %s at byte=%u payload_preview='%s'",
              e_parse.what(), static_cast<unsigned int>(e_parse.byte), preview.c_str());
      continue;
    } catch (const std::exception& e) {
      SDL_Log("RpcWorkerThread: JSON exception: %s", e.what());
      continue;
    }

    RpcHeader rpc = jsonRequest.get<RpcHeader>();
    
    if (rpc.className == "Client") {
       if (rpc.methodName == "Initialize") {
        Client_Initialize request = rpc.arguments.get<Client_Initialize>();
        browserProcessHandler->OpenClientProcessHandle(request.clientProcessId);
        browserProcessHandler->SetClientMessageWindowHandle(
            reinterpret_cast<HWND>(request.clientMessageWindowHandle));
        continue;
      }

      if (rpc.methodName == "CreateBrowser") {
        Client_CreateBrowser request = rpc.arguments.get<Client_CreateBrowser>();
        CefPostTask(TID_UI,
                    base::BindOnce(&BrowserProcessHandler::Client_CreateBrowserRpc,
                                   browserProcessHandler, rpc.id, request.url, request.rectangle));
        continue;
      }

      if (rpc.methodName == "Shutdown") {
        CefPostTask(TID_UI,
                    base::BindOnce(&BrowserProcessHandler::Client_ShutdownRpc,
                                   browserProcessHandler));
        continue;
      }
    }

    if (rpc.className == "Browser") {
      CefRefPtr<BrowserHandler> browserHandler = 
          browserProcessHandler->GetBrowserHandler(rpc.instanceId);
      CefRefPtr<CefBrowser> browser = browserHandler->GetBrowser();
      if (!browserHandler || !browser) {
        SDL_Log("RpcWorkerThread: Browser with id=%d not found", rpc.instanceId);
        continue;
      }

      if (rpc.methodName == "EvalJavaScript") {
        CefRefPtr<CefFrame> frame = browser->GetMainFrame();
        CefRefPtr<CefProcessMessage> message =
            CefProcessMessage::Create(kEvalMessage);
        message->GetArgumentList()->SetString(0, msg);
        frame->SendProcessMessage(PID_RENDERER, message);
        continue;
      };

      if (rpc.methodName == "CanGoBack") {
        Browser_CanGoBackResponse response;
        response.requestId = rpc.id;
        response.canGoBack = browser->CanGoBack();
        json jsonResponse = response;
        browserProcessHandler->SendMessage(jsonResponse.dump());
        continue;
      }

      if (rpc.methodName == "CanGoForward") {
        Browser_CanGoForwardResponse response;
        response.requestId = rpc.id;
        response.canGoForward = browser->CanGoForward();
        json jsonResponse = response;
        browserProcessHandler->SendMessage(jsonResponse.dump());
        continue;
      }

      if (rpc.methodName == "Back") {
        browser->GoBack();
        continue;
      }

      if (rpc.methodName == "Forward") {
        browser->GoForward();
        continue;
      }

      if (rpc.methodName == "Reload") {
        browser->Reload();
        continue;
      }

      if (rpc.methodName == "Focus") {
        Browser_Focus request = rpc.arguments.get<Browser_Focus>();
        browser->GetHost()->SetFocus(request.focus);
        continue;
      }

      if (rpc.methodName == "WasHidden") {
        Browser_WasHidden request = rpc.arguments.get<Browser_WasHidden>();
        browser->GetHost()->WasHidden(request.hidden);
        continue;
      }

      if (rpc.methodName == "LoadUrl") {
        Browser_LoadUrl request = rpc.arguments.get<Browser_LoadUrl>();
        CefRefPtr<CefFrame> frame = browser->GetMainFrame();
        frame->LoadURL(request.url);
        continue;
      }

      if (rpc.methodName == "NotifyResize") {
        Browser_NotifyResize request = rpc.arguments.get<Browser_NotifyResize>();
        browserHandler->SetPageRectangle(request.rectangle);
        continue;
      }

      if (rpc.methodName == "Cut") {
        browser->GetFocusedFrame()->Cut();
        continue;
      }

      if (rpc.methodName == "Copy") {
        browser->GetFocusedFrame()->Copy();
        continue;
      }

      if (rpc.methodName == "Paste") {
        browser->GetFocusedFrame()->Paste();
        continue;
      }

      if (rpc.methodName == "Delete") {
        browser->GetFocusedFrame()->Delete();
        continue;
      }

      if (rpc.methodName == "Undo") {
        browser->GetFocusedFrame()->Undo();
        continue;
      }

      if (rpc.methodName == "Redo") {
        browser->GetFocusedFrame()->Redo();
        continue;
      }

      if (rpc.methodName == "SelectAll") {
        browser->GetFocusedFrame()->SelectAll();
        continue;
      }

      if (rpc.methodName == "OnMouseClick") {
        Browser_OnMouseClick request = rpc.arguments.get<Browser_OnMouseClick>();
        browser->GetHost()->SendMouseClickEvent(
              request.event,
              static_cast<CefBrowserHost::MouseButtonType>(request.button),
              request.mouseUp, request.clickCount);
        continue;
      }

      if (rpc.methodName == "OnMouseMove") {
        Browser_OnMouseMove request = rpc.arguments.get<Browser_OnMouseMove>();
        browser->GetHost()->SendMouseMoveEvent(request.event,
                                                 request.mouseLeave);
        continue;
      }

      if (rpc.methodName == "OnMouseWheel") {
        Browser_OnMouseWheel request = rpc.arguments.get<Browser_OnMouseWheel>();
        browser->GetHost()->SendMouseWheelEvent(request.event, request.deltaX,
                                                  request.deltaY);
        continue;
      }

      if (rpc.methodName == "OnKeyboardEvent") {
        Browser_OnKeyboardEvent request =
            rpc.arguments.get<Browser_OnKeyboardEvent>();
        browser->GetHost()->SendKeyEvent(request.event);
        continue;
      }

      if (rpc.methodName == "Close") {
        Browser_Close request = rpc.arguments.get<Browser_Close>();
        CefPostTask(TID_UI,
                    base::BindOnce(&BrowserProcessHandler::Browser_CloseRpc,
                                   browserProcessHandler, browser, request.forceClose));
        continue;
      }

      if (rpc.methodName == "TryClose") {
        CefPostTask(TID_UI, base::BindOnce(&BrowserProcessHandler::Browser_TryCloseRpc,
                                           browserProcessHandler, browser, rpc.id));
        continue;
      }

      if (rpc.methodName == "Acknowledge") {
        Browser_Acknowledge request = rpc.arguments.get<Browser_Acknowledge>();
        // Try to find a waiting entry
        SDL_LockMutex(browserProcessHandler->responseMapMutex);
        auto it =
            browserProcessHandler->responseEntries.find(request.requestId);
        if (it != browserProcessHandler->responseEntries.end()) {
          ResponseEntry* e = it->second.get();
          SDL_LockMutex(e->mutex);
          e->payload = msg;
          e->ready = true;
          SDL_SignalCondition(e->cond);
          SDL_UnlockMutex(e->mutex);
        }
        SDL_UnlockMutex(browserProcessHandler->responseMapMutex);
        continue;
      }
    }

    SDL_Log("RpcWorkerThread: unknown message method '%s'", rpc.methodName.c_str());
  }

  return 0;
}

template Browser_Acknowledge
    BrowserProcessHandler::WaitForResponse<Browser_Acknowledge>(UUID);