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
inline UUID CreateUuid() {
  UUID uuid;
  RPC_STATUS resultCode = UuidCreate(&uuid);
  if (resultCode == RPC_S_OK) {
    return uuid;
  } else {
    throw std::runtime_error("UuidCreate() failed with error code: " +
                             std::to_string(resultCode));
  }
}

inline void to_json(json& j, const UUID& m) {
  RPC_CSTR str;
  RPC_STATUS resultCode = UuidToStringA(const_cast<UUID*>(&m), &str);
  if (resultCode == RPC_S_OK) {
    j = std::string(reinterpret_cast<char*>(str));
    RpcStringFreeA(&str);
  } else {
    throw std::runtime_error("UuidToStringA() failed with error code: " +
                             std::to_string(resultCode));
  }
}

inline void from_json(const json& j, UUID& m) {
  std::string str = j.get<std::string>();
  RPC_CSTR rpcStr = reinterpret_cast<RPC_CSTR>(const_cast<char*>(str.c_str()));
  RPC_STATUS resultCode = UuidFromStringA(rpcStr, &m);
  if (resultCode != RPC_S_OK) {
    throw std::runtime_error("UuidFromStringA() failed with error code: " +
                             std::to_string(resultCode));
  }
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

inline void from_json(const json& j, CefMouseEvent& m) {
  j.at("x").get_to(m.x);
  j.at("y").get_to(m.y);
  j.at("modifiers").get_to(m.modifiers);
}

inline void from_json(const json& j, CefKeyEvent& m) {
  j.at("type").get_to(m.type);
  j.at("modifiers").get_to(m.modifiers);
  j.at("windows_key_code").get_to(m.windows_key_code);
  j.at("native_key_code").get_to(m.native_key_code);
  j.at("is_system_key").get_to(m.is_system_key);
  std::string character = j.at("character");
  m.character = character.empty() ? 0 : character[0];
  std::string unmodified_character = j.at("unmodified_character");
  m.unmodified_character =
      unmodified_character.empty() ? 0 : unmodified_character[0];
  j.at("focus_on_editable_field").get_to(m.focus_on_editable_field);
}

// Request messages
struct RpcHeader {
  UUID id;
  std::string className;
  std::string methodName;
  int instanceId;
};

inline void from_json(const json& j, RpcHeader& m) {
  j.at("id").get_to(m.id);
  j.at("class").get_to(m.className);
  j.at("method").get_to(m.methodName);
  j.at("instanceId").get_to(m.instanceId);
}

struct Client_Initialize {
  int clientProcessId;
  uintptr_t clientMessageWindowHandle;
};

inline void from_json(const json& j, Client_Initialize& m) {
  j.at("clientProcessId").get_to(m.clientProcessId);
  j.at("clientMessageWindowHandle").get_to(m.clientMessageWindowHandle);
}

struct Client_CreateBrowser {
  std::string url;
  CefRect rectangle;
  std::optional<std::string> html;
};

inline void from_json(const json& j, Client_CreateBrowser& m) {
  j.at("url").get_to(m.url);
  j.at("rectangle").get_to(m.rectangle);
  j.at("html").get_to(m.html);
}

struct Browser_EvalJavaScript {
  UUID id;
  std::string evalJavaScript;
  std::string scriptUrl;
  int startLine;
};

inline void from_json(const json& j, Browser_EvalJavaScript& m) {
  j.at("id").get_to(m.id);
  j.at("evalJavaScript").get_to(m.evalJavaScript);
  j.at("scriptUrl").get_to(m.scriptUrl);
  j.at("startLine").get_to(m.startLine);
}

struct Browser_OnMouseClick {
  CefMouseEvent event;
  int button;
  bool mouseUp;
  int clickCount;
};

inline void from_json(const json& j, Browser_OnMouseClick& m) {
  j.at("event").get_to(m.event);
  j.at("button").get_to(m.button);
  j.at("mouseUp").get_to(m.mouseUp);
  j.at("clickCount").get_to(m.clickCount);
}

struct Browser_OnMouseMove {
  CefMouseEvent event;
  bool mouseLeave;
};

inline void from_json(const json& j, Browser_OnMouseMove& m) {
  j.at("event").get_to(m.event);
  j.at("mouseLeave").get_to(m.mouseLeave);
}

struct Browser_OnMouseWheel {
  CefMouseEvent event;
  int deltaX;
  int deltaY;
};

inline void from_json(const json& j, Browser_OnMouseWheel& m) {
  j.at("event").get_to(m.event);
  j.at("deltaX").get_to(m.deltaX);
  j.at("deltaY").get_to(m.deltaY);
}

struct Browser_OnKeyboardEvent {
  CefKeyEvent event;
};

inline void from_json(const json& j, Browser_OnKeyboardEvent& m) {
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
  int instanceId;
  NavigateDestination destination;
  std::optional<std::map<std::string, std::string>> formData;
  bool hashChange;
  std::string navigationType;
  bool userInitiated;
};

inline void to_json(json& j, const Browser_OnNavigate& m) {
  j = json::object();
  j["class"] = "Browser";
  j["method"] = "OnNavigate";
  j["id"] = m.id;
  j["instanceId"] = m.instanceId;
  j["destination"] = m.destination;
  j["formData"] = m.formData;
  j["hashChange"] = m.hashChange;
  j["navigationType"] = m.navigationType;
  j["userInitiated"] = m.userInitiated;
}

struct Browser_OnMouseOver {
  UUID id;
  int instanceId;
  std::string tagName;
  std::optional<std::string> inputType;
  std::optional<std::string> href;
  CefRect rectangle;
};

inline void to_json(json& j, const Browser_OnMouseOver& m) {
  j = json::object();
  j["class"] = "Browser";
  j["method"] = "OnMouseOver";
  j["id"] = m.id;
  j["instanceId"] = m.instanceId;
  j["tagName"] = m.tagName;
  j["inputType"] = m.inputType;
  j["href"] = m.href;
  j["rectangle"] = m.rectangle;
}

struct Browser_FocusOut {
  UUID id;
  int instanceId;
  std::optional<std::string> tagName;
  std::optional<std::string> inputType;
  std::optional<bool> isEditable;
};

inline void to_json(json& j, const Browser_FocusOut& m) {
  j = json::object();
  j["class"] = "Browser";
  j["method"] = "FocusOut";
  j["id"] = m.id;
  j["instanceId"] = m.instanceId;
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
  int instanceId;
};

inline void from_json(const json& j, Browser_CanGoBack& m) {
  j.at("id").get_to(m.id);
  j.at("instanceId").get_to(m.instanceId);
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

struct Browser_CanGoForwardResponse {
  UUID requestId;
  bool canGoForward;
};

inline void to_json(json& j, const Browser_CanGoForwardResponse& m) {
  j = json::object();
  j["requestId"] = m.requestId;
  j["canGoForward"] = m.canGoForward;
}

struct Browser_Focus {
  bool focus;
};

inline void from_json(const json& j, Browser_Focus& m) {
  j.at("focus").get_to(m.focus);
}

struct Browser_WasHidden {
  bool hidden;
};

inline void from_json(const json& j, Browser_WasHidden& m) {
  j.at("hidden").get_to(m.hidden);
}

struct Browser_LoadUrl {
  std::string url;
};

inline void from_json(const json& j, Browser_LoadUrl& m) {
  j.at("url").get_to(m.url);
}

struct Browser_NotifyResize {
  CefRect rectangle;
};

inline void from_json(const json& j, Browser_NotifyResize& m) {
  j.at("rectangle").get_to(m.rectangle);
}

struct Browser_OnAcceleratedPaint {
  UUID id;
  int instanceId;
  int elementType;
  uintptr_t sharedTextureHandle;
  int format;
};

inline void to_json(json& j, const Browser_OnAcceleratedPaint& m) {
  j = json::object();
  j["class"] = "Browser";
  j["method"] = "OnAcceleratedPaint";
  j["id"] = m.id;
  j["instanceId"] = m.instanceId;
  j["elementType"] = m.elementType;
  j["sharedTextureHandle"] = m.sharedTextureHandle;
  j["format"] = m.format;
}

struct Browser_OnTextSelectionChanged {
  UUID id;
  int instanceId;
  std::string selectedText;
  int selectedRangeFrom;
  int selectedRangeTo;
};

inline void to_json(json& j, const Browser_OnTextSelectionChanged& m) {
  j = json::object();
  j["class"] = "Browser";
  j["method"] = "OnTextSelectionChanged";
  j["id"] = m.id;
  j["instanceId"] = m.instanceId;
  j["selectedText"] = m.selectedText;
  j["selectedRangeFrom"] = m.selectedRangeFrom;
  j["selectedRangeTo"] = m.selectedRangeTo;
}

struct Browser_OnCursorChange {
  UUID id;
  int instanceId;
  uintptr_t cursorHandle;
  int cursorType;
  std::optional<CefCursorInfo> customCursorInfo;
};

inline void to_json(json& j, const Browser_OnCursorChange& m) {
  j = json::object();
  j["class"] = "Browser";
  j["method"] = "OnCursorChange";
  j["id"] = m.id;
  j["instanceId"] = m.instanceId;
  j["cursorHandle"] = m.cursorHandle;
  j["cursorType"] = m.cursorType;
}

struct Browser_OnAddressChange {
  UUID id;
  int instanceId;
  std::string url;
};

inline void to_json(json& j, const Browser_OnAddressChange& m) {
  j = json::object();
  j["class"] = "Browser";
  j["method"] = "OnAddressChange";
  j["id"] = m.id;
  j["instanceId"] = m.instanceId;
  j["url"] = m.url;
}

struct Browser_OnTitleChange {
  UUID id;
  int instanceId;
  std::string title;
};

inline void to_json(json& j, const Browser_OnTitleChange& m) {
  j = json::object();
  j["class"] = "Browser";
  j["method"] = "OnTitleChange";
  j["id"] = m.id;
  j["instanceId"] = m.instanceId;
  j["title"] = m.title;
}

struct Browser_OnConsoleMessage {
  UUID id;
  int instanceId;
  int level;
  std::string message;
  std::string source;
  int line;
};

inline void to_json(json& j, const Browser_OnConsoleMessage& m) {
  j = json::object();
  j["class"] = "Browser";
  j["method"] = "OnConsoleMessage";
  j["id"] = m.id;
  j["instanceId"] = m.instanceId;
  j["level"] = m.level;
  j["message"] = m.message;
  j["source"] = m.source;
  j["line"] = m.line;
}

struct Browser_OnLoadingProgressChange {
  UUID id;
  int instanceId;
  double progress;
};

inline void to_json(json& j, const Browser_OnLoadingProgressChange& m) {
  j = json::object();
  j["class"] = "Browser";
  j["method"] = "OnLoadingProgressChange";
  j["id"] = m.id;
  j["instanceId"] = m.instanceId;
  j["progress"] = m.progress;
}

struct Browser_OnFocusedNodeChanged {
  UUID id;
  int instanceId;
  std::string tagName;
  std::optional<std::string> inputType;
  bool isEditable;
};

inline void to_json(json& j, const Browser_OnFocusedNodeChanged& m) {
  j = json::object();
  j["class"] = "Browser";
  j["method"] = "OnFocusedNodeChanged";
  j["id"] = m.id;
  j["instanceId"] = m.instanceId;
  j["tagName"] = m.tagName;
  j["inputType"] = m.inputType;
  j["isEditable"] = m.isEditable;
}

struct Browser_Close {
  bool forceClose;
};

inline void from_json(const json& j, Browser_Close& m) {
  j.at("forceClose").get_to(m.forceClose);
}

struct Browser_TryCloseResponse {
  UUID requestId;
  bool canClose;
};

inline void to_json(json& j, const Browser_TryCloseResponse& m) {
  j = json::object();
  j["requestId"] = m.requestId;
  j["canClose"] = m.canClose;
}

struct Browser_Acknowledge {
  UUID acknowledge;
};

inline void from_json(const json& j, Browser_Acknowledge& m) {
  j.at("acknowledge").get_to(m.acknowledge);
}

struct Browser_OnBeforeClose {
  UUID id;
  int instanceId;
};

inline void to_json(json& j, const Browser_OnBeforeClose& m) {
  j = json::object();
  j["class"] = "Browser";
  j["method"] = "OnBeforeClose";
  j["id"] = m.id;
  j["instanceId"] = m.instanceId;
}