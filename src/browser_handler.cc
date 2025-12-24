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

CefRefPtr<CefRenderHandler> BrowserHandler::GetRenderHandler() {
  return this;
}

CefRefPtr<CefDisplayHandler> BrowserHandler::GetDisplayHandler() {
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
  Browser_OnAcceleratedPaint message;
  message.id = CreateUuid();
  message.instanceId = browser_->GetIdentifier();
  message.elementType = type;
  message.format = info.format;

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
      message.sharedTextureHandle = reinterpret_cast<uintptr_t>(duplicateHandle);
    }
  }
  json j = message;
  browserProcessHandler->SendMessage(j.dump());
  browserProcessHandler->WaitForResponse<Browser_Acknowledge>(message.id);
}

void BrowserHandler::OnTextSelectionChanged(
    CefRefPtr<CefBrowser> browser_,
    const CefString& selected_text,
    const CefRange& selected_range) {
  Browser_OnTextSelectionChanged message;
  message.id = CreateUuid();
  message.instanceId = browser_->GetIdentifier();
  message.selectedText = selected_text.ToString();
  message.selectedRangeFrom = selected_range.from;
  message.selectedRangeTo = selected_range.to;
  json j = message;
  browserProcessHandler->SendMessage(j.dump());
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
  Browser_OnAddressChange message;
  message.id = CreateUuid();
  message.instanceId = browser_->GetIdentifier();
  message.url = url.ToString();
  json j = message;
  browserProcessHandler->SendMessage(j.dump());
}

void BrowserHandler::OnTitleChange(CefRefPtr<CefBrowser> browser_,
                                   const CefString& title) {
  Browser_OnTitleChange message;
  message.id = CreateUuid();
  message.instanceId = browser_->GetIdentifier();
  message.title = title.ToString();
  json j = message;
  browserProcessHandler->SendMessage(j.dump());
}

bool BrowserHandler::OnConsoleMessage(CefRefPtr<CefBrowser> browser_,
                                      cef_log_severity_t level,
                                      const CefString& message,
                                      const CefString& source,
                                      int line) {
  Browser_OnConsoleMessage msg;
  msg.id = CreateUuid();
  msg.instanceId = browser_->GetIdentifier();
  msg.level = static_cast<int>(level);
  msg.message = message.ToString();
  msg.source = source.ToString();
  msg.line = line;
  json j = msg;
  browserProcessHandler->SendMessage(j.dump());
  return true;
}

void BrowserHandler::OnLoadingProgressChange(CefRefPtr<CefBrowser> browser_,
                                              double progress) {
  Browser_OnLoadingProgressChange message;
  message.id = CreateUuid();
  message.instanceId = browser_->GetIdentifier();
  message.progress = progress;
  json j = message;
  browserProcessHandler->SendMessage(j.dump());
}

bool BrowserHandler::OnCursorChange(CefRefPtr<CefBrowser> browser_,
                                    CefCursorHandle cursor,
                                    cef_cursor_type_t type,
                                    const CefCursorInfo& custom_cursor_info) {
  Browser_OnCursorChange message;
  message.id = CreateUuid();
  message.instanceId = browser_->GetIdentifier();
  message.cursorHandle = reinterpret_cast<uintptr_t>(cursor);
  message.cursorType = static_cast<int>(type);
  json j = message;
  browserProcessHandler->SendMessage(j.dump());
  return true;
}

bool BrowserHandler::DoClose(CefRefPtr<CefBrowser> browser_) {
  return false;
}

void BrowserHandler::OnBeforeClose(CefRefPtr<CefBrowser> browser_) {
  browserProcessHandler->RemoveBrowserHandler(browser_->GetIdentifier());
  Browser_OnBeforeClose message;
  message.id = CreateUuid();
  message.instanceId = browser_->GetIdentifier();
  json j = message;
  browserProcessHandler->SendMessage(j.dump());
  browserProcessHandler->WaitForResponse<Browser_Acknowledge>(message.id);
}
