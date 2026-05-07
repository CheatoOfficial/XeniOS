/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/menu_item_wx.h"

#include <atomic>
#include <string>

namespace xe {
namespace ui {

std::unique_ptr<MenuItem> MenuItem::Create(Type type, const std::string& text,
                                           const std::string& hotkey,
                                           std::function<void()> callback) {
  return std::make_unique<WxMenuItem>(type, text, hotkey, std::move(callback));
}

int WxMenuItem::AllocateId() {
  static std::atomic<int> next_id{wxID_HIGHEST + 1};
  return next_id.fetch_add(1);
}

WxMenuItem::WxMenuItem(Type type, const std::string& text,
                       const std::string& hotkey,
                       std::function<void()> callback)
    : MenuItem(type, text, hotkey, std::move(callback)) {
  switch (type) {
    case Type::kNormal:
      menu_bar_ = new wxMenuBar();
      break;
    case Type::kPopup:
      menu_ = new wxMenu();
      break;
    case Type::kString:
      wx_id_ = AllocateId();
      break;
    case Type::kSeparator:
      break;
  }
}

WxMenuItem::~WxMenuItem() {
  // wx frame/menu hierarchy owns attached children; no manual delete.
}

void WxMenuItem::SetEnabled(bool enabled) {
  if (!parent_item_) return;
  auto* parent_wx = static_cast<WxMenuItem*>(parent_item_);
  if (type_ == Type::kString && parent_wx->menu_) {
    parent_wx->menu_->Enable(wx_id_, enabled);
    return;
  }
  if (type_ == Type::kPopup) {
    if (parent_wx->menu_bar_ && menu_) {
      // Top-level menu in a menu bar — find its slot by wxMenu* identity.
      for (size_t i = 0; i < parent_wx->menu_bar_->GetMenuCount(); ++i) {
        if (parent_wx->menu_bar_->GetMenu(i) == menu_) {
          parent_wx->menu_bar_->EnableTop(i, enabled);
          return;
        }
      }
    } else if (parent_wx->menu_ && wx_menu_item_) {
      parent_wx->menu_->Enable(wx_menu_item_->GetId(), enabled);
    }
  }
}

void WxMenuItem::OnChildAdded(MenuItem* child_item) {
  auto* child = static_cast<WxMenuItem*>(child_item);

  if (type_ == Type::kNormal && menu_bar_) {
    if (child->type_ == Type::kPopup && child->menu_) {
      menu_bar_->Append(child->menu_, wxString::FromUTF8(child->text_));
    }
  } else if (menu_) {
    switch (child->type_) {
      case Type::kPopup:
        if (child->menu_) {
          child->wx_menu_item_ = menu_->AppendSubMenu(
              child->menu_, wxString::FromUTF8(child->text_));
        }
        break;
      case Type::kString: {
        wxString label = wxString::FromUTF8(child->text_);
        if (!child->hotkey_.empty()) {
          label += wxT("\t") + wxString::FromUTF8(child->hotkey_);
        }
        child->wx_menu_item_ = menu_->Append(child->wx_id_, label);
        id_to_child_[child->wx_id_] = child;
        menu_->Bind(wxEVT_MENU, &WxMenuItem::OnMenuItemSelected, this,
                    child->wx_id_);
        break;
      }
      case Type::kSeparator:
        child->wx_menu_item_ = menu_->AppendSeparator();
        break;
      default:
        break;
    }
  }
}

void WxMenuItem::OnChildRemoved(MenuItem* child_item) {
  auto* child = static_cast<WxMenuItem*>(child_item);
  if (!menu_) return;
  if (child->wx_id_ != wxID_NONE) {
    menu_->Unbind(wxEVT_MENU, &WxMenuItem::OnMenuItemSelected, this,
                  child->wx_id_);
    id_to_child_.erase(child->wx_id_);
  }
  if (child->wx_menu_item_) {
    menu_->Destroy(child->wx_menu_item_);
    child->wx_menu_item_ = nullptr;
  }
}

void WxMenuItem::OnMenuItemSelected(wxCommandEvent& event) {
  auto it = id_to_child_.find(event.GetId());
  if (it == id_to_child_.end()) return;
  it->second->OnSelected();
}

}  // namespace ui
}  // namespace xe
