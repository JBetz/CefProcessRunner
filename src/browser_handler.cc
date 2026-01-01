#include "browser_handler.h"
#include "browser_process_handler.h"
#include "rpc.hpp"
#include <rpc.h>
#include <SDL3/sdl.h>
#include <include/cef_scheme.h>

#include <windows.h>

using json = nlohmann::json;

BrowserHandler::BrowserHandler(BrowserProcessHandler* browserProcessHandler, CefRect pageRectangle)
    : browserProcessHandler(browserProcessHandler),
      pageRectangle(pageRectangle),
      popupRectangle(NULL),
      popupVisible(false) {}

CefRefPtr<CefBrowser> BrowserHandler::GetBrowser() {
  return this->browser;
}

void BrowserHandler::SetBrowser(CefRefPtr<CefBrowser> browser_) {
  this->browser = browser_;
}

void BrowserHandler::SetPageRectangle(const CefRect& rect) {
  this->pageRectangle = rect;
}

UUID BrowserHandler::SendRpcRequest(std::string methodName, json arguments) {
  RpcRequest request;
  request.id = CreateUuid();
  request.className = "Browser";
  request.methodName = methodName;
  request.instanceId = this->browser->GetIdentifier();
  request.arguments = json::object();
  json jsonRequest = request;
  browserProcessHandler->SendMessage(jsonRequest.dump());
  return request.id;
}

UUID BrowserHandler::SendRpcRequest(std::string methodName) {
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
  rect = pageRectangle;
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
  Browser_OnAcceleratedPaint arguments;
  arguments.elementType = type;
  arguments.format = info.format;

  // Duplicate the shared texture handle into the application process
  // (hardcoded PID = 1 for now) before sending it.
  HANDLE sourceHandle = info.shared_texture_handle;
  std::optional<HANDLE> applicationProcessHandle =
      browserProcessHandler->GetClientProcessHandle();
  if (!applicationProcessHandle.has_value()) {
    SDL_Log("Error duplicating shared texture: Application process handle not initialized");
  } else {
    HANDLE applicationHandle = applicationProcessHandle.value();
    HANDLE duplicateHandle = NULL;
    if (!DuplicateHandle(GetCurrentProcess(),
                         sourceHandle,
                         applicationHandle,
                         &duplicateHandle,
                         0,
                         FALSE,
                         DUPLICATE_SAME_ACCESS)) {
      SDL_Log("Error duplicating shared texture: DuplicateHandle() call failed");
    } else {
      arguments.sharedTextureHandle = reinterpret_cast<uintptr_t>(duplicateHandle);
    }
  }
  json jsonArguments = arguments;
  UUID requestId = this->SendRpcRequest("OnAcceleratedPaint", jsonArguments);
  browserProcessHandler->WaitForResponse<Browser_Acknowledge>(requestId);
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
  popupVisible = show;
}

void BrowserHandler::OnPopupSize(CefRefPtr<CefBrowser> browser_,
                                 const CefRect& rect) {
  *popupRectangle = rect;
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
  UUID requestId = this->SendRpcRequest("OnBeforeClose");
  browserProcessHandler->WaitForResponse<Browser_Acknowledge>(requestId);
}

void BrowserHandler::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser_,
                                         CefRefPtr<CefFrame> frame,
                                         CefRefPtr<CefContextMenuParams> params,
                                         CefRefPtr<CefMenuModel> model) {
}

bool BrowserHandler::RunContextMenu(CefRefPtr<CefBrowser> browser_,
                                    CefRefPtr<CefFrame> frame,
                                    CefRefPtr<CefContextMenuParams> params,
                                    CefRefPtr<CefMenuModel> model,
                                    CefRefPtr<CefRunContextMenuCallback> callback) {
  return false;
}

bool BrowserHandler::OnContextMenuCommand(CefRefPtr<CefBrowser> browser_,
                                          CefRefPtr<CefFrame> frame,
                                          CefRefPtr<CefContextMenuParams> params,
                                          int command_id,
                                          cef_event_flags_t event_flags) {
  return false;
}

void BrowserHandler::OnContextMenuDismissed(CefRefPtr<CefBrowser> browser_,
                                            CefRefPtr<CefFrame> frame) {}