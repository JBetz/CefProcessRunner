// Copyright (c) 2013 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#pragma once

#include <vector>

#include "include/cef_client.h"
#include "thread_safe_queue.hpp"
#include "rpc.hpp"

class BrowserProcessHandler;

class BrowserHandler : public CefClient, CefRenderHandler, CefDisplayHandler, CefLifeSpanHandler, CefContextMenuHandler {
 public:
  BrowserHandler(BrowserProcessHandler* browserProcessHandler, CefRect pageRectangle);

  CefRefPtr<CefBrowser> GetBrowser();
  void SetBrowser(CefRefPtr<CefBrowser> browser);
  void SetPageRectangle(const CefRect& rect);
  std::optional<UUID> SendRpcRequest(std::string methodName, json arguments);
  std::optional<UUID> SendRpcRequest(std::string methodName);

  // CefClient:
  CefRefPtr<CefRenderHandler> GetRenderHandler() override;
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

   // CefRenderHandler:
  void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
  void OnPaint(CefRefPtr<CefBrowser> browser,
               PaintElementType type,
               const RectList& dirtyRects,
               const void* buffer,
               int width,
               int height) override;
  void OnAcceleratedPaint(CefRefPtr<CefBrowser> browser,
                          PaintElementType type,
                          const RectList& dirtyRects,
                          const CefAcceleratedPaintInfo& info);
  bool GetScreenInfo(CefRefPtr<CefBrowser> browser,
                     CefScreenInfo& screen_info) override;
  bool GetScreenPoint(CefRefPtr<CefBrowser> browser,
                      int viewX,
                      int viewY,
                      int& screenX,
                      int& screenY) override;
  void OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) override;
  void OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect& rect) override;
  void OnTextSelectionChanged(CefRefPtr<CefBrowser> browser,
                              const CefString& selected_text,
                              const CefRange& selected_range) override;

  // CefDisplayHandler:
  void OnAddressChange(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                       cef_log_severity_t level,
                       const CefString& message,
                       const CefString& source,
                       int line) override;
  void OnLoadingProgressChange(CefRefPtr<CefBrowser> browser,
                               double progress) override;
  bool OnCursorChange(CefRefPtr<CefBrowser> browser,
                          CefCursorHandle cursor,
                          cef_cursor_type_t type,
                      const CefCursorInfo& custom_cursor_info) override;
  
  // CefLifeSpanHandler:
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  // CefContextMenuHandler:
  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override;
  bool RunContextMenu(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefContextMenuParams> params,
                        CefRefPtr<CefMenuModel> model,
                        CefRefPtr<CefRunContextMenuCallback> callback) override;
  bool OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefContextMenuParams> params,
                            int command_id,
                            EventFlags event_flags) override;
  void OnContextMenuDismissed(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame) override;

 private:
  BrowserProcessHandler* browserProcessHandler;
  CefRefPtr<CefBrowser> browser;
  CefRect pageRectangle;

  IMPLEMENT_REFCOUNTING(BrowserHandler);
};