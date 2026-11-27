#include "app/App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/Theme.h"

namespace lale {

namespace {

constexpr std::string_view kComposerFieldId = "composer";
constexpr std::string_view kSystemPromptFieldId = "system-prompt";
constexpr std::string_view kMessageViewportId = "message-viewport";
constexpr std::string_view kSettingsViewportId = "settings-viewport";
constexpr std::string_view kConversationIdPrefix = "conversation:";
constexpr std::string_view kStepperIdPrefix = "stepper:";
constexpr std::string_view kToggleIdPrefix = "toggle:";
constexpr std::string_view kNewConversationTitle = "New chat";
constexpr std::string_view kNoModelReply =
    "No model is loaded yet. Open Settings to choose a GGUF model. Replies "
    "will appear here once the llama.cpp backend is connected.";

void SetPadding(YGNodeRef node, float horizontal, float vertical) {
  YGNodeStyleSetPadding(node, YGEdgeHorizontal, horizontal);
  YGNodeStyleSetPadding(node, YGEdgeVertical, vertical);
}

void MakeRow(YGNodeRef node) {
  YGNodeStyleSetFlexDirection(node, YGFlexDirectionRow);
}

void MakeScrollViewport(ui::Node* viewport, float scroll_offset) {
  YGNodeStyleSetFlexGrow(viewport->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(viewport->yoga_node(), 1);
  YGNodeStyleSetMinHeight(viewport->yoga_node(), 0);
  YGNodeStyleSetOverflow(viewport->yoga_node(), YGOverflowScroll);
  viewport->style().scrolls_vertically = true;
  viewport->style().scroll_offset = scroll_offset;
}

void AddSeparator(ui::Node* parent) {
  ui::Node* separator = parent->AddChild();
  YGNodeStyleSetHeight(separator->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(separator->yoga_node(), 0);
  separator->style().background = theme::kBorder;
}

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
}

std::string FormatDecimal(float value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.2f", value);
  return buffer;
}

void RemoveLastCodePoint(std::string* text) {
  while (!text->empty() &&
         (static_cast<unsigned char>(text->back()) & 0xC0) == 0x80) {
    text->pop_back();
  }
  if (!text->empty()) text->pop_back();
}

std::string TrimWhitespace(std::string_view text) {
  size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return {};
  size_t last = text.find_last_not_of(" \t\r\n");
  return std::string(text.substr(first, last - first + 1));
}

std::string NormalizeLineEndings(std::string_view text) {
  std::string normalized;
  normalized.reserve(text.size());
  for (char character : text) {
    if (character != '\r') normalized.push_back(character);
  }
  return normalized;
}

std::string WindowsFontPath(const char* file_name) {
  char windows_directory[MAX_PATH] = {};
  GetWindowsDirectoryA(windows_directory, MAX_PATH);
  return std::string(windows_directory) + "\\Fonts\\" + file_name;
}

void ClampScrollViewport(ui::Node* root, std::string_view viewport_id,
                         float* scroll_offset, bool* pinned_to_bottom) {
  ui::Node* viewport = root->FindById(viewport_id);
  if (!viewport || viewport->children().empty()) return;
  float viewport_height = YGNodeLayoutGetHeight(viewport->yoga_node());
  float content_height =
      YGNodeLayoutGetHeight(viewport->children().front()->yoga_node());
  float maximum_offset = std::max(0.0f, content_height - viewport_height);
  if (pinned_to_bottom && *pinned_to_bottom) *scroll_offset = maximum_offset;
  *scroll_offset = std::clamp(*scroll_offset, 0.0f, maximum_offset);
  if (pinned_to_bottom) {
    *pinned_to_bottom = *scroll_offset >= maximum_offset - 1.0f;
  }
  viewport->style().scroll_offset = *scroll_offset;
}

}

App::App() {
  Conversation welcome_conversation;
  welcome_conversation.title = "Getting started";
  welcome_conversation.messages.push_back(
      {MessageRole::kAssistant,
       "Welcome to Lale. Choose a GGUF model in Settings, then type below. "
       "Press Enter to send and Shift+Enter for a new line."});
  conversations_.push_back(std::move(welcome_conversation));
}

App::~App() = default;

bool App::Initialize(int show_command) {
  LoadFonts();
  if (!window_.Create(L"Lale", 1280, 800, this)) return false;
  if (!presenter_.Initialize(window_.hwnd(), window_.client_width(),
                             window_.client_height())) {
    return false;
  }
  RenderFrame();
  window_.Show(show_command);
  return true;
}

int App::Run() {
  MSG message{};
  while (true) {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) return static_cast<int>(message.wParam);
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (needs_render_) {
      RenderFrame();
    } else {
      WaitMessage();
    }
  }
}

void App::LoadFonts() {
  std::string regular_font_key =
      ui::LoadFontFile(WindowsFontPath("segoeui.ttf"));
  std::string semibold_font_key =
      ui::LoadFontFile(WindowsFontPath("seguisb.ttf"));
  if (semibold_font_key.empty()) semibold_font_key = regular_font_key;
  body_font_ =
      std::make_unique<ui::Font>(regular_font_key, theme::kBodyPointSize);
  body_strong_font_ =
      std::make_unique<ui::Font>(semibold_font_key, theme::kBodyPointSize);
  small_font_ =
      std::make_unique<ui::Font>(regular_font_key, theme::kSmallPointSize);
  title_font_ =
      std::make_unique<ui::Font>(semibold_font_key, theme::kTitlePointSize);
}

void App::Invalidate() { needs_render_ = true; }

void App::ResetCaretBlink() {
  caret_visible_ = true;
  Invalidate();
}

void App::RenderFrame() {
  needs_render_ = false;
  interface_root_ = BuildInterface();
  LayoutInterface(interface_root_.get());
  renderer_.Resize(window_.client_width(), window_.client_height());
  renderer_.Render(*interface_root_, caret_visible_);
  presenter_.Present(renderer_.pixels(), renderer_.width(), renderer_.height());
}

void App::LayoutInterface(ui::Node* root) {
  const float client_width = static_cast<float>(window_.client_width());
  const float client_height = static_cast<float>(window_.client_height());
  YGNodeStyleSetWidth(root->yoga_node(), client_width);
  YGNodeStyleSetHeight(root->yoga_node(), client_height);
  YGNodeCalculateLayout(root->yoga_node(), client_width, client_height,
                        YGDirectionLTR);
  ClampScrollViewport(root, kMessageViewportId, &message_scroll_offset_,
                      &message_scroll_pinned_to_bottom_);
  ClampScrollViewport(root, kSettingsViewportId, &settings_scroll_offset_,
                      nullptr);
  root->ResolveBounds(0, 0, {0, 0, client_width, client_height});
}

std::unique_ptr<ui::Node> App::BuildInterface() {
  auto root = std::make_unique<ui::Node>();
  MakeRow(root->yoga_node());
  root->style().background = theme::kAppBackground;
  const float client_width = static_cast<float>(window_.client_width());
  const bool sidebar_fits_beside_settings =
      client_width >= theme::kSidebarWidth + theme::kSettingsPanelWidth +
                          theme::kMinimumChatWidth;
  if (!settings_panel_open_ || sidebar_fits_beside_settings) {
    BuildSidebar(root.get());
  }
  BuildChatArea(root.get());
  if (settings_panel_open_) BuildSettingsPanel(root.get());
  return root;
}

ui::Node* App::BuildLabel(ui::Node* parent, std::string text,
                          const ui::Font* font, ui::Color color,
                          bool wraps_text) {
  ui::Node* label = parent->AddChild();
  label->style().text = std::move(text);
  label->style().font = font;
  label->style().text_color = color;
  label->style().wraps_text = wraps_text;
  label->EnableTextMeasurement();
  return label;
}

ui::Node* App::BuildIconButton(ui::Node* parent, std::string id,
                               ui::IconKind icon) {
  const bool hovered = hovered_node_id_ == id;
  ui::Node* button = parent->AddChild(std::move(id));
  YGNodeStyleSetWidth(button->yoga_node(), 30);
  YGNodeStyleSetHeight(button->yoga_node(), 30);
  YGNodeStyleSetFlexShrink(button->yoga_node(), 0);
  button->style().corner_radius = 8;
  button->style().cursor = CursorKind::kHand;
  button->style().background =
      hovered ? theme::kHoverBackground : theme::kTransparent;
  button->style().icon = icon;
  button->style().icon_color =
      hovered ? theme::kTextPrimary : theme::kTextMuted;
  return button;
}

ui::Node* App::BuildButton(ui::Node* parent, std::string id, std::string label,
                           bool primary, bool enabled) {
  const bool hovered = enabled && hovered_node_id_ == id;
  ui::Node* button = parent->AddChild(enabled ? std::move(id) : std::string());
  MakeRow(button->yoga_node());
  YGNodeStyleSetAlignItems(button->yoga_node(), YGAlignCenter);
  YGNodeStyleSetJustifyContent(button->yoga_node(), YGJustifyCenter);
  YGNodeStyleSetFlexShrink(button->yoga_node(), 0);
  SetPadding(button->yoga_node(), 12, 7);
  button->style().corner_radius = 8;
  button->style().cursor = enabled ? CursorKind::kHand : CursorKind::kArrow;
  if (primary) {
    button->style().background = !enabled  ? theme::kAccentDisabled
                                 : hovered ? theme::kAccentHover
                                           : theme::kAccent;
  } else {
    button->style().background =
        hovered ? theme::kHoverBackground : theme::kTransparent;
  }
  BuildLabel(button, std::move(label), body_font_.get(),
             enabled ? theme::kTextPrimary : theme::kTextMuted);
  return button;
}

void App::BuildSidebar(ui::Node* parent) {
  ui::Node* sidebar = parent->AddChild();
  YGNodeRef sidebar_layout = sidebar->yoga_node();
  YGNodeStyleSetWidth(sidebar_layout, theme::kSidebarWidth);
  YGNodeStyleSetFlexShrink(sidebar_layout, 0);
  YGNodeStyleSetPadding(sidebar_layout, YGEdgeAll, 12);
  YGNodeStyleSetGap(sidebar_layout, YGGutterRow, 4);
  sidebar->style().background = theme::kSidebarBackground;

  ui::Node* brand = sidebar->AddChild();
  SetPadding(brand->yoga_node(), 10, 8);
  BuildLabel(brand, "Lale", title_font_.get(), theme::kTextPrimary);

  ui::Node* new_chat_button =
      BuildButton(sidebar, "new-chat", "New chat", false);
  YGNodeStyleSetJustifyContent(new_chat_button->yoga_node(),
                               YGJustifyFlexStart);
  YGNodeStyleSetMargin(new_chat_button->yoga_node(), YGEdgeBottom, 12);
  new_chat_button->style().border = theme::kBorder;
  new_chat_button->style().border_width = 1;

  ui::Node* recent_heading = sidebar->AddChild();
  SetPadding(recent_heading->yoga_node(), 10, 4);
  BuildLabel(recent_heading, "Recent", small_font_.get(), theme::kTextMuted);

  ui::Node* conversation_list = sidebar->AddChild();
  YGNodeStyleSetFlexGrow(conversation_list->yoga_node(), 1);
  YGNodeStyleSetGap(conversation_list->yoga_node(), YGGutterRow, 2);
  const float title_width = theme::kSidebarWidth - 24 - 20;
  for (size_t index = 0; index < conversations_.size(); ++index) {
    std::string item_id =
        std::string(kConversationIdPrefix) + std::to_string(index);
    ui::Node* item = conversation_list->AddChild(item_id);
    SetPadding(item->yoga_node(), 10, 8);
    item->style().corner_radius = 8;
    item->style().cursor = CursorKind::kHand;
    if (index == active_conversation_index_) {
      item->style().background = theme::kSelectedBackground;
    } else if (hovered_node_id_ == item_id) {
      item->style().background = theme::kHoverBackground;
    }
    BuildLabel(item,
               body_font_->Elide(conversations_[index].title, title_width),
               body_font_.get(), theme::kTextPrimary);
  }

  ui::Node* settings_button =
      BuildButton(sidebar, "settings-toggle", "Settings", false);
  YGNodeStyleSetJustifyContent(settings_button->yoga_node(),
                               YGJustifyFlexStart);
  if (settings_panel_open_) {
    settings_button->style().background = theme::kSelectedBackground;
  }
}

void App::BuildChatArea(ui::Node* parent) {
  ui::Node* chat_area = parent->AddChild();
  YGNodeStyleSetFlexGrow(chat_area->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(chat_area->yoga_node(), 1);
  YGNodeStyleSetMinWidth(chat_area->yoga_node(), 0);

  const Conversation& conversation = conversations_[active_conversation_index_];
  ui::Node* top_bar = chat_area->AddChild();
  MakeRow(top_bar->yoga_node());
  YGNodeStyleSetHeight(top_bar->yoga_node(), theme::kTopBarHeight);
  YGNodeStyleSetFlexShrink(top_bar->yoga_node(), 0);
  YGNodeStyleSetAlignItems(top_bar->yoga_node(), YGAlignCenter);
  YGNodeStyleSetJustifyContent(top_bar->yoga_node(), YGJustifySpaceBetween);
  YGNodeStyleSetGap(top_bar->yoga_node(), YGGutterColumn, 12);
  SetPadding(top_bar->yoga_node(), 20, 0);
  ui::Node* title = BuildLabel(top_bar, conversation.title,
                               body_strong_font_.get(), theme::kTextPrimary);
  YGNodeStyleSetFlexShrink(title->yoga_node(), 1);
  title->style().elides_text = true;

  ui::Node* model_badge = top_bar->AddChild();
  SetPadding(model_badge->yoga_node(), 10, 4);
  YGNodeStyleSetFlexShrink(model_badge->yoga_node(), 0);
  model_badge->style().corner_radius = 12;
  model_badge->style().border = theme::kBorder;
  model_badge->style().border_width = 1;
  BuildLabel(model_badge,
             generation_settings_.model_path.empty()
                 ? "No model loaded"
                 : generation_settings_.model_path,
             small_font_.get(), theme::kTextMuted);

  AddSeparator(chat_area);
  BuildMessageList(chat_area);
  BuildComposer(chat_area);
}

void App::BuildMessageList(ui::Node* parent) {
  ui::Node* viewport = parent->AddChild(std::string(kMessageViewportId));
  MakeScrollViewport(viewport, message_scroll_offset_);

  ui::Node* content = viewport->AddChild();
  YGNodeStyleSetFlexShrink(content->yoga_node(), 0);
  YGNodeStyleSetAlignItems(content->yoga_node(), YGAlignCenter);
  SetPadding(content->yoga_node(), 0, 24);

  ui::Node* column = content->AddChild();
  YGNodeStyleSetWidthPercent(column->yoga_node(), 100);
  YGNodeStyleSetMaxWidth(column->yoga_node(),
                         theme::kMessageColumnMaximumWidth);
  YGNodeStyleSetGap(column->yoga_node(), YGGutterRow, 14);
  SetPadding(column->yoga_node(), 24, 0);

  const Conversation& conversation = conversations_[active_conversation_index_];
  if (conversation.messages.empty()) {
    ui::Node* empty_state = column->AddChild();
    YGNodeStyleSetAlignItems(empty_state->yoga_node(), YGAlignCenter);
    YGNodeStyleSetGap(empty_state->yoga_node(), YGGutterRow, 8);
    YGNodeStyleSetMargin(empty_state->yoga_node(), YGEdgeTop, 96);
    BuildLabel(empty_state, "Start a conversation", title_font_.get(),
               theme::kTextPrimary);
    BuildLabel(empty_state,
               "Load a GGUF model in Settings to chat with llama.cpp.",
               body_font_.get(), theme::kTextMuted);
    return;
  }

  for (const ChatMessage& message : conversation.messages) {
    const bool from_user = message.role == MessageRole::kUser;
    ui::Node* row = column->AddChild();
    MakeRow(row->yoga_node());
    YGNodeStyleSetJustifyContent(
        row->yoga_node(), from_user ? YGJustifyFlexEnd : YGJustifyFlexStart);
    ui::Node* bubble = row->AddChild();
    YGNodeStyleSetMaxWidthPercent(bubble->yoga_node(), 85);
    YGNodeStyleSetFlexShrink(bubble->yoga_node(), 1);
    SetPadding(bubble->yoga_node(), 14, 10);
    bubble->style().corner_radius = 14;
    bubble->style().background =
        from_user ? theme::kUserBubble : theme::kAssistantBubble;
    BuildLabel(bubble, message.text, body_font_.get(), theme::kTextPrimary,
               true);
  }
}

void App::BuildComposer(ui::Node* parent) {
  const bool composer_focused = focused_field_id_ == kComposerFieldId;
  ui::Node* dock = parent->AddChild();
  YGNodeStyleSetFlexShrink(dock->yoga_node(), 0);
  YGNodeStyleSetAlignItems(dock->yoga_node(), YGAlignCenter);
  YGNodeStyleSetPadding(dock->yoga_node(), YGEdgeHorizontal, 24);
  YGNodeStyleSetPadding(dock->yoga_node(), YGEdgeTop, 8);
  YGNodeStyleSetPadding(dock->yoga_node(), YGEdgeBottom, 16);

  ui::Node* field_box = dock->AddChild(std::string(kComposerFieldId));
  MakeRow(field_box->yoga_node());
  YGNodeStyleSetWidthPercent(field_box->yoga_node(), 100);
  YGNodeStyleSetMaxWidth(field_box->yoga_node(),
                         theme::kMessageColumnMaximumWidth - 48);
  YGNodeStyleSetAlignItems(field_box->yoga_node(), YGAlignFlexEnd);
  YGNodeStyleSetGap(field_box->yoga_node(), YGGutterColumn, 10);
  YGNodeStyleSetPadding(field_box->yoga_node(), YGEdgeLeft, 14);
  YGNodeStyleSetPadding(field_box->yoga_node(), YGEdgeRight, 8);
  YGNodeStyleSetPadding(field_box->yoga_node(), YGEdgeVertical, 8);
  field_box->style().corner_radius = 14;
  field_box->style().background = theme::kInputBackground;
  field_box->style().border =
      composer_focused ? theme::kAccent : theme::kBorder;
  field_box->style().border_width = 1;
  field_box->style().cursor = CursorKind::kIBeam;

  ui::Node* field_text = BuildLabel(field_box, composer_text_, body_font_.get(),
                                    theme::kTextPrimary, true);
  YGNodeStyleSetFlexGrow(field_text->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(field_text->yoga_node(), 1);
  YGNodeStyleSetMaxHeight(field_text->yoga_node(),
                          theme::kComposerMaximumHeight);
  YGNodeStyleSetMargin(field_text->yoga_node(), YGEdgeBottom, 7);
  field_text->style().placeholder = "Message Lale";
  field_text->style().placeholder_color = theme::kTextMuted;
  field_text->style().shows_caret = composer_focused;

  BuildButton(field_box, "send", "Send", true,
              !TrimWhitespace(composer_text_).empty());

  ui::Node* hint = dock->AddChild();
  YGNodeStyleSetMargin(hint->yoga_node(), YGEdgeTop, 8);
  YGNodeStyleSetMaxWidthPercent(hint->yoga_node(), 100);
  ui::Node* hint_text =
      BuildLabel(hint, "Enter to send, Shift+Enter for a new line",
                 small_font_.get(), theme::kTextMuted);
  hint_text->style().elides_text = true;
}

void App::BuildSettingsPanel(ui::Node* parent) {
  ui::Node* panel = parent->AddChild();
  YGNodeStyleSetWidth(panel->yoga_node(), theme::kSettingsPanelWidth);
  YGNodeStyleSetFlexShrink(panel->yoga_node(), 0);
  panel->style().background = theme::kSidebarBackground;

  ui::Node* header = panel->AddChild();
  MakeRow(header->yoga_node());
  YGNodeStyleSetHeight(header->yoga_node(), theme::kTopBarHeight);
  YGNodeStyleSetFlexShrink(header->yoga_node(), 0);
  YGNodeStyleSetAlignItems(header->yoga_node(), YGAlignCenter);
  YGNodeStyleSetJustifyContent(header->yoga_node(), YGJustifySpaceBetween);
  YGNodeStyleSetPadding(header->yoga_node(), YGEdgeLeft, 20);
  YGNodeStyleSetPadding(header->yoga_node(), YGEdgeRight, 10);
  BuildLabel(header, "Settings", body_strong_font_.get(), theme::kTextPrimary);
  BuildIconButton(header, "settings-close", ui::IconKind::kClose);
  AddSeparator(panel);

  ui::Node* viewport = panel->AddChild(std::string(kSettingsViewportId));
  MakeScrollViewport(viewport, settings_scroll_offset_);
  ui::Node* content = viewport->AddChild();
  YGNodeStyleSetFlexShrink(content->yoga_node(), 0);
  YGNodeStyleSetPadding(content->yoga_node(), YGEdgeAll, 20);
  YGNodeStyleSetGap(content->yoga_node(), YGGutterRow, 10);

  auto add_section_heading = [&](std::string heading_text, bool first) {
    ui::Node* heading = content->AddChild();
    if (!first) YGNodeStyleSetMargin(heading->yoga_node(), YGEdgeTop, 14);
    BuildLabel(heading, std::move(heading_text), body_strong_font_.get(),
               theme::kTextPrimary);
  };

  add_section_heading("Model", true);
  ui::Node* model_card = content->AddChild();
  YGNodeStyleSetGap(model_card->yoga_node(), YGGutterRow, 4);
  SetPadding(model_card->yoga_node(), 12, 10);
  model_card->style().corner_radius = 8;
  model_card->style().background = theme::kPanelBackground;
  BuildLabel(model_card,
             generation_settings_.model_path.empty()
                 ? "No model loaded"
                 : generation_settings_.model_path,
             body_font_.get(), theme::kTextPrimary, true);
  BuildLabel(model_card, "Model loading arrives with the llama.cpp backend.",
             small_font_.get(), theme::kTextMuted, true);

  add_section_heading("Sampling", false);
  BuildStepperRow(content, "Temperature", "temperature",
                  FormatDecimal(generation_settings_.temperature));
  BuildStepperRow(content, "Top P", "top-p",
                  FormatDecimal(generation_settings_.top_p));
  BuildStepperRow(content, "Top K", "top-k",
                  std::to_string(generation_settings_.top_k));
  BuildStepperRow(content, "Max tokens", "max-tokens",
                  std::to_string(generation_settings_.maximum_tokens));
  BuildStepperRow(content, "Context size", "context-size",
                  std::to_string(generation_settings_.context_size));

  add_section_heading("System prompt", false);
  const bool system_prompt_focused = focused_field_id_ == kSystemPromptFieldId;
  ui::Node* prompt_box = content->AddChild(std::string(kSystemPromptFieldId));
  YGNodeStyleSetMinHeight(prompt_box->yoga_node(), 88);
  SetPadding(prompt_box->yoga_node(), 12, 10);
  prompt_box->style().corner_radius = 8;
  prompt_box->style().background = theme::kInputBackground;
  prompt_box->style().border =
      system_prompt_focused ? theme::kAccent : theme::kBorder;
  prompt_box->style().border_width = 1;
  prompt_box->style().cursor = CursorKind::kIBeam;
  ui::Node* prompt_text =
      BuildLabel(prompt_box, generation_settings_.system_prompt,
                 body_font_.get(), theme::kTextPrimary, true);
  prompt_text->style().placeholder = "No system prompt";
  prompt_text->style().placeholder_color = theme::kTextMuted;
  prompt_text->style().shows_caret = system_prompt_focused;

  add_section_heading("Output", false);
  BuildToggleRow(content, "Stream tokens", "stream",
                 generation_settings_.stream_tokens);
}

void App::BuildStepperRow(ui::Node* parent, std::string_view label,
                          std::string_view setting_key,
                          std::string value_text) {
  ui::Node* row = parent->AddChild();
  MakeRow(row->yoga_node());
  YGNodeStyleSetAlignItems(row->yoga_node(), YGAlignCenter);
  YGNodeStyleSetJustifyContent(row->yoga_node(), YGJustifySpaceBetween);
  BuildLabel(row, std::string(label), body_font_.get(), theme::kTextMuted);

  ui::Node* controls = row->AddChild();
  MakeRow(controls->yoga_node());
  YGNodeStyleSetAlignItems(controls->yoga_node(), YGAlignCenter);
  controls->style().corner_radius = 8;
  controls->style().background = theme::kPanelBackground;

  std::string id_prefix =
      std::string(kStepperIdPrefix) + std::string(setting_key) + ":";
  BuildIconButton(controls, id_prefix + "decrease", ui::IconKind::kMinus);
  ui::Node* value_box = controls->AddChild();
  YGNodeStyleSetWidth(value_box->yoga_node(), 64);
  YGNodeStyleSetAlignItems(value_box->yoga_node(), YGAlignCenter);
  BuildLabel(value_box, std::move(value_text), body_font_.get(),
             theme::kTextPrimary);
  BuildIconButton(controls, id_prefix + "increase", ui::IconKind::kPlus);
}

void App::BuildToggleRow(ui::Node* parent, std::string_view label,
                         std::string_view setting_key, bool enabled) {
  ui::Node* row = parent->AddChild();
  MakeRow(row->yoga_node());
  YGNodeStyleSetAlignItems(row->yoga_node(), YGAlignCenter);
  YGNodeStyleSetJustifyContent(row->yoga_node(), YGJustifySpaceBetween);
  BuildLabel(row, std::string(label), body_font_.get(), theme::kTextMuted);

  ui::Node* track =
      row->AddChild(std::string(kToggleIdPrefix) + std::string(setting_key));
  MakeRow(track->yoga_node());
  YGNodeStyleSetWidth(track->yoga_node(), 38);
  YGNodeStyleSetHeight(track->yoga_node(), 22);
  YGNodeStyleSetPadding(track->yoga_node(), YGEdgeAll, 3);
  YGNodeStyleSetJustifyContent(track->yoga_node(),
                               enabled ? YGJustifyFlexEnd : YGJustifyFlexStart);
  track->style().corner_radius = 11;
  track->style().background = enabled ? theme::kAccent : theme::kToggleOff;
  track->style().cursor = CursorKind::kHand;

  ui::Node* knob = track->AddChild();
  YGNodeStyleSetWidth(knob->yoga_node(), 16);
  YGNodeStyleSetHeight(knob->yoga_node(), 16);
  knob->style().corner_radius = 8;
  knob->style().background = theme::kToggleKnob;
}

bool App::IsTextField(std::string_view node_id) const {
  return node_id == kComposerFieldId || node_id == kSystemPromptFieldId;
}

std::string* App::FocusedText() {
  if (focused_field_id_ == kComposerFieldId) return &composer_text_;
  if (focused_field_id_ == kSystemPromptFieldId) {
    return &generation_settings_.system_prompt;
  }
  return nullptr;
}

void App::HandleClick(const std::string& node_id) {
  if (node_id == "new-chat") {
    StartNewConversation();
  } else if (StartsWith(node_id, kConversationIdPrefix)) {
    size_t index = std::stoul(node_id.substr(kConversationIdPrefix.size()));
    if (index < conversations_.size()) {
      active_conversation_index_ = index;
      message_scroll_pinned_to_bottom_ = true;
    }
  } else if (node_id == "settings-toggle") {
    settings_panel_open_ = !settings_panel_open_;
  } else if (node_id == "settings-close") {
    settings_panel_open_ = false;
  } else if (node_id == "send") {
    SendComposerMessage();
  } else if (StartsWith(node_id, kStepperIdPrefix)) {
    std::string_view rest =
        std::string_view(node_id).substr(kStepperIdPrefix.size());
    size_t separator = rest.find(':');
    int direction = rest.substr(separator + 1) == "increase" ? 1 : -1;
    StepSetting(rest.substr(0, separator), direction);
  } else if (node_id == std::string(kToggleIdPrefix) + "stream") {
    generation_settings_.stream_tokens = !generation_settings_.stream_tokens;
  }
}

void App::StepSetting(std::string_view setting_key, int direction) {
  GenerationSettings& settings = generation_settings_;
  if (setting_key == "temperature") {
    settings.temperature = std::clamp(
        std::round((settings.temperature + 0.05f * direction) * 100) / 100,
        0.0f, 2.0f);
  } else if (setting_key == "top-p") {
    settings.top_p =
        std::clamp(std::round((settings.top_p + 0.05f * direction) * 100) / 100,
                   0.05f, 1.0f);
  } else if (setting_key == "top-k") {
    settings.top_k = std::clamp(settings.top_k + 5 * direction, 0, 200);
  } else if (setting_key == "max-tokens") {
    settings.maximum_tokens =
        std::clamp(settings.maximum_tokens + 64 * direction, 64, 8192);
  } else if (setting_key == "context-size") {
    int stepped =
        direction > 0 ? settings.context_size * 2 : settings.context_size / 2;
    settings.context_size = std::clamp(stepped, 512, 131072);
  }
}

void App::SendComposerMessage() {
  std::string message_text = TrimWhitespace(composer_text_);
  if (message_text.empty()) return;
  Conversation& conversation = conversations_[active_conversation_index_];
  if (conversation.messages.empty() &&
      conversation.title == kNewConversationTitle) {
    conversation.title = message_text.substr(0, message_text.find('\n'));
  }
  conversation.messages.push_back({MessageRole::kUser, message_text});
  conversation.messages.push_back(
      {MessageRole::kAssistant, std::string(kNoModelReply)});
  composer_text_.clear();
  message_scroll_pinned_to_bottom_ = true;
}

void App::StartNewConversation() {
  const Conversation& active = conversations_[active_conversation_index_];
  if (!active.messages.empty()) {
    conversations_.insert(conversations_.begin(),
                          Conversation{std::string(kNewConversationTitle), {}});
    active_conversation_index_ = 0;
  }
  composer_text_.clear();
  focused_field_id_ = std::string(kComposerFieldId);
  message_scroll_pinned_to_bottom_ = true;
}

void App::OnResize(int client_width, int client_height) {
  presenter_.Resize(client_width, client_height);
  RenderFrame();
}

void App::OnPaint() { RenderFrame(); }

void App::OnMouseMove(float x, float y) {
  const ui::Node* hit =
      interface_root_ ? interface_root_->HitTest(x, y) : nullptr;
  std::string hit_id = hit ? hit->id() : std::string();
  if (hit_id != hovered_node_id_) {
    hovered_node_id_ = std::move(hit_id);
    Invalidate();
  }
}

void App::OnMouseLeave() {
  if (!hovered_node_id_.empty()) {
    hovered_node_id_.clear();
    Invalidate();
  }
}

void App::OnMouseDown(float x, float y) {
  const ui::Node* hit =
      interface_root_ ? interface_root_->HitTest(x, y) : nullptr;
  std::string hit_id = hit ? hit->id() : std::string();
  if (IsTextField(hit_id)) {
    focused_field_id_ = hit_id;
  } else if (hit_id != "send") {
    focused_field_id_.clear();
  }
  HandleClick(hit_id);
  ResetCaretBlink();
}

void App::OnMouseWheel(float x, float y, float wheel_steps) {
  if (!interface_root_) return;
  float scroll_delta = wheel_steps * theme::kWheelScrollDistance;
  ui::Node* settings_viewport = interface_root_->FindById(kSettingsViewportId);
  if (settings_viewport && settings_viewport->bounds().Contains(x, y)) {
    settings_scroll_offset_ -= scroll_delta;
    Invalidate();
    return;
  }
  ui::Node* message_viewport = interface_root_->FindById(kMessageViewportId);
  if (message_viewport && message_viewport->bounds().Contains(x, y)) {
    message_scroll_offset_ -= scroll_delta;
    message_scroll_pinned_to_bottom_ = false;
    Invalidate();
  }
}

void App::OnText(const std::string& utf8_text) {
  std::string* focused_text = FocusedText();
  if (!focused_text) return;
  focused_text->append(utf8_text);
  ResetCaretBlink();
}

void App::OnKey(UINT virtual_key, bool control_down, bool shift_down) {
  if (virtual_key == VK_ESCAPE) {
    if (settings_panel_open_) {
      settings_panel_open_ = false;
    } else {
      focused_field_id_.clear();
    }
    Invalidate();
    return;
  }
  std::string* focused_text = FocusedText();
  if (!focused_text) return;
  if (virtual_key == VK_BACK) {
    RemoveLastCodePoint(focused_text);
  } else if (virtual_key == VK_RETURN) {
    if (focused_field_id_ == kComposerFieldId && !shift_down) {
      SendComposerMessage();
    } else {
      focused_text->push_back('\n');
    }
  } else if (virtual_key == 'V' && control_down) {
    focused_text->append(NormalizeLineEndings(window_.ClipboardText()));
  } else {
    return;
  }
  ResetCaretBlink();
}

void App::OnCaretTimer() {
  if (focused_field_id_.empty()) return;
  caret_visible_ = !caret_visible_;
  Invalidate();
}

CursorKind App::CursorAt(float x, float y) {
  const ui::Node* hit =
      interface_root_ ? interface_root_->HitTest(x, y) : nullptr;
  return hit ? hit->style().cursor : CursorKind::kArrow;
}

}
