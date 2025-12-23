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

CefRefPtr<CefBrowser> BrowserProcessHandler::GetBrowser(int instanceId) {
  auto it = browserHandlers.find(instanceId);
  if (it != browserHandlers.end()) {
    return it->second->GetBrowser();
  }
  return nullptr;
}

CefRefPtr<BrowserHandler> BrowserProcessHandler::GetBrowserHandler(int instanceId) {
  auto it = browserHandlers.find(instanceId);
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

void BrowserProcessHandler::CreateBrowserRpc(const Client_CreateBrowser& request) {
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
    Client_CreateBrowserResponse response;
    response.requestId = request.id;
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

    std::string className = jsonRequest["class"].get<std::string>();
    std::string methodName = jsonRequest["method"].get<std::string>();
    UUID id = jsonRequest["id"].get<UUID>();

    if (className == "Client") {
       if (methodName == "Initialize") {
        Client_Initialize request = jsonRequest.get<Client_Initialize>();
        browserProcessHandler->OpenClientProcessHandle(request.clientProcessId);
        browserProcessHandler->SetClientMessageWindowHandle(
            reinterpret_cast<HWND>(request.clientMessageWindowHandle));
        continue;
      }

      if (methodName == "CreateBrowser") {
        Client_CreateBrowser request = jsonRequest.get<Client_CreateBrowser>();
        CefPostTask(TID_UI,
                    base::BindOnce(&BrowserProcessHandler::CreateBrowserRpc,
                                   browserProcessHandler, request));
        continue;
      }
    }

    if (className == "Browser") {
      if (methodName == "EvalJavaScript") {
        Browser_EvalJavaScript evalRequest =
            jsonRequest.get<Browser_EvalJavaScript>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(evalRequest.instanceId);
        CefRefPtr<CefFrame> frame = browser->GetMainFrame();
        CefRefPtr<CefProcessMessage> message =
            CefProcessMessage::Create(kEvalMessage);
        message->GetArgumentList()->SetString(0, msg);
        frame->SendProcessMessage(PID_RENDERER, message);
        continue;
      };

      if (methodName == "CanGoBack") {
        Browser_CanGoBack request = jsonRequest.get<Browser_CanGoBack>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        Browser_CanGoBackResponse response;
        response.requestId = request.id;
        response.canGoBack = false;
        if (browser) {
          response.canGoBack = browser->CanGoBack();
        }
        json jsonResponse = response;
        browserProcessHandler->SendMessage(jsonResponse.dump());
        continue;
      }

      if (methodName == "CanGoForward") {
        Browser_CanGoForward request = jsonRequest.get<Browser_CanGoForward>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        Browser_CanGoForwardResponse response;
        response.requestId = request.id;
        response.canGoForward = false;
        if (browser) {
          response.canGoForward = browser->CanGoForward();
        }
        json jsonResponse = response;
        browserProcessHandler->SendMessage(jsonResponse.dump());
        continue;
      }

      if (methodName == "Back") {
        Browser_Back request = jsonRequest.get<Browser_Back>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          browser->GoBack();
        }
        continue;
      }

      if (methodName == "Forward") {
        Browser_Forward request = jsonRequest.get<Browser_Forward>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          browser->GoForward();
        }
        continue;
      }

      if (methodName == "Reload") {
        Browser_Reload request = jsonRequest.get<Browser_Reload>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          browser->Reload();
        }
        continue;
      }

      if (methodName == "Focus") {
        Browser_Focus request = jsonRequest.get<Browser_Focus>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          browser->GetHost()->SetFocus(true);
        }
        continue;
      }

      if (methodName == "Defocus") {
        Browser_Defocus request = jsonRequest.get<Browser_Defocus>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          browser->GetHost()->SetFocus(false);
        }
        continue;
      }

      if (methodName == "WasHidden") {
        Browser_WasHidden request = jsonRequest.get<Browser_WasHidden>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          browser->GetHost()->WasHidden(request.hidden);
        }
        continue;
      }

      if (methodName == "LoadUrl") {
        Browser_LoadUrl request = jsonRequest.get<Browser_LoadUrl>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          CefRefPtr<CefFrame> frame = browser->GetMainFrame();
          frame->LoadURL(request.url);
        }
        continue;
      }

      if (methodName == "NotifyResize") {
        Browser_NotifyResize request = jsonRequest.get<Browser_NotifyResize>();
        CefRefPtr<BrowserHandler> browserHandler =
            browserProcessHandler->GetBrowserHandler(request.instanceId);
        if (browserHandler) {
          browserHandler->SetPageRectangle(request.rectangle);
          browserHandler->GetBrowser()->GetHost()->WasResized();
        }
        continue;
      }

      if (methodName == "Cut") {
        Browser_Cut request = jsonRequest.get<Browser_Cut>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          browser->GetFocusedFrame()->Cut();
        }
        continue;
      }

      if (methodName == "Copy") {
        Browser_Copy request = jsonRequest.get<Browser_Copy>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          browser->GetFocusedFrame()->Copy();
        }
        continue;
      }

      if (methodName == "Paste") {
        Browser_Paste request = jsonRequest.get<Browser_Paste>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          browser->GetFocusedFrame()->Paste();
        }
        continue;
      }

      if (methodName == "Delete") {
        Browser_Delete request = jsonRequest.get<Browser_Delete>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          browser->GetFocusedFrame()->Delete();
        }
        continue;
      }

      if (methodName == "Undo") {
        Browser_Undo request = jsonRequest.get<Browser_Undo>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          browser->GetFocusedFrame()->Undo();
        }
        continue;
      }

      if (methodName == "Redo") {
        Browser_Redo request = jsonRequest.get<Browser_Redo>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          browser->GetFocusedFrame()->Redo();
        }
        continue;
      }

      if (methodName == "SelectAll") {
        Browser_SelectAll request = jsonRequest.get<Browser_SelectAll>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          browser->GetFocusedFrame()->SelectAll();
        }
        continue;
      }

      if (methodName == "OnMouseClick") {
        Browser_OnMouseClick request = jsonRequest.get<Browser_OnMouseClick>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          browser->GetHost()->SendMouseClickEvent(
              request.event,
              static_cast<CefBrowserHost::MouseButtonType>(request.button),
              request.mouseUp, request.clickCount);
        }
        continue;
      }

      if (methodName == "OnMouseMove") {
        Browser_OnMouseMove request = jsonRequest.get<Browser_OnMouseMove>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          browser->GetHost()->SendMouseMoveEvent(request.event,
                                                 request.mouseLeave);
        }
        continue;
      }

      if (methodName == "OnMouseWheel") {
        Browser_OnMouseWheel request = jsonRequest.get<Browser_OnMouseWheel>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          browser->GetHost()->SendMouseWheelEvent(request.event, request.deltaX,
                                                  request.deltaY);
        }
        continue;
      }

      if (methodName == "OnKeyboardEvent") {
        Browser_OnKeyboardEvent request =
            jsonRequest.get<Browser_OnKeyboardEvent>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
          browser->GetHost()->SendKeyEvent(request.event);
        }
        continue;
      }

      if (methodName == "Close") {
        Browser_Close request = jsonRequest.get<Browser_Close>();
        CefRefPtr<CefBrowser> browser =
            browserProcessHandler->GetBrowser(request.instanceId);
        if (browser) {
            browser->GetHost()->CloseBrowser(request.forceClose);
        }
        continue;
      }

      if (methodName == "Acknowledge") {
        Browser_Acknowledge request = jsonRequest.get<Browser_Acknowledge>();
        // Try to find a waiting entry
        SDL_LockMutex(browserProcessHandler->responseMapMutex);
        auto it =
            browserProcessHandler->responseEntries.find(request.acknowledge);
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

    SDL_Log("RpcWorkerThread: unknown message method '%s'", methodName.c_str());
  }

  return 0;
}

template Browser_Acknowledge
    BrowserProcessHandler::WaitForResponse<Browser_Acknowledge>(UUID);