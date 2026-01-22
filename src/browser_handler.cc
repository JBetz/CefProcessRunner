#include "browser_handler.h"
#include "browser_process_handler.h"
#include "rpc.hpp"
#include <rpc.h>
#include <SDL3/sdl.h>
#include <include/cef_scheme.h>

#include <windows.h>

using json = nlohmann::json;

BrowserHandler::BrowserHandler(BrowserProcessHandler* browserProcessHandler, CefRect initialPageRectangle)
    : browserProcessHandler(browserProcessHandler),
      initialPageRectangle(initialPageRectangle) {}

CefRefPtr<CefBrowser> BrowserHandler::GetBrowser() {
  return this->browser;
}

void BrowserHandler::SetBrowser(CefRefPtr<CefBrowser> browser_) {
  this->browser = browser_;
}

std::optional<UUID> BrowserHandler::SendRpcRequest(std::string methodName, json arguments) {
  if (this->browser == nullptr) {
    SDL_Log("WARNING: Attempted to send RPC request '%s' but browser is not available yet or has already been destroyed.", methodName.c_str());
    return std::nullopt;
  }
  RpcRequest request;
  request.id = CreateUuid();
  request.className = "Browser";
  request.methodName = methodName;
  request.instanceId = this->browser->GetIdentifier();
  request.arguments = arguments;
  json jsonRequest = request;
  browserProcessHandler->SendMessage(jsonRequest.dump());
  return request.id;
}

std::optional<UUID> BrowserHandler::SendRpcRequest(std::string methodName) {
  return this->SendRpcRequest(methodName, json::object());
}

CefRefPtr<CefRenderHandler> BrowserHandler::GetRenderHandler() {
  return this;
}

CefRefPtr<CefDisplayHandler> BrowserHandler::GetDisplayHandler() {
  return this;
}

CefRefPtr<CefLifeSpanHandler> BrowserHandler::GetLifeSpanHandler() {
  return this;
}

CefRefPtr<CefContextMenuHandler> BrowserHandler::GetContextMenuHandler() {
  return this;
}

CefRefPtr<CefRequestHandler> BrowserHandler::GetRequestHandler() {
  return this;
}

bool BrowserHandler::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser_,
                                 CefRefPtr<CefFrame> frame,
                                 CefProcessId source_process,
                                 CefRefPtr<CefProcessMessage> message) {
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  if (!args || args->GetSize() == 0) {
    return false;
  }
  if (args->GetType(0) == VTYPE_STRING) {
    std::string payload = args->GetString(0).ToString();
    browserProcessHandler->BrowserProcessHandler::SendMessage(payload);
    return true;
  }
  return false;
}

void BrowserHandler::GetViewRect(CefRefPtr<CefBrowser> browser_,
                                 CefRect& rect) {
  if (this->browser == nullptr) {
    rect = initialPageRectangle;
  } else {
    std::optional<UUID> requestId = this->SendRpcRequest("GetViewRect");
    CefRect currentPageRectangle =
        browserProcessHandler->BrowserProcessHandler::WaitForResponse<CefRect>(
            requestId.value());
    rect = currentPageRectangle;
  }
}

void BrowserHandler::OnPaint(CefRefPtr<CefBrowser> browser_,
                             PaintElementType type,
                             const RectList& dirtyRects,
                             const void* buffer,
                             int width,
                             int height) {
  SDL_Log(
      "WARNING: BrowserHandler::OnPaint() was called, which means that this "
      "browser (id: %d) was set up incorrectly, or your machine does not have a "
      "GPU compatible with Chromium's hardware-accelerated rendering.",
      browser_->GetIdentifier());
}

void BrowserHandler::OnAcceleratedPaint(
    CefRefPtr<CefBrowser> browser_,
    PaintElementType type,
    const RectList& dirtyRects,
    const CefAcceleratedPaintInfo& info) {
  HANDLE sourceHandle = info.shared_texture_handle;
  std::optional<HANDLE> applicationProcessHandle =
      browserProcessHandler->GetClientProcessHandle();
  if (!applicationProcessHandle.has_value()) {
    SDL_Log("Error duplicating shared texture: Application process handle not initialized");
    return;
  } 
  HANDLE applicationHandle = applicationProcessHandle.value();
  HANDLE duplicateHandle = NULL;
  if (!DuplicateHandle(GetCurrentProcess(), sourceHandle, applicationHandle,
                       &duplicateHandle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
    SDL_Log(
        "Error duplicating shared texture: DuplicateHandle() call failed");
    return;
  }
  Browser_OnAcceleratedPaint arguments;
  arguments.elementType = type;
  arguments.format = info.format;
  arguments.sharedTextureHandle =
        reinterpret_cast<uintptr_t>(duplicateHandle);
  json jsonArguments = arguments;
  std::optional<UUID> requestId = this->SendRpcRequest("OnAcceleratedPaint", jsonArguments);
  if (!requestId.has_value()) {
    return;
  }
  browserProcessHandler->WaitForResponse<std::monostate>(requestId.value());
}

void BrowserHandler::OnTextSelectionChanged(
    CefRefPtr<CefBrowser> browser_,
    const CefString& selected_text,
    const CefRange& selected_range) {
  Browser_OnTextSelectionChanged arguments;
  arguments.selectedText = selected_text.ToString();
  arguments.selectedRangeFrom = selected_range.from;
  arguments.selectedRangeTo = selected_range.to;
  json jsonArguments = arguments;
  this->SendRpcRequest("OnTextSelectionChanged", jsonArguments);
}


bool BrowserHandler::GetScreenInfo(CefRefPtr<CefBrowser> browser_,
                                   CefScreenInfo& screen_info) {
  return false;
}

bool BrowserHandler::GetScreenPoint(CefRefPtr<CefBrowser> browser_,
                                    int viewX,
                                    int viewY,
                                    int& screenX,
                                    int& screenY) {
  return false;
}

void BrowserHandler::OnPopupShow(CefRefPtr<CefBrowser> browser_, bool show) {
  Browser_OnPopupShow arguments;
  arguments.show = show;
  json jsonArguments = arguments;
  this->SendRpcRequest("OnPopupShow", jsonArguments);
}

void BrowserHandler::OnPopupSize(CefRefPtr<CefBrowser> browser_,
                                 const CefRect& rect) {
  Browser_OnPopupSize arguments;
  arguments.rectangle = rect;
  json jsonArguments = arguments;
  this->SendRpcRequest("OnPopupSize", jsonArguments);
}

void BrowserHandler::OnAddressChange(CefRefPtr<CefBrowser> browser_,
                                    CefRefPtr<CefFrame> frame,
                                    const CefString& url) {
  Browser_OnAddressChange arguments;
  arguments.url = url.ToString();
  json jsonArguments = arguments;
  this->SendRpcRequest("OnAddressChange", jsonArguments);
}

void BrowserHandler::OnTitleChange(CefRefPtr<CefBrowser> browser_,
                                   const CefString& title) {
  Browser_OnTitleChange arguments;
  arguments.title = title.ToString();
  json jsonArguments = arguments;
  this->SendRpcRequest("OnTitleChange", jsonArguments);
}

bool BrowserHandler::OnConsoleMessage(CefRefPtr<CefBrowser> browser_,
                                      cef_log_severity_t level,
                                      const CefString& message,
                                      const CefString& source,
                                      int line) {
  Browser_OnConsoleMessage arguments;
  arguments.level = static_cast<int>(level);
  arguments.message = message.ToString();
  arguments.source = source.ToString();
  arguments.line = line;
  json jsonArguments = arguments;
  this->SendRpcRequest("OnConsoleMessage", jsonArguments);
  return true;
}

void BrowserHandler::OnLoadingProgressChange(CefRefPtr<CefBrowser> browser_,
                                            double progress) {
  Browser_OnLoadingProgressChange arguments;
  arguments.progress = progress;
  json jsonArguments = arguments;
  this->SendRpcRequest("OnLoadingProgressChange", jsonArguments);
}

bool BrowserHandler::OnCursorChange(CefRefPtr<CefBrowser> browser_,
                                    CefCursorHandle cursor,
                                    cef_cursor_type_t type,
                                    const CefCursorInfo& custom_cursor_info) {
  Browser_OnCursorChange arguments;
  arguments.cursorHandle = reinterpret_cast<uintptr_t>(cursor);
  arguments.cursorType = static_cast<int>(type);
  json jsonArguments = arguments;
  this->SendRpcRequest("OnCursorChange", jsonArguments);
  return true;
}

bool BrowserHandler::DoClose(CefRefPtr<CefBrowser> browser_) {
  return false;
}

void BrowserHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser_) {
  browserProcessHandler->RemoveBrowserHandler(browser_->GetIdentifier());
  std::optional<UUID> requestId = this->SendRpcRequest("OnBeforeClose");
  if (!requestId.has_value()) {
    return;
  }
  browserProcessHandler->WaitForResponse<std::monostate>(requestId.value());
}

bool BrowserHandler::OnBeforePopup(CefRefPtr<CefBrowser> browser_,
CefRefPtr<CefFrame> frame,
int popup_id,
const CefString& target_url,
const CefString& target_frame_name,
CefLifeSpanHandler::WindowOpenDisposition target_disposition,
bool user_gesture,
const CefPopupFeatures& popupFeatures,
CefWindowInfo& windowInfo,
CefRefPtr<CefClient>& client,
CefBrowserSettings& settings,
CefRefPtr<CefDictionaryValue>& extra_info,
bool* no_javascript_access) {
  Browser_OnBeforePopup arguments;
  arguments.targetUrl = target_url.ToString();
  arguments.targetFrameName = target_frame_name.ToString();
  arguments.targetDisposition = static_cast<int>(target_disposition);
  arguments.userGesture = user_gesture;
  json jsonArguments = arguments;
  std::optional<UUID> requestId = this->SendRpcRequest("OnBeforePopup", jsonArguments);
  if (!requestId.has_value()) {
    return true;
  }
  std::optional<bool> result = browserProcessHandler->WaitForResponse<bool>(requestId.value());
  return result.value_or(true);
}

bool BrowserHandler::OnOpenURLFromTab(CefRefPtr<CefBrowser> browser_,
                                    CefRefPtr<CefFrame> frame,
                                    const CefString& target_url,
                                    CefLifeSpanHandler::WindowOpenDisposition target_disposition,
                                    bool user_gesture) {
Browser_OnOpenUrlFromTab arguments;
  arguments.targetUrl = target_url.ToString();
  arguments.targetDisposition = static_cast<int>(target_disposition);
  arguments.userGesture = user_gesture;
  json jsonArguments = arguments;
  std::optional<UUID> requestId = this->SendRpcRequest("OnOpenUrlFromTab", jsonArguments);
  if (!requestId.has_value()) {
    return true;
  }
  std::optional<bool> result = browserProcessHandler->WaitForResponse<bool>(requestId.value());
  return result.value_or(true);
}

void BrowserHandler::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser_,
                                         CefRefPtr<CefFrame> frame,
                                         CefRefPtr<CefContextMenuParams> params,
                                         CefRefPtr<CefMenuModel> model) {
  Browser_OnBeforeContextMenu arguments;
  arguments.origin = CefPoint(params->GetXCoord(), params->GetYCoord());
  arguments.nodeType = static_cast<int>(params->GetTypeFlags());
  arguments.nodeMedia = static_cast<int>(params->GetMediaType());
  arguments.nodeMediaStateFlags = static_cast<int>(params->GetMediaStateFlags());
  arguments.nodeEditFlags = static_cast<int>(params->GetEditStateFlags());
  arguments.selectionText = params->GetSelectionText().ToString();
  json jsonArguments = arguments;
  this->SendRpcRequest("OnBeforeContextMenu", jsonArguments);
}

bool BrowserHandler::RunContextMenu(CefRefPtr<CefBrowser> browser_,
                                    CefRefPtr<CefFrame> frame,
                                    CefRefPtr<CefContextMenuParams> params,
                                    CefRefPtr<CefMenuModel> model,
                                    CefRefPtr<CefRunContextMenuCallback> callback) {
  callback->Cancel();
  return true;
}