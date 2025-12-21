#pragma once

#include "include/internal/cef_types_wrappers.h"
#include "json.hpp"
#include <optional>
#include <rpc.h>
#include <string>

using json = nlohmann::json;

// HANDLE
inline void to_json(json& j, const HANDLE& m) {
  j = static_cast<std::uint64_t>(reinterpret_cast<uintptr_t>(m));
}

inline void from_json(const json& j, HANDLE& m) {
  std::uint64_t v = j.get<std::uint64_t>();
  m = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(v));
}

// UUID
inline void to_json(json& j, const UUID& m) {
  RPC_CSTR str;
  UuidToStringA(const_cast<UUID*>(&m), &str);
  j = std::string(reinterpret_cast<char*>(str));
  RpcStringFreeA(&str);
}

inline void from_json(const json& j, UUID& m) {
  std::string str = j.get<std::string>();
  RPC_CSTR rpcStr = reinterpret_cast<RPC_CSTR>(const_cast<char*>(str.c_str()));
  UuidFromStringA(rpcStr, &m);
}

// CEF types
inline void to_json(json& j, const CefRect& m) {
  j = json::object();
  j["x"] = m.x;
  j["y"] = m.y;
  j["width"] = m.width;
  j["height"] = m.height;
}

inline void from_json(const json& j, CefRect& r) {
  j.at("x").get_to(r.x);
  j.at("y").get_to(r.y);
  j.at("width").get_to(r.width);
  j.at("height").get_to(r.height);
}

inline void to_json(json& j, const CefPoint& m) {
  j = json::object();
  j["x"] = m.x;
  j["y"] = m.y;
}

inline void to_json(json& j, const CefSize& m) {
  j = json::object();
  j["width"] = m.width;
  j["height"] = m.height;
}

inline void to_json(json& j, const CefCursorInfo& m) {
  j = json::object();
  j["hotspot"] = m.hotspot;
  j["image_scale_factor"] = m.image_scale_factor;
  j["buffer"] = "";
  j["size"] = m.size;
}

// Request messages
struct Client_Initialize {
  UUID id;
  int clientProcessId;
  uintptr_t clientMessageWindowHandle;
};

inline void from_json(const json& j, Client_Initialize& m) {
  j.at("id").get_to(m.id);
  j.at("clientProcessId").get_to(m.clientProcessId);
  j.at("clientMessageWindowHandle").get_to(m.clientMessageWindowHandle);
}

struct Client_CreateBrowser {
  UUID id;
  std::string url;
  CefRect rectangle;
  std::optional<std::string> html;
};

inline void from_json(const json& j, Client_CreateBrowser& m) {
  j.at("id").get_to(m.id);
  j.at("url").get_to(m.url);
  j.at("rectangle").get_to(m.rectangle);
  j.at("html").get_to(m.html);
}

struct Browser_EvalJavaScript {
  UUID id;
  int browserId;
  std::string evalJavaScript;
  std::string scriptUrl;
  int startLine;
};

inline void from_json(const json& j, Browser_EvalJavaScript& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
  j.at("evalJavaScript").get_to(m.evalJavaScript);
  j.at("scriptUrl").get_to(m.scriptUrl);
  j.at("startLine").get_to(m.startLine);
}

struct Browser_Cut {
  UUID id;
  int browserId;
};

inline void from_json(const json& j, Browser_Cut& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
}

struct Browser_Copy {
  UUID id;
  int browserId;
};

inline void from_json(const json& j, Browser_Copy& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
}

struct Browser_Paste {
  UUID id;
  int browserId;
};

inline void from_json(const json& j, Browser_Paste& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
}

struct Browser_Delete {
  UUID id;
  int browserId;
};

inline void from_json(const json& j, Browser_Delete& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
}

struct Browser_Undo {
  UUID id;
  int browserId;
};

inline void from_json(const json& j, Browser_Undo& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
}

struct Browser_Redo {
  UUID id;
  int browserId;
};

inline void from_json(const json& j, Browser_Redo& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
}

struct Browser_SelectAll {
  UUID id;
  int browserId;
};

inline void from_json(const json& j, Browser_SelectAll& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
}

inline void from_json(const json& j, MouseEvent& m) {
  j.at("x").get_to(m.x);
  j.at("y").get_to(m.y);
  j.at("modifiers").get_to(m.modifiers);
}

struct Browser_OnMouseClick {
  UUID id;
  int browserId;
  MouseEvent event;
  int button;
  bool mouseUp;
  int clickCount;
};

inline void from_json(const json& j, Browser_OnMouseClick& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
  j.at("event").get_to(m.event);
  j.at("button").get_to(m.button);
  j.at("mouseUp").get_to(m.mouseUp);
  j.at("clickCount").get_to(m.clickCount);
}

struct Browser_OnMouseMove {
  UUID id;
  int browserId;
  MouseEvent event;
  bool mouseLeave;
};

inline void from_json(const json& j, Browser_OnMouseMove& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
  j.at("event").get_to(m.event);
  j.at("mouseLeave").get_to(m.mouseLeave);
}

struct Browser_OnMouseWheel {
  UUID id;
  int browserId;
  MouseEvent event;
  int deltaX;
  int deltaY;
};

inline void from_json(const json& j, Browser_OnMouseWheel& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
  j.at("event").get_to(m.event);
  j.at("deltaX").get_to(m.deltaX);
  j.at("deltaY").get_to(m.deltaY);
}

struct Browser_OnKeyboardEvent {
  UUID id;
  int browserId;
  CefKeyEvent event;
};

inline void from_json(const json& j, CefKeyEvent& m) {
  j.at("type").get_to(m.type);
  j.at("modifiers").get_to(m.modifiers);
  j.at("windows_key_code").get_to(m.windows_key_code);
  j.at("native_key_code").get_to(m.native_key_code);
  j.at("is_system_key").get_to(m.is_system_key);
  std::string character = j.at("character");
  m.character = character.empty() ? 0 : character[0];
  std::string unmodified_character = j.at("unmodified_character");
  m.unmodified_character = unmodified_character.empty() ? 0 : unmodified_character[0];
  j.at("focus_on_editable_field").get_to(m.focus_on_editable_field);
}

inline void from_json(const json& j, Browser_OnKeyboardEvent& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
  j.at("event").get_to(m.event);
}

struct NavigateDestination {
  std::string id;
  int index;
  std::string key;
  bool sameDocument;
  std::string url;
};

inline void to_json(json& j, const NavigateDestination& m) {
  j = json::object();
  j["id"] = m.id;
  j["index"] = m.index;
  j["key"] = m.key;
  j["sameDocument"] = m.sameDocument;
  j["url"] = m.url;
}

struct Browser_OnNavigate {
  UUID id;
  int browserId;
  NavigateDestination destination;
  std::optional<std::map<std::string, std::string>> formData;
  bool hashChange;
  std::string navigationType;
  bool userInitiated;
};

inline void to_json(json& j, const Browser_OnNavigate& m) {
  j = json::object();
  j["type"] = "Browser_OnNavigate";
  j["id"] = m.id;
  j["browserId"] = m.browserId;
  j["destination"] = m.destination;
  j["formData"] = m.formData;
  j["hashChange"] = m.hashChange;
  j["navigationType"] = m.navigationType;
  j["userInitiated"] = m.userInitiated;
}

struct Browser_OnMouseOver {
  UUID id;
  int browserId;
  std::string tagName;
  std::optional<std::string> inputType;
  std::optional<std::string> href;
  CefRect rectangle;
};

inline void to_json(json& j, const Browser_OnMouseOver& m) {
  j = json::object();
  j["type"] = "Browser_OnMouseOver";
  j["id"] = m.id;
  j["browserId"] = m.browserId;
  j["tagName"] = m.tagName;
  j["inputType"] = m.inputType;
  j["href"] = m.href;
  j["rectangle"] = m.rectangle;
}

struct Browser_FocusOut {
  UUID id;
  int browserId;
  std::optional<std::string> tagName;
  std::optional<std::string> inputType;
  std::optional<bool> isEditable;
};

inline void to_json(json& j, const Browser_FocusOut& m) {
  j = json::object();
  j["type"] = "Browser_FocusOut";
  j["id"] = m.id;
  j["browserId"] = m.browserId;
  j["tagName"] = m.tagName; 
  j["inputType"] = m.inputType;
  j["isEditable"] = m.isEditable;
 }

// Response messages
struct Client_CreateBrowserResponse {
  UUID requestId;
  int browserId;
};

inline void to_json(json& j, const Client_CreateBrowserResponse& m) {
  j = json::object();
  j["requestId"] = m.requestId;
  j["browserId"] = m.browserId;
}

struct EvalJavaScriptError {
  int endColumn;
  int endPosition;
  int lineNumber;
  std::string message;
  std::string scriptResourceName;
  std::string sourceLine;
  int startColumn;
  int startPosition;
};

inline void to_json(json& j, const EvalJavaScriptError& m) {
  j = json::object();
  j["endColumn"] = m.endColumn;
  j["endPosition"] = m.endPosition;
  j["lineNumber"] = m.lineNumber;
  j["message"] = m.message;
  j["scriptResourceName"] = m.scriptResourceName;
  j["sourceLine"] = m.sourceLine;
  j["startColumn"] = m.startColumn;
  j["startPosition"] = m.startPosition;
}

struct Browser_EvalJavaScriptResponse {
  UUID requestId;
  bool success;
  std::optional<EvalJavaScriptError> error;
  std::optional<std::string> result;
};

inline void to_json(json& j, const Browser_EvalJavaScriptResponse& m) {
  j = json::object();
  j["requestId"] = m.requestId;
  j["success"] = m.success;
  if (m.error.has_value()) {
    j["error"] = m.error.value();
  }
  if (m.result.has_value()) {
    j["result"] = m.result.value();
  }
}

struct Browser_CanGoBack {
  UUID id;
  int browserId;
};

inline void from_json(const json& j, Browser_CanGoBack& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
}

struct Browser_CanGoBackResponse {
  UUID requestId;
  bool canGoBack;
};

inline void to_json(json& j, const Browser_CanGoBackResponse& m) {
  j = json::object();
  j["requestId"] = m.requestId;
  j["canGoBack"] = m.canGoBack;
}

struct Browser_CanGoForward {
  UUID id;
  int browserId;
};

inline void from_json(const json& j, Browser_CanGoForward& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
}

struct Browser_CanGoForwardResponse {
  UUID requestId;
  bool canGoForward;
};

inline void to_json(json& j, const Browser_CanGoForwardResponse& m) {
  j = json::object();
  j["requestId"] = m.requestId;
  j["canGoForward"] = m.canGoForward;
}

struct Browser_Back {
  UUID id;
  int browserId;
};

inline void from_json(const json& j, Browser_Back& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
}

struct Browser_Forward {
  UUID id;
  int browserId;
};

inline void from_json(const json& j, Browser_Forward& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
}

struct Browser_Reload {
  UUID id;
  int browserId;
};

inline void from_json(const json& j, Browser_Reload& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
}

struct Browser_Focus {
  UUID id;
  int browserId;
};

inline void from_json(const json& j, Browser_Focus& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
}

struct Browser_Defocus {
  UUID id;
  int browserId;
};

inline void from_json(const json& j, Browser_Defocus& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
}

struct Browser_WasHidden {
  UUID id;
  int browserId;
  bool wasHidden;
};

inline void from_json(const json& j, Browser_WasHidden& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
  j.at("wasHidden").get_to(m.wasHidden);
}

struct Browser_LoadUrl {
  UUID id;
  int browserId;
  std::string url;
};

inline void from_json(const json& j, Browser_LoadUrl& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
  j.at("url").get_to(m.url);
}

struct Browser_NotifyResize {
  UUID id;
  int browserId;
  CefRect rectangle;
};

inline void from_json(const json& j, Browser_NotifyResize& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
  j.at("rectangle").get_to(m.rectangle);
}

struct Browser_OnAcceleratedPaint {
  UUID id;
  int browserId;
  int elementType;
  uintptr_t sharedTextureHandle;
  int format;
};

inline void to_json(json& j, const Browser_OnAcceleratedPaint& m) {
  j = json::object();
  j["type"] = "Browser_OnAcceleratedPaint";
  j["id"] = m.id;
  j["browserId"] = m.browserId;
  j["elementType"] = m.elementType;
  j["sharedTextureHandle"] = m.sharedTextureHandle;
  j["format"] = m.format;
}

struct Browser_OnTextSelectionChanged {
  UUID id;
  int browserId;
  std::string selectedText;
  int selectedRangeFrom;
  int selectedRangeTo;
};

inline void to_json(json& j, const Browser_OnTextSelectionChanged& m) {
  j = json::object();
  j["type"] = "Browser_OnTextSelectionChanged";
  j["id"] = m.id;
  j["browserId"] = m.browserId;
  j["selectedText"] = m.selectedText;
  j["selectedRangeFrom"] = m.selectedRangeFrom;
  j["selectedRangeTo"] = m.selectedRangeTo;
}

struct Browser_OnCursorChange {
  UUID id;
  int browserId;
  uintptr_t cursorHandle;
  int cursorType;
  std::optional<CefCursorInfo> customCursorInfo;
};

inline void to_json(json& j, const Browser_OnCursorChange& m) {
  j = json::object();
  j["type"] = "Browser_OnCursorChange";
  j["id"] = m.id;
  j["browserId"] = m.browserId;
  j["cursorHandle"] = m.cursorHandle;
  j["cursorType"] = m.cursorType;
}

struct Browser_OnAddressChange {
  UUID id;
  int browserId;
  std::string url;
};

inline void to_json(json& j, const Browser_OnAddressChange& m) {
  j = json::object();
  j["type"] = "Browser_OnAddressChange";
  j["id"] = m.id;
  j["browserId"] = m.browserId;
  j["url"] = m.url;
}

struct Browser_OnTitleChange {
  UUID id;
  int browserId;
  std::string title;
};

inline void to_json(json& j, const Browser_OnTitleChange& m) {
  j = json::object();
  j["type"] = "Browser_OnTitleChange";
  j["id"] = m.id;
  j["browserId"] = m.browserId;
  j["title"] = m.title;
}

struct Browser_OnConsoleMessage {
  UUID id;
  int browserId;
  int level;
  std::string message;
  std::string source;
  int line;
};

inline void to_json(json& j, const Browser_OnConsoleMessage& m) {
  j = json::object();
  j["type"] = "Browser_OnConsoleMessage";
  j["id"] = m.id;
  j["browserId"] = m.browserId;
  j["level"] = m.level;
  j["message"] = m.message;
  j["source"] = m.source;
  j["line"] = m.line;
}

struct Browser_OnLoadingProgressChange {
  UUID id;
  int browserId;
  double progress;
};

inline void to_json(json& j, const Browser_OnLoadingProgressChange& m) {
  j = json::object();
  j["type"] = "Browser_OnLoadingProgressChange";
  j["id"] = m.id;
  j["browserId"] = m.browserId;
  j["progress"] = m.progress;
}

struct Browser_OnFocusedNodeChanged {
  UUID id;
  int browserId;
  std::string tagName;
  std::optional<std::string> inputType;
  bool isEditable;
};

inline void to_json(json& j, const Browser_OnFocusedNodeChanged& m) {
  j = json::object();
  j["type"] = "Browser_OnFocusedNodeChanged";
  j["id"] = m.id;
  j["browserId"] = m.browserId;
  j["tagName"] = m.tagName;
  j["inputType"] = m.inputType;
  j["isEditable"] = m.isEditable;
}

struct Browser_Acknowledge {
  UUID id;
  int browserId;
  UUID acknowledge;
};

inline void from_json(const json& j, Browser_Acknowledge& m) {
  j.at("id").get_to(m.id);
  j.at("browserId").get_to(m.browserId);
  j.at("acknowledge").get_to(m.acknowledge);
}