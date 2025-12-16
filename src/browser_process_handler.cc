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
      browserHandlers() {}

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

void BrowserProcessHandler::CreateBrowserRpc(const CreateBrowserRequest& request) {
  CefWindowInfo windowInfo;
  windowInfo.SetAsWindowless(nullptr);  // no OS parent
  windowInfo.windowless_rendering_enabled = true;
  windowInfo.shared_texture_enabled = true;
  windowInfo.bounds = request.rectangle;

  CefBrowserSettings browserSettings;
  browserSettings.windowless_frame_rate = 30;

  CefRefPtr<CefRequestContext> requestContext =
      CefRequestContext::CreateContext(CefRequestContextSettings(), nullptr);
  CefRefPtr<CefDictionaryValue> extraInfo = CefDictionaryValue::Create();

  CefRefPtr<BrowserHandler> handler = new BrowserHandler(this, request.rectangle);

  CefRefPtr<CefBrowser> browser = CefBrowserHost::CreateBrowserSync(
      windowInfo,
      handler,
      request.url,
      browserSettings,
      extraInfo, 
      requestContext);

  handler->SetBrowser(browser);

  int browserId = -1;
  if (browser) {
    browserId = browser->GetIdentifier();
    browserHandlers[browserId] = handler;
    SDL_Log("Created browser on UI thread; id=%d url=%s", browserId,
            request.url.c_str());
    CreateBrowserResponse response;
    response.id = request.id;
    response.browserId = browserId;
    json j = response;
    outgoingMessageQueue.push(j.dump());
  } else {
    SDL_Log("CreateBrowserSync returned null");
  }
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
    return j.get<T>();
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

    std::string type = jsonRequest["type"].get<std::string>();
    UUID id = jsonRequest["id"].get<UUID>();

    if (type == "InitializeClientRequest") {
      InitializeClientRequest request = jsonRequest.get<InitializeClientRequest>();
      browserProcessHandler->OpenClientProcessHandle(request.clientProcessId);
      browserProcessHandler->SetClientMessageWindowHandle(
          reinterpret_cast<HWND>(request.clientMessageWindowHandle));
      InitializeClientResponse response;
      response.id = request.id;
      json jsonResponse = response;
      browserProcessHandler->SendMessage(jsonResponse.dump());
      continue;
    }

    if (type == "CreateBrowserRequest") {
      CreateBrowserRequest request = jsonRequest.get<CreateBrowserRequest>();
      CefPostTask(TID_UI, base::BindOnce(&BrowserProcessHandler::CreateBrowserRpc, browserProcessHandler, request));
      continue;
    }

    if (type == "EvalJavaScriptRequest") {
      EvalJavaScriptRequest evalRequest = jsonRequest.get<EvalJavaScriptRequest>();
      CefRefPtr<CefBrowser> browser =
          browserProcessHandler->GetBrowser(evalRequest.browserId);
            CefRefPtr<CefFrame> frame = browser->GetMainFrame();
      CefRefPtr<CefProcessMessage> message =
          CefProcessMessage::Create(kEvalMessage);
      message->GetArgumentList()->SetString(0, msg);
            frame->SendProcessMessage(PID_RENDERER, message);
      continue;
    };

    if (type == "CanGoBackRequest") {
      CanGoBackRequest request = jsonRequest.get<CanGoBackRequest>();
      CefRefPtr<CefBrowser> browser = browserProcessHandler->GetBrowser(request.browserId);
      CanGoBackResponse response;
      response.id = request.id;
      response.canGoBack = false;
      if (browser) {
        response.canGoBack = browser->CanGoBack();
      }
      json jsonResponse = response;
      browserProcessHandler->SendMessage(jsonResponse.dump());
      continue;
    }

    if (type == "CanGoForwardRequest") {
      CanGoForwardRequest request = jsonRequest.get<CanGoForwardRequest>();
      CefRefPtr<CefBrowser> browser = browserProcessHandler->GetBrowser(request.browserId);
      CanGoForwardResponse response;
      response.id = request.id;
      response.canGoForward = false;
      if (browser) {
        response.canGoForward = browser->CanGoForward();
      }
      json jsonResponse = response;
      browserProcessHandler->SendMessage(jsonResponse.dump());
      continue;
    }

    if (type == "GoBackRequest") {
      GoBackRequest request = jsonRequest.get<GoBackRequest>();
      CefRefPtr<CefBrowser> browser = browserProcessHandler->GetBrowser(request.browserId);
      if (browser) {
        browser->GoBack();
      }
      continue;
    }

    if (type == "GoForwardRequest") {
      GoForwardRequest request = jsonRequest.get<GoForwardRequest>();
      CefRefPtr<CefBrowser> browser = browserProcessHandler->GetBrowser(request.browserId);
      if (browser) {
        browser->GoForward();
      }
      continue;
    }

    if (type == "ReloadRequest") {
      ReloadRequest request = jsonRequest.get<ReloadRequest>();
      CefRefPtr<CefBrowser> browser = browserProcessHandler->GetBrowser(request.browserId);
      if (browser) {
        browser->Reload();
      }
      continue;
    }

    if (type == "FocusRequest") {
      FocusRequest request = jsonRequest.get<FocusRequest>();
      CefRefPtr<CefBrowser> browser = browserProcessHandler->GetBrowser(request.browserId);
      if (browser) {
        browser->GetHost()->SetFocus(true);
      }
      continue;
    }

    if (type == "DefocusRequest") {
      DefocusRequest request = jsonRequest.get<DefocusRequest>();
      CefRefPtr<CefBrowser> browser = browserProcessHandler->GetBrowser(request.browserId);
      if (browser) {
        browser->GetHost()->SetFocus(false);
      }
      continue;
    }

    if (type == "WasHiddenRequest") {
      WasHiddenRequest request = jsonRequest.get<WasHiddenRequest>();
      CefRefPtr<CefBrowser> browser = browserProcessHandler->GetBrowser(request.browserId);
      if (browser) {
        browser->GetHost()->WasHidden(request.hidden);
      }
      continue;
    }

    if (type == "LoadUrlRequest") {
      LoadUrlRequest request = jsonRequest.get<LoadUrlRequest>();
      CefRefPtr<CefBrowser> browser = browserProcessHandler->GetBrowser(request.browserId);
      if (browser) {
        CefRefPtr<CefFrame> frame = browser->GetMainFrame();
        frame->LoadURL(request.url);
      }
      continue;
    }

    if (type == "ResizeNotification") {
      ResizeNotification request = jsonRequest.get<ResizeNotification>();
      CefRefPtr<BrowserHandler> browserHandler = browserProcessHandler->GetBrowserHandler(request.browserId);
      if (browserHandler) {
        browserHandler->SetPageRectangle(request.newRectangle);
        browserHandler->GetBrowser()->GetHost()->WasResized();
      }
      continue;
    }

    if (type == "MouseClickEvent") {
      MouseClickEvent request = jsonRequest.get<MouseClickEvent>();
      CefRefPtr<CefBrowser> browser = browserProcessHandler->GetBrowser(request.browserId);
      if (browser) {
        browser->GetHost()->SendMouseClickEvent(
            request.mouseEvent,
            static_cast<CefBrowserHost::MouseButtonType>(request.button),
            request.mouseUp,
            request.clickCount);
      }
      continue;
    }

    if (type == "MouseMoveEvent") {
      MouseMoveEvent request = jsonRequest.get<MouseMoveEvent>();
      CefRefPtr<CefBrowser> browser = browserProcessHandler->GetBrowser(request.browserId);
      if (browser) {
        browser->GetHost()->SendMouseMoveEvent(
            request.mouseEvent,
            request.mouseLeave);
      }
      continue;
    }

    if (type == "MouseWheelEvent") {
      MouseWheelEvent request = jsonRequest.get<MouseWheelEvent>();
      CefRefPtr<CefBrowser> browser = browserProcessHandler->GetBrowser(request.browserId);
      if (browser) {
        browser->GetHost()->SendMouseWheelEvent(request.mouseEvent, request.deltaX, request.deltaY);
      }
      continue;
    }

    if (type == "KeyboardEvent") {
      KeyboardEvent request = jsonRequest.get<KeyboardEvent>();
      CefRefPtr<CefBrowser> browser = browserProcessHandler->GetBrowser(request.browserId);
      if (browser) {
        browser->GetHost()->SendKeyEvent(request.keyEvent);
      }
      continue;
    }

    if (type == "Acknowledgement") {
      // Try to find a waiting entry
      SDL_LockMutex(browserProcessHandler->responseMapMutex);
      auto it = browserProcessHandler->responseEntries.find(id);
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

    SDL_Log("RpcWorkerThread: unknown message type '%s'", type.c_str());
  }

  return 0;
}

template Acknowledgement
    BrowserProcessHandler::WaitForResponse<Acknowledgement>(UUID);