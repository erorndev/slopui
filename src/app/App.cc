#include "app/App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/SystemAppearance.h"

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
constexpr std::string_view kNoModelNotice =
    "No model is loaded, so there is no reply yet. Choose a GGUF model in "
    "Settings once the llama.cpp backend is connected.";

constexpr int kInitialLogicalWidth = 1280;
constexpr int kInitialLogicalHeight = 800;

UINT DotsPerInchOverrideFromEnvironment() {
  char override_text[16] = {};
  DWORD length =
      GetEnvironmentVariableA("LALE_DPI", override_text, sizeof(override_text));
  if (length == 0 || length >= sizeof(override_text)) return 0;
  int parsed_dots_per_inch = std::atoi(override_text);
  return parsed_dots_per_inch >= 48 && parsed_dots_per_inch <= 480
             ? static_cast<UINT>(parsed_dots_per_inch)
             : 0;
}

void ApplyThemeOverrideFromEnvironment(SystemAppearance* appearance) {
  char override_text[16] = {};
  DWORD length = GetEnvironmentVariableA("LALE_THEME", override_text,
                                         sizeof(override_text));
  if (length == 0 || length >= sizeof(override_text)) return;
  if (std::strcmp(override_text, "light") == 0) {
    appearance->prefers_dark_mode = false;
  } else if (std::strcmp(override_text, "dark") == 0) {
    appearance->prefers_dark_mode = true;
  }
}

void SetPadding(YGNodeRef node, float horizontal, float vertical) {
  YGNodeStyleSetPadding(node, YGEdgeHorizontal, horizontal);
  YGNodeStyleSetPadding(node, YGEdgeVertical, vertical);
}

void MakeRow(YGNodeRef node) {
  YGNodeStyleSetFlexDirection(node, YGFlexDirectionRow);
  YGNodeStyleSetAlignItems(node, YGAlignCenter);
}

void MakeScrollViewport(ui::Node* viewport, float scroll_offset) {
  YGNodeStyleSetFlexGrow(viewport->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(viewport->yoga_node(), 1);
  YGNodeStyleSetMinHeight(viewport->yoga_node(), 0);
  YGNodeStyleSetOverflow(viewport->yoga_node(), YGOverflowScroll);
  viewport->style().scrolls_vertically = true;
  viewport->style().scroll_offset = scroll_offset;
}

ui::Node* AddHairline(ui::Node* parent, ui::Color color) {
  ui::Node* hairline = parent->AddChild();
  YGNodeStyleSetHeight(hairline->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(hairline->yoga_node(), 0);
  hairline->style().background = color;
  return hairline;
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

std::string FileNameFromPath(const std::string& path) {
  size_t separator = path.find_last_of("/\\");
  return separator == std::string::npos ? path : path.substr(separator + 1);
}

}

App::App() {
  conversations_.push_back({std::string(kNewConversationTitle), {}});
}

App::~App() = default;

bool App::Initialize(int show_command) {
  LoadFonts();
  if (!window_.Create(L"Lale", kInitialLogicalWidth, kInitialLogicalHeight,
                      this)) {
    return false;
  }
  ApplySystemAppearance();
  OnDpiChanged(window_.dots_per_inch());
  if (!presenter_.Initialize(window_.hwnd(), window_.client_width(),
                             window_.client_height())) {
    return false;
  }
  if (UINT override_dots_per_inch = DotsPerInchOverrideFromEnvironment()) {
    window_.OverrideDotsPerInch(override_dots_per_inch, kInitialLogicalWidth,
                                kInitialLogicalHeight);
  }
  focused_field_id_ = std::string(kComposerFieldId);
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
  caption_font_ =
      std::make_unique<ui::Font>(regular_font_key, theme::kCaptionPointSize);
  caption_strong_font_ =
      std::make_unique<ui::Font>(semibold_font_key, theme::kCaptionPointSize);
  body_font_ =
      std::make_unique<ui::Font>(regular_font_key, theme::kBodyPointSize);
  body_strong_font_ =
      std::make_unique<ui::Font>(semibold_font_key, theme::kBodyPointSize);
  headline_font_ =
      std::make_unique<ui::Font>(semibold_font_key, theme::kHeadlinePointSize);
  title_font_ =
      std::make_unique<ui::Font>(semibold_font_key, theme::kTitlePointSize);
}

void App::ApplySystemAppearance() {
  SystemAppearance appearance = ReadSystemAppearance();
  ApplyThemeOverrideFromEnvironment(&appearance);
  palette_ = theme::MakePalette(appearance);
  window_.UseDarkTitleBar(palette_.is_dark);
  Invalidate();
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
  renderer_.Render(*interface_root_, caret_visible_, device_pixels_per_point_);
  presenter_.Present(renderer_.pixels(), renderer_.width(), renderer_.height());
}

float App::LogicalClientWidth() const {
  return static_cast<float>(window_.client_width()) / device_pixels_per_point_;
}

float App::LogicalClientHeight() const {
  return static_cast<float>(window_.client_height()) / device_pixels_per_point_;
}

const ui::Node* App::HitTestDevicePoint(float device_x, float device_y) const {
  if (!interface_root_) return nullptr;
  return interface_root_->HitTest(device_x / device_pixels_per_point_,
                                  device_y / device_pixels_per_point_);
}

void App::LayoutInterface(ui::Node* root) {
  const float client_width = LogicalClientWidth();
  const float client_height = LogicalClientHeight();
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

ui::Color App::InteractiveFill(std::string_view node_id, ui::Color resting,
                               ui::Color hovered, ui::Color pressed) const {
  if (node_id.empty()) return resting;
  const bool is_hovered = hovered_node_id_ == node_id;
  if (is_hovered && pressed_node_id_ == node_id) return pressed;
  return is_hovered ? hovered : resting;
}

std::unique_ptr<ui::Node> App::BuildInterface() {
  auto root = std::make_unique<ui::Node>();
  YGNodeStyleSetFlexDirection(root->yoga_node(), YGFlexDirectionRow);
  root->style().background = palette_.window_background;
  const bool sidebar_fits_beside_settings =
      LogicalClientWidth() >= theme::kSidebarWidth +
                                  theme::kSettingsPanelWidth +
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

ui::Node* App::BuildIcon(ui::Node* parent, ui::IconKind icon, ui::Color color,
                         float size) {
  ui::Node* icon_node = parent->AddChild();
  YGNodeStyleSetWidth(icon_node->yoga_node(), size);
  YGNodeStyleSetHeight(icon_node->yoga_node(), size);
  YGNodeStyleSetFlexShrink(icon_node->yoga_node(), 0);
  icon_node->style().icon = icon;
  icon_node->style().icon_color = color;
  return icon_node;
}

ui::Node* App::BuildIconButton(ui::Node* parent, std::string id,
                               ui::IconKind icon, float size) {
  const bool hovered = hovered_node_id_ == id;
  ui::Node* button = parent->AddChild(id);
  YGNodeStyleSetWidth(button->yoga_node(), size);
  YGNodeStyleSetHeight(button->yoga_node(), size);
  YGNodeStyleSetFlexShrink(button->yoga_node(), 0);
  button->style().corner_radius = theme::kControlCornerRadius;
  button->style().cursor = CursorKind::kHand;
  button->style().background =
      InteractiveFill(id, theme::kTransparent, palette_.control_hover,
                      palette_.control_pressed);
  button->style().icon = icon;
  button->style().icon_color =
      hovered ? palette_.text_primary : palette_.text_secondary;
  return button;
}

ui::Node* App::BuildTextButton(ui::Node* parent, std::string id,
                               std::string label, ButtonStyle style,
                               bool enabled) {
  ui::Node* button = parent->AddChild(enabled ? id : std::string());
  MakeRow(button->yoga_node());
  YGNodeStyleSetJustifyContent(button->yoga_node(), YGJustifyCenter);
  YGNodeStyleSetFlexShrink(button->yoga_node(), 0);
  YGNodeStyleSetHeight(button->yoga_node(), 32);
  YGNodeStyleSetPadding(button->yoga_node(), YGEdgeHorizontal,
                        theme::Spacing(4));
  button->style().corner_radius = theme::kControlCornerRadius;
  button->style().cursor = enabled ? CursorKind::kHand : CursorKind::kArrow;
  const std::string_view interactive_id = enabled ? id : std::string_view();

  ui::Color text_color = palette_.text_primary;
  switch (style) {
    case ButtonStyle::kPrimary:
      button->style().background =
          enabled
              ? InteractiveFill(interactive_id, palette_.accent,
                                palette_.accent_hover, palette_.accent_pressed)
              : palette_.accent_disabled;
      text_color =
          enabled ? palette_.text_on_accent : palette_.text_on_accent_disabled;
      break;
    case ButtonStyle::kSecondary:
      button->style().background =
          InteractiveFill(interactive_id, palette_.control_background,
                          palette_.control_hover, palette_.control_pressed);
      button->style().border = palette_.separator;
      button->style().border_width = 1;
      text_color = enabled ? palette_.text_primary : palette_.text_tertiary;
      break;
    case ButtonStyle::kPlain:
      button->style().background =
          InteractiveFill(interactive_id, theme::kTransparent,
                          palette_.control_hover, palette_.control_pressed);
      text_color = enabled ? palette_.accent : palette_.text_tertiary;
      break;
  }
  BuildLabel(button, std::move(label), body_font_.get(), text_color);
  return button;
}

ui::Node* App::BuildSidebarRow(ui::Node* parent, std::string id,
                               std::string label, ui::IconKind leading_icon,
                               bool selected, std::string trailing_hint) {
  ui::Node* row = parent->AddChild(id);
  MakeRow(row->yoga_node());
  YGNodeStyleSetHeight(row->yoga_node(), theme::kListRowHeight);
  YGNodeStyleSetFlexShrink(row->yoga_node(), 0);
  YGNodeStyleSetGap(row->yoga_node(), YGGutterColumn, theme::Spacing(2));
  YGNodeStyleSetPadding(row->yoga_node(), YGEdgeHorizontal, theme::Spacing(2));
  row->style().corner_radius = theme::kControlCornerRadius;
  row->style().cursor = CursorKind::kHand;
  row->style().background =
      selected
          ? palette_.selected_background
          : InteractiveFill(id, theme::kTransparent, palette_.control_hover,
                            palette_.control_pressed);
  if (leading_icon != ui::IconKind::kNone) {
    BuildIcon(row, leading_icon, palette_.text_secondary, 16);
  }
  ui::Node* title = BuildLabel(row, std::move(label), body_font_.get(),
                               palette_.text_primary);
  YGNodeStyleSetFlexGrow(title->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(title->yoga_node(), 1);
  title->style().elides_text = true;
  if (!trailing_hint.empty()) {
    BuildLabel(row, std::move(trailing_hint), caption_font_.get(),
               palette_.text_tertiary);
  }
  return row;
}

void App::BuildSidebar(ui::Node* parent) {
  ui::Node* sidebar = parent->AddChild();
  YGNodeRef sidebar_layout = sidebar->yoga_node();
  YGNodeStyleSetWidth(sidebar_layout, theme::kSidebarWidth);
  YGNodeStyleSetFlexShrink(sidebar_layout, 0);
  YGNodeStyleSetPadding(sidebar_layout, YGEdgeHorizontal, theme::Spacing(3));
  YGNodeStyleSetPadding(sidebar_layout, YGEdgeBottom, theme::Spacing(3));
  YGNodeStyleSetGap(sidebar_layout, YGGutterRow, theme::Spacing(0.5f));
  sidebar->style().background = palette_.sidebar_background;

  ui::Node* brand = sidebar->AddChild();
  MakeRow(brand->yoga_node());
  YGNodeStyleSetHeight(brand->yoga_node(), theme::kTopBarHeight);
  YGNodeStyleSetFlexShrink(brand->yoga_node(), 0);
  YGNodeStyleSetPadding(brand->yoga_node(), YGEdgeHorizontal,
                        theme::Spacing(2));
  BuildLabel(brand, "Lale", headline_font_.get(), palette_.text_primary);

  BuildSidebarRow(sidebar, "new-chat", "New chat", ui::IconKind::kPlus, false,
                  "Ctrl+N");

  ui::Node* section_caption = sidebar->AddChild();
  YGNodeStyleSetMargin(section_caption->yoga_node(), YGEdgeTop,
                       theme::Spacing(5));
  YGNodeStyleSetMargin(section_caption->yoga_node(), YGEdgeBottom,
                       theme::Spacing(1));
  YGNodeStyleSetPadding(section_caption->yoga_node(), YGEdgeHorizontal,
                        theme::Spacing(2));
  BuildLabel(section_caption, "Chats", caption_strong_font_.get(),
             palette_.text_tertiary);

  ui::Node* conversation_list = sidebar->AddChild();
  YGNodeStyleSetFlexGrow(conversation_list->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(conversation_list->yoga_node(), 1);
  YGNodeStyleSetGap(conversation_list->yoga_node(), YGGutterRow,
                    theme::Spacing(0.5f));
  for (size_t index = 0; index < conversations_.size(); ++index) {
    BuildSidebarRow(conversation_list,
                    std::string(kConversationIdPrefix) + std::to_string(index),
                    conversations_[index].title, ui::IconKind::kNone,
                    index == active_conversation_index_, {});
  }

  BuildSidebarRow(sidebar, "settings-toggle", "Settings", ui::IconKind::kNone,
                  settings_panel_open_, "Ctrl+,");
}

void App::BuildChatArea(ui::Node* parent) {
  ui::Node* chat_area = parent->AddChild();
  YGNodeStyleSetFlexGrow(chat_area->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(chat_area->yoga_node(), 1);
  YGNodeStyleSetMinWidth(chat_area->yoga_node(), 0);
  BuildTopBar(chat_area);
  AddHairline(chat_area, message_scroll_offset_ > 0.5f ? palette_.separator
                                                       : theme::kTransparent);
  BuildMessageList(chat_area);
  BuildComposer(chat_area);
}

void App::BuildTopBar(ui::Node* parent) {
  ui::Node* top_bar = parent->AddChild();
  MakeRow(top_bar->yoga_node());
  YGNodeStyleSetHeight(top_bar->yoga_node(), theme::kTopBarHeight);
  YGNodeStyleSetFlexShrink(top_bar->yoga_node(), 0);
  YGNodeStyleSetJustifyContent(top_bar->yoga_node(), YGJustifySpaceBetween);
  YGNodeStyleSetGap(top_bar->yoga_node(), YGGutterColumn, theme::Spacing(3));
  YGNodeStyleSetPadding(top_bar->yoga_node(), YGEdgeLeft, theme::Spacing(5));
  YGNodeStyleSetPadding(top_bar->yoga_node(), YGEdgeRight, theme::Spacing(3));

  ui::Node* title =
      BuildLabel(top_bar, conversations_[active_conversation_index_].title,
                 headline_font_.get(), palette_.text_primary);
  YGNodeStyleSetFlexShrink(title->yoga_node(), 1);
  title->style().elides_text = true;

  const std::string model_picker_id = "model-picker";
  ui::Node* model_picker = top_bar->AddChild(model_picker_id);
  MakeRow(model_picker->yoga_node());
  YGNodeStyleSetHeight(model_picker->yoga_node(), 32);
  YGNodeStyleSetFlexShrink(model_picker->yoga_node(), 0);
  YGNodeStyleSetGap(model_picker->yoga_node(), YGGutterColumn,
                    theme::Spacing(1.5f));
  YGNodeStyleSetPadding(model_picker->yoga_node(), YGEdgeHorizontal,
                        theme::Spacing(2.5f));
  model_picker->style().corner_radius = theme::kControlCornerRadius;
  model_picker->style().cursor = CursorKind::kHand;
  model_picker->style().background =
      InteractiveFill(model_picker_id, theme::kTransparent,
                      palette_.control_hover, palette_.control_pressed);
  BuildLabel(model_picker,
             generation_settings_.model_path.empty()
                 ? "No model"
                 : FileNameFromPath(generation_settings_.model_path),
             body_font_.get(), palette_.text_secondary);
  BuildIcon(model_picker, ui::IconKind::kChevronDown, palette_.text_secondary,
            12);
}

void App::BuildMessageList(ui::Node* parent) {
  ui::Node* viewport = parent->AddChild(std::string(kMessageViewportId));
  MakeScrollViewport(viewport, message_scroll_offset_);

  const Conversation& conversation = conversations_[active_conversation_index_];
  ui::Node* content = viewport->AddChild();
  YGNodeStyleSetFlexShrink(content->yoga_node(), 0);
  YGNodeStyleSetFlexGrow(content->yoga_node(), 1);
  YGNodeStyleSetAlignItems(content->yoga_node(), YGAlignCenter);
  YGNodeStyleSetPadding(content->yoga_node(), YGEdgeVertical,
                        theme::Spacing(6));
  if (conversation.messages.empty()) {
    YGNodeStyleSetJustifyContent(content->yoga_node(), YGJustifyCenter);
    BuildEmptyState(content);
    return;
  }

  ui::Node* column = content->AddChild();
  YGNodeStyleSetWidthPercent(column->yoga_node(), 100);
  YGNodeStyleSetMaxWidth(column->yoga_node(),
                         theme::kMessageColumnMaximumWidth);
  YGNodeStyleSetGap(column->yoga_node(), YGGutterRow, theme::Spacing(6));
  YGNodeStyleSetPadding(column->yoga_node(), YGEdgeHorizontal,
                        theme::Spacing(6));

  for (const ChatMessage& message : conversation.messages) {
    if (message.role == MessageRole::kUser) {
      ui::Node* row = column->AddChild();
      YGNodeStyleSetFlexDirection(row->yoga_node(), YGFlexDirectionRow);
      YGNodeStyleSetJustifyContent(row->yoga_node(), YGJustifyFlexEnd);
      ui::Node* bubble = row->AddChild();
      YGNodeStyleSetMaxWidthPercent(bubble->yoga_node(), 80);
      YGNodeStyleSetFlexShrink(bubble->yoga_node(), 1);
      SetPadding(bubble->yoga_node(), theme::Spacing(4), theme::Spacing(2.5f));
      bubble->style().corner_radius = theme::kBubbleCornerRadius;
      bubble->style().background = palette_.accent;
      BuildLabel(bubble, message.text, body_font_.get(),
                 palette_.text_on_accent, true);
    } else {
      BuildLabel(
          column, message.text, body_font_.get(),
          message.is_notice ? palette_.text_secondary : palette_.text_primary,
          true);
    }
  }
}

void App::BuildEmptyState(ui::Node* parent) {
  ui::Node* empty_state = parent->AddChild();
  YGNodeStyleSetAlignItems(empty_state->yoga_node(), YGAlignCenter);
  YGNodeStyleSetGap(empty_state->yoga_node(), YGGutterRow, theme::Spacing(2));
  YGNodeStyleSetPadding(empty_state->yoga_node(), YGEdgeHorizontal,
                        theme::Spacing(6));
  const bool model_loaded = !generation_settings_.model_path.empty();
  BuildLabel(
      empty_state,
      model_loaded ? "What would you like to ask?" : "Choose a model to begin",
      title_font_.get(), palette_.text_primary);
  ui::Node* description_spacer = empty_state->AddChild();
  YGNodeStyleSetHeight(description_spacer->yoga_node(), theme::Spacing(1));
  BuildLabel(empty_state, "Lale runs llama.cpp models locally on this PC.",
             body_font_.get(), palette_.text_secondary);
  BuildLabel(empty_state, "Nothing you type leaves your computer.",
             body_font_.get(), palette_.text_secondary);
  if (!model_loaded) {
    ui::Node* button_spacer = empty_state->AddChild();
    YGNodeStyleSetHeight(button_spacer->yoga_node(), theme::Spacing(3));
    BuildTextButton(empty_state, "empty-open-settings", "Open Settings",
                    ButtonStyle::kPrimary);
  }
}

void App::BuildComposer(ui::Node* parent) {
  const bool composer_focused = focused_field_id_ == kComposerFieldId;
  ui::Node* dock = parent->AddChild();
  YGNodeStyleSetFlexShrink(dock->yoga_node(), 0);
  YGNodeStyleSetAlignItems(dock->yoga_node(), YGAlignCenter);
  YGNodeStyleSetPadding(dock->yoga_node(), YGEdgeHorizontal, theme::Spacing(6));
  YGNodeStyleSetPadding(dock->yoga_node(), YGEdgeTop, theme::Spacing(2));
  YGNodeStyleSetPadding(dock->yoga_node(), YGEdgeBottom, theme::Spacing(5));

  ui::Node* field_box = dock->AddChild(std::string(kComposerFieldId));
  YGNodeStyleSetFlexDirection(field_box->yoga_node(), YGFlexDirectionRow);
  YGNodeStyleSetAlignItems(field_box->yoga_node(), YGAlignFlexEnd);
  YGNodeStyleSetWidthPercent(field_box->yoga_node(), 100);
  YGNodeStyleSetMaxWidth(
      field_box->yoga_node(),
      theme::kMessageColumnMaximumWidth - 2 * theme::Spacing(6));
  YGNodeStyleSetGap(field_box->yoga_node(), YGGutterColumn, theme::Spacing(2));
  YGNodeStyleSetPadding(field_box->yoga_node(), YGEdgeLeft, theme::Spacing(4));
  YGNodeStyleSetPadding(field_box->yoga_node(), YGEdgeRight, theme::Spacing(2));
  YGNodeStyleSetPadding(field_box->yoga_node(), YGEdgeVertical,
                        theme::Spacing(2));
  field_box->style().corner_radius = theme::kComposerCornerRadius;
  field_box->style().background = palette_.card_background;
  field_box->style().border =
      composer_focused ? palette_.accent : palette_.separator;
  field_box->style().border_width = 1;
  field_box->style().cursor = CursorKind::kIBeam;

  ui::Node* field_text = BuildLabel(field_box, composer_text_, body_font_.get(),
                                    palette_.text_primary, true);
  YGNodeStyleSetFlexGrow(field_text->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(field_text->yoga_node(), 1);
  YGNodeStyleSetMaxHeight(field_text->yoga_node(),
                          theme::kComposerMaximumTextHeight);
  YGNodeStyleSetMargin(field_text->yoga_node(), YGEdgeBottom,
                       theme::Spacing(1.5f));
  field_text->style().placeholder = "Message Lale";
  field_text->style().placeholder_color = palette_.text_tertiary;
  field_text->style().shows_caret = composer_focused;

  const bool can_send = !TrimWhitespace(composer_text_).empty();
  const std::string send_id = can_send ? "send" : std::string();
  ui::Node* send_button = field_box->AddChild(send_id);
  YGNodeStyleSetWidth(send_button->yoga_node(), 32);
  YGNodeStyleSetHeight(send_button->yoga_node(), 32);
  YGNodeStyleSetFlexShrink(send_button->yoga_node(), 0);
  send_button->style().corner_radius = 16;
  send_button->style().cursor =
      can_send ? CursorKind::kHand : CursorKind::kIBeam;
  send_button->style().background =
      can_send ? InteractiveFill(send_id, palette_.accent,
                                 palette_.accent_hover, palette_.accent_pressed)
               : palette_.accent_disabled;
  send_button->style().icon = ui::IconKind::kArrowUp;
  send_button->style().icon_color =
      can_send ? palette_.text_on_accent : palette_.text_on_accent_disabled;
}

ui::Node* App::BuildSettingsSection(ui::Node* parent, std::string caption) {
  ui::Node* caption_box = parent->AddChild();
  YGNodeStyleSetMargin(caption_box->yoga_node(), YGEdgeTop, theme::Spacing(5));
  YGNodeStyleSetMargin(caption_box->yoga_node(), YGEdgeBottom,
                       theme::Spacing(1.5f));
  YGNodeStyleSetPadding(caption_box->yoga_node(), YGEdgeHorizontal,
                        theme::Spacing(1));
  BuildLabel(caption_box, std::move(caption), caption_strong_font_.get(),
             palette_.text_tertiary);

  ui::Node* card = parent->AddChild();
  card->style().corner_radius = theme::kCardCornerRadius;
  card->style().background = palette_.card_background;
  if (!palette_.is_dark) {
    card->style().border = palette_.separator;
    card->style().border_width = 1;
  }
  return card;
}

void App::BuildSettingsRowSeparator(ui::Node* card) {
  ui::Node* separator = AddHairline(card, palette_.separator);
  YGNodeStyleSetMargin(separator->yoga_node(), YGEdgeLeft,
                       theme::Spacing(3.5f));
}

void App::BuildSettingsPanel(ui::Node* parent) {
  ui::Node* panel_with_edge = parent->AddChild();
  YGNodeStyleSetFlexDirection(panel_with_edge->yoga_node(), YGFlexDirectionRow);
  YGNodeStyleSetFlexShrink(panel_with_edge->yoga_node(), 0);
  ui::Node* edge = panel_with_edge->AddChild();
  YGNodeStyleSetWidth(edge->yoga_node(), 1);
  edge->style().background = palette_.separator;

  ui::Node* panel = panel_with_edge->AddChild();
  YGNodeStyleSetWidth(panel->yoga_node(), theme::kSettingsPanelWidth - 1);
  panel->style().background = palette_.sidebar_background;

  ui::Node* header = panel->AddChild();
  MakeRow(header->yoga_node());
  YGNodeStyleSetHeight(header->yoga_node(), theme::kTopBarHeight);
  YGNodeStyleSetFlexShrink(header->yoga_node(), 0);
  YGNodeStyleSetJustifyContent(header->yoga_node(), YGJustifySpaceBetween);
  YGNodeStyleSetPadding(header->yoga_node(), YGEdgeLeft, theme::Spacing(5));
  YGNodeStyleSetPadding(header->yoga_node(), YGEdgeRight, theme::Spacing(3));
  BuildLabel(header, "Settings", headline_font_.get(), palette_.text_primary);
  BuildIconButton(header, "settings-close", ui::IconKind::kClose, 28);

  ui::Node* viewport = panel->AddChild(std::string(kSettingsViewportId));
  MakeScrollViewport(viewport, settings_scroll_offset_);
  ui::Node* content = viewport->AddChild();
  YGNodeStyleSetFlexShrink(content->yoga_node(), 0);
  YGNodeStyleSetPadding(content->yoga_node(), YGEdgeHorizontal,
                        theme::Spacing(4));
  YGNodeStyleSetPadding(content->yoga_node(), YGEdgeBottom, theme::Spacing(6));

  ui::Node* model_card = BuildSettingsSection(content, "Model");
  ui::Node* model_row = model_card->AddChild();
  YGNodeStyleSetGap(model_row->yoga_node(), YGGutterRow, theme::Spacing(1));
  SetPadding(model_row->yoga_node(), theme::Spacing(3.5f), theme::Spacing(3));
  BuildLabel(model_row,
             generation_settings_.model_path.empty()
                 ? "No model loaded"
                 : FileNameFromPath(generation_settings_.model_path),
             body_font_.get(), palette_.text_primary, true);
  BuildLabel(model_row, "Model loading arrives with the llama.cpp backend.",
             caption_font_.get(), palette_.text_secondary, true);

  ui::Node* sampling_card = BuildSettingsSection(content, "Sampling");
  BuildStepperRow(sampling_card, "Temperature", "temperature",
                  FormatDecimal(generation_settings_.temperature));
  BuildSettingsRowSeparator(sampling_card);
  BuildStepperRow(sampling_card, "Top P", "top-p",
                  FormatDecimal(generation_settings_.top_p));
  BuildSettingsRowSeparator(sampling_card);
  BuildStepperRow(sampling_card, "Top K", "top-k",
                  std::to_string(generation_settings_.top_k));
  BuildSettingsRowSeparator(sampling_card);
  BuildStepperRow(sampling_card, "Max tokens", "max-tokens",
                  std::to_string(generation_settings_.maximum_tokens));
  BuildSettingsRowSeparator(sampling_card);
  BuildStepperRow(sampling_card, "Context size", "context-size",
                  std::to_string(generation_settings_.context_size));

  ui::Node* prompt_card = BuildSettingsSection(content, "System prompt");
  const bool system_prompt_focused = focused_field_id_ == kSystemPromptFieldId;
  ui::Node* prompt_field =
      prompt_card->AddChild(std::string(kSystemPromptFieldId));
  YGNodeStyleSetMinHeight(prompt_field->yoga_node(), 96);
  SetPadding(prompt_field->yoga_node(), theme::Spacing(3.5f),
             theme::Spacing(3));
  prompt_field->style().corner_radius = theme::kCardCornerRadius;
  prompt_field->style().cursor = CursorKind::kIBeam;
  if (system_prompt_focused) {
    prompt_field->style().border = palette_.accent;
    prompt_field->style().border_width = 1;
  }
  ui::Node* prompt_text =
      BuildLabel(prompt_field, generation_settings_.system_prompt,
                 body_font_.get(), palette_.text_primary, true);
  prompt_text->style().placeholder = "No system prompt";
  prompt_text->style().placeholder_color = palette_.text_tertiary;
  prompt_text->style().shows_caret = system_prompt_focused;

  ui::Node* output_card = BuildSettingsSection(content, "Output");
  BuildToggleRow(output_card, "Stream tokens", "stream",
                 generation_settings_.stream_tokens);

  ui::Node* reset_row = content->AddChild();
  YGNodeStyleSetMargin(reset_row->yoga_node(), YGEdgeTop, theme::Spacing(4));
  YGNodeStyleSetAlignItems(reset_row->yoga_node(), YGAlignFlexStart);
  BuildTextButton(reset_row, "reset-settings", "Reset to defaults",
                  ButtonStyle::kPlain, !SettingsMatchDefaults());
}

void App::BuildStepperRow(ui::Node* card, std::string_view label,
                          std::string_view setting_key,
                          std::string value_text) {
  ui::Node* row = card->AddChild();
  MakeRow(row->yoga_node());
  YGNodeStyleSetHeight(row->yoga_node(), theme::kSettingsRowHeight);
  YGNodeStyleSetJustifyContent(row->yoga_node(), YGJustifySpaceBetween);
  YGNodeStyleSetPadding(row->yoga_node(), YGEdgeLeft, theme::Spacing(3.5f));
  YGNodeStyleSetPadding(row->yoga_node(), YGEdgeRight, theme::Spacing(2));
  BuildLabel(row, std::string(label), body_font_.get(), palette_.text_primary);

  ui::Node* stepper = row->AddChild();
  MakeRow(stepper->yoga_node());
  stepper->style().corner_radius = theme::kControlCornerRadius;
  stepper->style().background =
      palette_.is_dark ? palette_.control_hover : palette_.window_background;

  std::string id_prefix =
      std::string(kStepperIdPrefix) + std::string(setting_key) + ":";
  BuildIconButton(stepper, id_prefix + "decrease", ui::IconKind::kMinus, 28);
  ui::Node* value_box = stepper->AddChild();
  YGNodeStyleSetWidth(value_box->yoga_node(), 56);
  YGNodeStyleSetAlignItems(value_box->yoga_node(), YGAlignCenter);
  BuildLabel(value_box, std::move(value_text), body_font_.get(),
             palette_.text_primary);
  BuildIconButton(stepper, id_prefix + "increase", ui::IconKind::kPlus, 28);
}

void App::BuildToggleRow(ui::Node* card, std::string_view label,
                         std::string_view setting_key, bool enabled) {
  const std::string toggle_id =
      std::string(kToggleIdPrefix) + std::string(setting_key);
  ui::Node* row = card->AddChild(toggle_id);
  MakeRow(row->yoga_node());
  YGNodeStyleSetHeight(row->yoga_node(), theme::kSettingsRowHeight);
  YGNodeStyleSetJustifyContent(row->yoga_node(), YGJustifySpaceBetween);
  YGNodeStyleSetPadding(row->yoga_node(), YGEdgeHorizontal,
                        theme::Spacing(3.5f));
  row->style().cursor = CursorKind::kHand;
  BuildLabel(row, std::string(label), body_font_.get(), palette_.text_primary);

  ui::Node* track = row->AddChild();
  YGNodeStyleSetFlexDirection(track->yoga_node(), YGFlexDirectionRow);
  YGNodeStyleSetAlignItems(track->yoga_node(), YGAlignCenter);
  YGNodeStyleSetWidth(track->yoga_node(), 40);
  YGNodeStyleSetHeight(track->yoga_node(), 22);
  YGNodeStyleSetPadding(track->yoga_node(), YGEdgeAll, 2);
  YGNodeStyleSetJustifyContent(track->yoga_node(),
                               enabled ? YGJustifyFlexEnd : YGJustifyFlexStart);
  track->style().corner_radius = 11;
  track->style().background = enabled ? palette_.accent : palette_.toggle_off;

  ui::Node* knob = track->AddChild();
  YGNodeStyleSetWidth(knob->yoga_node(), 18);
  YGNodeStyleSetHeight(knob->yoga_node(), 18);
  knob->style().corner_radius = 9;
  knob->style().background = palette_.toggle_knob;
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
  } else if (node_id == "model-picker" || node_id == "empty-open-settings") {
    settings_panel_open_ = true;
    settings_scroll_offset_ = 0;
  } else if (node_id == "send") {
    SendComposerMessage();
  } else if (node_id == "reset-settings") {
    ResetSettingsToDefaults();
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

bool App::SettingsMatchDefaults() const {
  const GenerationSettings defaults;
  const GenerationSettings& current = generation_settings_;
  return current.temperature == defaults.temperature &&
         current.top_p == defaults.top_p && current.top_k == defaults.top_k &&
         current.maximum_tokens == defaults.maximum_tokens &&
         current.context_size == defaults.context_size &&
         current.stream_tokens == defaults.stream_tokens &&
         current.system_prompt == defaults.system_prompt;
}

void App::ResetSettingsToDefaults() {
  std::string kept_model_path = generation_settings_.model_path;
  generation_settings_ = GenerationSettings();
  generation_settings_.model_path = std::move(kept_model_path);
}

void App::SendComposerMessage() {
  std::string message_text = TrimWhitespace(composer_text_);
  if (message_text.empty()) return;
  Conversation& conversation = conversations_[active_conversation_index_];
  if (conversation.messages.empty() &&
      conversation.title == kNewConversationTitle) {
    conversation.title = message_text.substr(0, message_text.find('\n'));
  }
  conversation.messages.push_back({MessageRole::kUser, message_text, false});
  conversation.messages.push_back(
      {MessageRole::kAssistant, std::string(kNoModelNotice), true});
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

void App::OnDpiChanged(UINT dots_per_inch) {
  device_pixels_per_point_ = static_cast<float>(dots_per_inch) /
                             static_cast<float>(USER_DEFAULT_SCREEN_DPI);
  ui::SetLayoutPointScale(device_pixels_per_point_);
  Invalidate();
}

void App::OnSystemAppearanceChanged() { ApplySystemAppearance(); }

void App::OnPaint() { RenderFrame(); }

void App::OnMouseMove(float x, float y) {
  const ui::Node* hit = HitTestDevicePoint(x, y);
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
  const ui::Node* hit = HitTestDevicePoint(x, y);
  std::string hit_id = hit ? hit->id() : std::string();
  if (IsTextField(hit_id)) {
    focused_field_id_ = hit_id;
  } else if (hit_id != "send") {
    focused_field_id_.clear();
  }
  pressed_node_id_ = IsTextField(hit_id) ? std::string() : hit_id;
  hovered_node_id_ = hit_id;
  ResetCaretBlink();
}

void App::OnMouseUp(float x, float y) {
  const ui::Node* hit = HitTestDevicePoint(x, y);
  std::string released_id = hit ? hit->id() : std::string();
  std::string pressed_id = std::move(pressed_node_id_);
  pressed_node_id_.clear();
  if (!pressed_id.empty() && pressed_id == released_id) {
    HandleClick(pressed_id);
  }
  Invalidate();
}

void App::OnMouseWheel(float x, float y, float wheel_steps) {
  if (!interface_root_) return;
  const float logical_x = x / device_pixels_per_point_;
  const float logical_y = y / device_pixels_per_point_;
  float scroll_delta = wheel_steps * theme::kWheelScrollDistance;
  ui::Node* settings_viewport = interface_root_->FindById(kSettingsViewportId);
  if (settings_viewport &&
      settings_viewport->bounds().Contains(logical_x, logical_y)) {
    settings_scroll_offset_ -= scroll_delta;
    Invalidate();
    return;
  }
  ui::Node* message_viewport = interface_root_->FindById(kMessageViewportId);
  if (message_viewport &&
      message_viewport->bounds().Contains(logical_x, logical_y)) {
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
  if (control_down && virtual_key == 'N') {
    StartNewConversation();
    ResetCaretBlink();
    return;
  }
  if (control_down && virtual_key == VK_OEM_COMMA) {
    settings_panel_open_ = !settings_panel_open_;
    Invalidate();
    return;
  }
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
  const ui::Node* hit = HitTestDevicePoint(x, y);
  return hit ? hit->style().cursor : CursorKind::kArrow;
}

}
