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

// Callback for CefBrowserHost::DownloadImage
class DownloadImageCallback : public CefDownloadImageCallback {
public:
  DownloadImageCallback(BrowserProcessHandler* handler, const UUID& requestId, const std::string& imageUrl)
      : handler(handler), requestId(requestId), imageUrl(imageUrl) {}

  void OnDownloadImageFinished(const CefString& image_url,
                               int http_status_code,
                               CefRefPtr<CefImage> image_) override {
    Browser_OnDownloadImageComplete arguments;
    arguments.imageUrl = imageUrl;
    arguments.httpStatusCode = http_status_code;

    if (image_ && !image_->IsEmpty()) {
      float scale_factor = 1.0f;
      int pixel_width = 0;
      int pixel_height = 0;
      
      CefRefPtr<CefBinaryValue> binary = image_->GetAsPNG(scale_factor, true, pixel_width, pixel_height);
      if (binary && binary->GetSize() > 0) {
        std::vector<uint8_t> data(binary->GetSize());
        binary->GetData(data.data(), data.size(), 0);
        PNGImageData image;
        image.data = data;
        image.width = pixel_width;
        image.height = pixel_height;
        arguments.images.push_back(image);
      }
    }

    RpcResponse response;
    response.requestId = requestId;
    response.success = true;
    response.returnValue = arguments;
    json jsonResponse = response;
    
    handler->SendMessage(jsonResponse.dump());
  }

private:
  BrowserProcessHandler* handler;
  UUID requestId;
  std::string imageUrl;

  IMPLEMENT_REFCOUNTING(DownloadImageCallback);
};

BrowserProcessHandler::BrowserProcessHandler(HANDLE applicationProcessHandle, HWND applicationMessageWindowHandle, int windowMessageId)
: applicationProcessHandle(applicationProcessHandle),
  applicationMessageWindowHandle(applicationMessageWindowHandle),
  windowMessageId(windowMessageId),
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

HANDLE BrowserProcessHandler::GetApplicationProcessHandle() {
  return this->applicationProcessHandle;
}

HWND BrowserProcessHandler::GetApplicationMessageWindowHandle() {
  return this->applicationMessageWindowHandle;
}

int BrowserProcessHandler::GetWindowMessageId() {
  return this->windowMessageId;
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

  HANDLE hEvent = OpenEvent(EVENT_MODIFY_STATE, FALSE, L"ChromiumSocketReady");
  if (hEvent == NULL) {
    SDL_Log("Error opening ChromiumSocketReady event: %lu", GetLastError());
    abort();
  }
  if (!SetEvent(hEvent)) {
    SDL_Log("Error signaling ChromiumSocketReady event: %lu", GetLastError());
    abort();
  }
  CloseHandle(hEvent);
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
    RpcResponse response;
    response.requestId = requestId;
    response.success = true;
    response.returnValue = browserId;
    json j = response;
    this->SendMessage(j.dump());
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
  RpcResponse response;
  response.requestId = requestId;
  response.returnValue = canClose;
  json jsonResponse = response;
  this->SendMessage(jsonResponse.dump());
}

void BrowserProcessHandler::SendMessage(std::string payload) {
  outgoingMessageQueue.push(payload);
}

void BrowserProcessHandler::HandleRpcRequest(RpcRequest request) {
if (request.className == "Client") {
  if (request.methodName == "CreateBrowser") {
      Client_CreateBrowser arguments =
          request.arguments.get<Client_CreateBrowser>();
      CefPostTask(TID_UI, base::BindOnce(
                              &BrowserProcessHandler::Client_CreateBrowserRpc,
                              this, request.id, arguments.url,
                              arguments.rectangle));
      return;
    }

    if (request.methodName == "Shutdown") {
      CefPostTask(TID_UI,
                  base::BindOnce(&BrowserProcessHandler::Client_ShutdownRpc, this));
      return;
    }
  }

  if (request.className == "Browser") {
    CefRefPtr<BrowserHandler> browserHandler = this->GetBrowserHandler(request.instanceId);
    CefRefPtr<CefBrowser> browser = browserHandler->GetBrowser();
    if (!browserHandler || !browser) {
      RpcResponse response;
      response.requestId = request.id;
      response.success = false;
      response.error =
          printf("Browser instance %d not found.", request.instanceId);
      json jsonResponse = response;
      this->SendMessage(jsonResponse.dump());
      return;
    }

    if (request.methodName == "EvalJavaScript") {
      CefRefPtr<CefFrame> frame = browser->GetMainFrame();
      CefRefPtr<CefProcessMessage> message =
          CefProcessMessage::Create(kEvalMessage);
      json jsonRequest = request;
      message->GetArgumentList()->SetString(0, jsonRequest.dump());
      frame->SendProcessMessage(PID_RENDERER, message);
      return;
    };

    if (request.methodName == "CanGoBack") {
      RpcResponse response;
      response.requestId = request.id;
      response.success = true;
      response.returnValue = browser->CanGoBack();
      json jsonResponse = response;
      this->SendMessage(jsonResponse.dump());
      return;
    }

    if (request.methodName == "CanGoForward") {
      RpcResponse response;
      response.requestId = request.id;
      response.success = true;
      response.returnValue = browser->CanGoForward();
      json jsonResponse = response;
      this->SendMessage(jsonResponse.dump());
      return;
    }

    if (request.methodName == "Back") {
      browser->GoBack();
      return;
    }

    if (request.methodName == "Forward") {
      browser->GoForward();
      return;
    }

    if (request.methodName == "Reload") {
      browser->Reload();
      return;
    }

    if (request.methodName == "Focus") {
      Browser_Focus arguments = request.arguments.get<Browser_Focus>();
      browser->GetHost()->SetFocus(arguments.focus);
      return;
    }

    if (request.methodName == "WasHidden") {
      Browser_WasHidden arguments = request.arguments.get<Browser_WasHidden>();
      browser->GetHost()->WasHidden(arguments.hidden);
      return;
    }

    if (request.methodName == "LoadUrl") {
      Browser_LoadUrl arguments = request.arguments.get<Browser_LoadUrl>();
      CefRefPtr<CefFrame> frame = browser->GetMainFrame();
      frame->LoadURL(arguments.url);
      return;
    }

    if (request.methodName == "WasResized") {
      browserHandler->GetBrowser()->GetHost()->WasResized();
      return;
    }

    if (request.methodName == "Cut") {
      browser->GetFocusedFrame()->Cut();
      return;
    }

    if (request.methodName == "Copy") {
      browser->GetFocusedFrame()->Copy();
      return;
    }

    if (request.methodName == "Paste") {
      browser->GetFocusedFrame()->Paste();
      return;
    }

    if (request.methodName == "Delete") {
      browser->GetFocusedFrame()->Delete();
      return;
    }

    if (request.methodName == "Undo") {
      browser->GetFocusedFrame()->Undo();
      return;
    }

    if (request.methodName == "Redo") {
      browser->GetFocusedFrame()->Redo();
      return;
    }

    if (request.methodName == "SelectAll") {
      browser->GetFocusedFrame()->SelectAll();
      return;
    }

    if (request.methodName == "OnMouseClick") {
      Browser_OnMouseClick arguments =
          request.arguments.get<Browser_OnMouseClick>();
      browser->GetHost()->SendMouseClickEvent(
          arguments.event,
          static_cast<CefBrowserHost::MouseButtonType>(arguments.button),
          arguments.mouseUp, arguments.clickCount);
      return;
    }

    if (request.methodName == "OnMouseMove") {
      Browser_OnMouseMove arguments =
          request.arguments.get<Browser_OnMouseMove>();
      browser->GetHost()->SendMouseMoveEvent(arguments.event,
                                             arguments.mouseLeave);
      return;
    }

    if (request.methodName == "OnMouseWheel") {
      Browser_OnMouseWheel arguments =
          request.arguments.get<Browser_OnMouseWheel>();
      browser->GetHost()->SendMouseWheelEvent(arguments.event, arguments.deltaX,
                                              arguments.deltaY);
      return;
    }

    if (request.methodName == "OnKeyboardEvent") {
      Browser_OnKeyboardEvent arguments =
          request.arguments.get<Browser_OnKeyboardEvent>();
      browser->GetHost()->SendKeyEvent(arguments.event);
      return;
    }

    if (request.methodName == "Close") {
      Browser_Close arguments = request.arguments.get<Browser_Close>();
      CefPostTask(
          TID_UI,
          base::BindOnce(&BrowserProcessHandler::Browser_CloseRpc, this, browser, arguments.forceClose));
      return;
    }

    if (request.methodName == "TryClose") {
      CefPostTask(TID_UI,
                  base::BindOnce(&BrowserProcessHandler::Browser_TryCloseRpc, this, browser, request.id));
      return;
    }

    if (request.methodName == "DownloadImage") {
      Browser_DownloadImage arguments = request.arguments.get<Browser_DownloadImage>();
      CefRefPtr<DownloadImageCallback> callback = 
          new DownloadImageCallback(this, request.id, arguments.imageUrl);
      browser->GetHost()->DownloadImage(
          arguments.imageUrl,
          arguments.isFavicon,
          static_cast<uint32_t>(arguments.maxImageSize),
          arguments.bypassCache,
          callback);
      return;
    }
  }

  SDL_Log("RpcWorkerThread: unknown message method '%s'",
          request.methodName.c_str());
}

void BrowserProcessHandler::HandleRpcResponse(RpcResponse response) {
  SDL_LockMutex(this->responseMapMutex);
  auto it = this->responseEntries.find(response.requestId);
  if (it != this->responseEntries.end()) {
    ResponseEntry* e = it->second.get();
    SDL_LockMutex(e->mutex);
    e->payload = response.returnValue.dump();
    e->ready = true;
    SDL_SignalCondition(e->cond);
    SDL_UnlockMutex(e->mutex);
  }
  SDL_UnlockMutex(this->responseMapMutex);
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
        HWND applicationMessageWindowHandle = browserProcessHandler
            ->GetApplicationMessageWindowHandle();
        int windowMessageId = browserProcessHandler->GetWindowMessageId();
        NET_WaitUntilStreamSocketDrained(streamSocket, -1);
        PostMessageW(applicationMessageWindowHandle, windowMessageId, 0, 0);
    }
    SDL_Delay(1);  // tiny yield
  }
  return 0;
}

int BrowserProcessHandler::RpcWorkerThread(void* browserProcessHandlerPtr) {
  CefRefPtr<BrowserProcessHandler> browserProcessHandler =
      base::WrapRefCounted<BrowserProcessHandler>(
          static_cast<BrowserProcessHandler*>(browserProcessHandlerPtr));
  while (true) {
    std::string message = browserProcessHandler->incomingMessageQueue.pop();

    json jsonMessage;
    try {
      jsonMessage = json::parse(message);
    } catch (const nlohmann::json::parse_error& e_parse) {
      size_t previewLen = std::min<size_t>(message.size(), 256);
      std::string preview = message.substr(0, previewLen);
      SDL_Log(
          "RpcWorkerThread: JSON parse_error: %s at byte=%u "
          "payload_preview='%s'",
          e_parse.what(), static_cast<unsigned int>(e_parse.byte),
          preview.c_str());
      continue;
    } catch (const std::exception& e) {
      SDL_Log("RpcWorkerThread: JSON exception: %s", e.what());
      continue;
    }

    if (!jsonMessage.contains("requestId")) {
      browserProcessHandler->HandleRpcRequest(jsonMessage.get<RpcRequest>());
    }
    else {
      browserProcessHandler->HandleRpcResponse(jsonMessage.get<RpcResponse>());
    }
  }
  return 0;
}

template std::monostate 
    BrowserProcessHandler::WaitForResponse<std::monostate>(UUID);
template bool 
    BrowserProcessHandler::WaitForResponse<bool>(UUID);
template CefRect
    BrowserProcessHandler::WaitForResponse<CefRect>(UUID);
