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
      pageRectangle(pageRectangle) {}

CefRefPtr<CefBrowser> BrowserHandler::GetBrowser() {
  return this->browser;
}

void BrowserHandler::SetBrowser(CefRefPtr<CefBrowser> browser_) {
  this->browser = browser_;
}

void BrowserHandler::SetPageRectangle(const CefRect& rect) {
  this->pageRectangle = rect;
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
  HANDLE sourceHandle = info.shared_texture_handle;
  std::optional<HANDLE> applicationProcessHandle =
      browserProcessHandler->GetClientProcessHandle();
  if (!applicationProcessHandle.has_value()) {
    SDL_Log("Error duplicating shared texture: Application process handle not initialized");
  } else {
    HANDLE applicationHandle = applicationProcessHandle.value();
    HANDLE duplicateHandle = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), sourceHandle, applicationHandle,
                         &duplicateHandle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
      SDL_Log(
          "Error duplicating shared texture: DuplicateHandle() call failed");
    } else {
      arguments.sharedTextureHandle =
          reinterpret_cast<uintptr_t>(duplicateHandle);
    }
  }

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

void BrowserHandler::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser_,
                                         CefRefPtr<CefFrame> frame,
                                         CefRefPtr<CefContextMenuParams> params,
                                         CefRefPtr<CefMenuModel> model) {
  Browser_OnBeforeContextMenu arguments;
  arguments.nodeType = static_cast<int>(params->GetTypeFlags());
  arguments.nodeMedia = static_cast<int>(params->GetMediaType());
  arguments.nodeMediaStateFlags = static_cast<int>(params->GetMediaStateFlags());
  arguments.nodeEditFlags = static_cast<int>(params->GetEditStateFlags());
  arguments.selectionText = params->GetSelectionText().ToString();
  json jsonArguments = arguments;
  std::optional<UUID> requestId = this->SendRpcRequest("OnBeforeContextMenu", jsonArguments);
  if (!requestId.has_value()) {
    return;
  }
  ContextMenuConfiguration config = browserProcessHandler->WaitForResponse<ContextMenuConfiguration>(
          requestId.value());
  std::vector<ContextMenuCommand> commands = config.commands;
  for (size_t i = 0; i < commands.size(); i++) {
    ContextMenuCommand command = commands[i];
    model->InsertItemAt(command.index, command.commandId, command.label);
  }
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