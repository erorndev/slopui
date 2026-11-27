#include "app/App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <initializer_list>
#include <thread>

#include "platform/FileDialog.h"
#include "platform/SystemAppearance.h"

namespace lale {

namespace {

constexpr std::string_view kComposerFieldId = "composer";
constexpr std::string_view kSystemPromptFieldId = "system-prompt";
constexpr std::string_view kRenameFieldId = "rename-field";
constexpr std::string_view kRecentModelIdPrefix = "recent-model:";
constexpr std::string_view kCopyMessageIdPrefix = "copy-message:";
constexpr std::string_view kCopyCodeIdPrefix = "copy-code:";
constexpr std::string_view kSelectableIdPrefix = "select:";
constexpr std::string_view kMessageViewportId = "message-viewport";
constexpr std::string_view kSettingsViewportId = "settings-viewport";
constexpr std::string_view kTextNodeSuffix = "#text";
constexpr std::string_view kConversationIdPrefix = "conversation:";
constexpr std::string_view kStepperIdPrefix = "stepper:";
constexpr std::string_view kToggleIdPrefix = "toggle:";
constexpr std::string_view kMinimizeButtonId = "window:minimize";
constexpr std::string_view kMaximizeButtonId = "window:maximize";
constexpr std::string_view kCloseButtonId = "window:close";
constexpr std::string_view kSettingsSectionIdPrefix = "settings-section:";
constexpr std::string_view kThemeIdPrefix = "theme:";
constexpr std::string_view kNewConversationTitle = "New chat";
constexpr std::string_view kNoModelNotice =
    "No model is loaded, so there is no reply. Choose a GGUF model in "
    "Settings.";
constexpr std::string_view kLoadingNotice =
    "The model is still loading, so there is no reply. Send your message "
    "again in a moment.";

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

ui::Node* AddHorizontalRule(ui::Node* parent, ui::Color color) {
  ui::Node* rule = parent->AddChild();
  YGNodeStyleSetHeight(rule->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(rule->yoga_node(), 0);
  rule->style().background = color;
  return rule;
}

ui::Node* AddVerticalRule(ui::Node* parent, ui::Color color) {
  ui::Node* rule = parent->AddChild();
  YGNodeStyleSetWidth(rule->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(rule->yoga_node(), 0);
  rule->style().background = color;
  return rule;
}

void AddVerticalSpace(ui::Node* parent, float height) {
  ui::Node* space = parent->AddChild();
  YGNodeStyleSetHeight(space->yoga_node(), height);
  YGNodeStyleSetFlexShrink(space->yoga_node(), 0);
}

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
}

std::string FormatDecimal(float value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.2f", value);
  return buffer;
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

std::string BundledFontPath(const char* file_name) {
  char executable_path[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, executable_path, MAX_PATH);
  std::string directory(executable_path);
  directory.resize(directory.find_last_of("/\\") + 1);
  return directory + "fonts\\" + file_name;
}

std::string LoadFirstAvailableFont(
    std::initializer_list<const char*> file_names) {
  for (const char* file_name : file_names) {
    std::string font_key = ui::LoadFontFile(BundledFontPath(file_name));
    if (font_key.empty()) {
      font_key = ui::LoadFontFile(WindowsFontPath(file_name));
    }
    if (!font_key.empty()) return font_key;
  }
  return {};
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

constexpr float kContextWarningFraction = 0.8f;

std::string FormatWithThousands(int value) {
  std::string digits = std::to_string(value);
  for (int position = static_cast<int>(digits.size()) - 3; position > 0;
       position -= 3) {
    digits.insert(static_cast<size_t>(position), ",");
  }
  return digits;
}

std::string FileNameFromPath(const std::string& path) {
  size_t separator = path.find_last_of("/\\");
  return separator == std::string::npos ? path : path.substr(separator + 1);
}

}

App::App()
    : settings_file_path_(SettingsFilePath()),
      conversations_file_path_(ConversationsFilePath()) {
  conversations_.push_back({std::string(kNewConversationTitle), {}});
  for (Conversation& stored : LoadConversations(conversations_file_path_)) {
    conversations_.push_back(std::move(stored));
  }
  saved_settings_ = LoadStoredSettings(settings_file_path_);
  generation_settings_ = saved_settings_.generation;
  theme_preference_ = saved_settings_.theme;
  recent_model_paths_ = saved_settings_.recent_model_paths;
}

App::~App() {
  SaveSettingsIfChanged();
  SaveConversationHistory();
}

void App::SaveConversationHistory() {
  SaveConversations(conversations_, conversations_file_path_);
}

void App::SaveSettingsIfChanged() {
  StoredSettings current{generation_settings_, theme_preference_,
                         recent_model_paths_};
  if (current == saved_settings_) return;
  if (SaveStoredSettings(current, settings_file_path_)) {
    saved_settings_ = std::move(current);
  }
}

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
  inference_engine_ =
      std::make_unique<llm::InferenceEngine>([this] { window_.PostWake(); });
  RequestModelLoad();
  focused_field_id_ = std::string(kComposerFieldId);
  RenderFrame();
  window_.Show(show_command);
  return true;
}

llm::ModelOptions App::CurrentModelOptions() const {
  const GenerationSettings& settings = generation_settings_;
  llm::ModelOptions options;
  options.model_path = settings.model_path;
  options.gpu_layer_count =
      settings.gpu_layer_count >= GenerationSettings::kAllGpuLayers
          ? -1
          : settings.gpu_layer_count;
  options.thread_count = settings.thread_count;
  options.context_size = settings.context_size;
  return options;
}

bool App::ModelLoaded() const { return model_state_ == ModelState::kLoaded; }

void App::RememberRecentModel(const std::string& model_path) {
  if (model_path.empty()) return;
  std::erase(recent_model_paths_, model_path);
  recent_model_paths_.insert(recent_model_paths_.begin(), model_path);
  if (recent_model_paths_.size() > StoredSettings::kMaximumRecentModels) {
    recent_model_paths_.resize(StoredSettings::kMaximumRecentModels);
  }
  SaveSettingsIfChanged();
}

std::string App::ModelLoadingText() const {
  if (model_load_progress_ >= 1) return "Preparing the model…";
  return "Loading the model… " +
         std::to_string(static_cast<int>(model_load_progress_ * 100)) + "%";
}

void App::RequestModelLoad() {
  llm::ModelOptions options = CurrentModelOptions();
  const bool unchanged = options == requested_model_options_ &&
                         model_state_ != ModelState::kFailed;
  if (unchanged) return;
  StopGeneration();
  requested_model_options_ = options;
  ++pending_model_request_count_;
  model_error_.clear();
  if (options.model_path.empty()) {
    model_state_ = ModelState::kNone;
    inference_engine_->UnloadModel();
  } else {
    model_state_ = ModelState::kLoading;
    model_load_progress_ = 0;
    inference_engine_->LoadModel(options);
  }
  Invalidate();
}

void App::StopGeneration() {
  if (generating_) inference_engine_->CancelGeneration();
}

void App::OnWake() {
  for (const llm::EngineEvent& event : inference_engine_->TakeEvents()) {
    HandleEngineEvent(event);
  }
  Invalidate();
}

void App::HandleEngineEvent(const llm::EngineEvent& event) {
  using llm::EngineEventKind;
  switch (event.kind) {
    case EngineEventKind::kModelLoadProgress:
      if (pending_model_request_count_ == 1) {
        model_load_progress_ = event.progress;
      }
      return;
    case EngineEventKind::kModelLoaded:
    case EngineEventKind::kModelLoadFailed:
    case EngineEventKind::kModelUnloaded:
      if (--pending_model_request_count_ > 0) return;
      if (event.kind == EngineEventKind::kModelLoaded) {
        model_state_ = ModelState::kLoaded;
        RememberRecentModel(requested_model_options_.model_path);
      } else if (event.kind == EngineEventKind::kModelLoadFailed) {
        model_state_ = ModelState::kFailed;
        model_error_ = event.text;
      }
      return;
    case EngineEventKind::kPromptStarted:
    case EngineEventKind::kTokenText:
    case EngineEventKind::kGenerationFinished:
    case EngineEventKind::kGenerationFailed:
      break;
  }
  if (!generating_ || generating_conversation_index_ >= conversations_.size()) {
    return;
  }
  std::vector<ChatMessage>& messages =
      conversations_[generating_conversation_index_].messages;
  if (messages.empty() || !messages.back().is_streaming) return;
  ChatMessage& reply = messages.back();
  if (event.kind == EngineEventKind::kPromptStarted) {
    reply.prompt_token_count = event.token_count;
    return;
  }
  if (event.kind == EngineEventKind::kTokenText ||
      event.kind == EngineEventKind::kGenerationFinished) {
    reply.generated_token_count = event.token_count;
    reply.generation_seconds = event.seconds;
  }
  if (event.kind == EngineEventKind::kTokenText) {
    reply.text += event.text;
    return;
  }
  generating_ = false;
  reply.is_streaming = false;
  if (event.kind == EngineEventKind::kGenerationFailed) {
    reply.is_notice = true;
    reply.text = event.text;
  } else if (TrimWhitespace(reply.text).empty()) {
    messages.pop_back();
  } else {
    reply.text = TrimWhitespace(reply.text);
  }
  SaveConversationHistory();
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
      LoadFirstAvailableFont({"Inter-Regular.ttf", "segoeui.ttf"});
  std::string semibold_font_key = LoadFirstAvailableFont(
      {"Inter-SemiBold.ttf", "seguisb.ttf", "segoeui.ttf"});
  std::string monospace_font_key =
      LoadFirstAvailableFont({"CascadiaMono.ttf", "consola.ttf"});
  if (monospace_font_key.empty()) monospace_font_key = regular_font_key;
  std::string italic_font_key =
      LoadFirstAvailableFont({"Inter-Italic.ttf", "segoeuii.ttf"});
  if (italic_font_key.empty()) italic_font_key = regular_font_key;
  std::string semibold_italic_font_key =
      LoadFirstAvailableFont({"Inter-SemiBoldItalic.ttf", "seguisbi.ttf"});
  if (semibold_italic_font_key.empty()) {
    semibold_italic_font_key = semibold_font_key;
  }
  body_italic_font_ =
      std::make_unique<ui::Font>(italic_font_key, theme::kBodyPointSize);
  body_strong_italic_font_ = std::make_unique<ui::Font>(
      semibold_italic_font_key, theme::kBodyPointSize);
  large_heading_font_ = std::make_unique<ui::Font>(
      semibold_font_key, theme::kLargeHeadingPointSize);
  medium_heading_font_ = std::make_unique<ui::Font>(
      semibold_font_key, theme::kMediumHeadingPointSize);
  caption_font_ =
      std::make_unique<ui::Font>(regular_font_key, theme::kCaptionPointSize);
  caption_strong_font_ =
      std::make_unique<ui::Font>(semibold_font_key, theme::kCaptionPointSize);
  label_font_ =
      std::make_unique<ui::Font>(regular_font_key, theme::kLabelPointSize);
  label_strong_font_ =
      std::make_unique<ui::Font>(semibold_font_key, theme::kLabelPointSize);
  monospace_caption_font_ = std::make_unique<ui::Font>(
      monospace_font_key, theme::kMonospaceCaptionPointSize);
  monospace_body_font_ = std::make_unique<ui::Font>(
      monospace_font_key, theme::kMonospaceBodyPointSize);
  body_font_ =
      std::make_unique<ui::Font>(regular_font_key, theme::kBodyPointSize);
  body_strong_font_ =
      std::make_unique<ui::Font>(semibold_font_key, theme::kBodyPointSize);
  headline_font_ =
      std::make_unique<ui::Font>(semibold_font_key, theme::kHeadlinePointSize);
  title_font_ =
      std::make_unique<ui::Font>(semibold_font_key, theme::kTitlePointSize);
  display_font_ =
      std::make_unique<ui::Font>(semibold_font_key, theme::kDisplayPointSize);
}

void App::ApplySystemAppearance() {
  SystemAppearance appearance = ReadSystemAppearance();
  ApplyThemeOverrideFromEnvironment(&appearance);
  if (theme_preference_ != ThemePreference::kSystem) {
    appearance.prefers_dark_mode = theme_preference_ == ThemePreference::kDark;
  }
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
  const float measured_dock_height =
      composer_dock_node_
          ? YGNodeLayoutGetHeight(composer_dock_node_->yoga_node())
          : 0;
  if (std::abs(measured_dock_height - composer_dock_height_) > 0.5f) {
    composer_dock_height_ = measured_dock_height;
    interface_root_ = BuildInterface();
    LayoutInterface(interface_root_.get());
  }
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
  copyable_code_blocks_.clear();
  selectable_texts_.clear();
  composer_dock_node_ = nullptr;
  auto root = std::make_unique<ui::Node>();
  root->style().background = palette_.background_primary;
  BuildTitleBar(root.get());

  ui::Node* body = root->AddChild();
  YGNodeStyleSetFlexDirection(body->yoga_node(), YGFlexDirectionRow);
  YGNodeStyleSetFlexGrow(body->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(body->yoga_node(), 1);
  YGNodeStyleSetMinHeight(body->yoga_node(), 0);
  if (settings_page_open_) {
    BuildSettingsNavigation(body);
    AddVerticalRule(body, palette_.border);
    BuildSettingsPage(body);
  } else {
    BuildSidebar(body);
    AddVerticalRule(body, palette_.border);
    BuildChatArea(body);
  }
  return root;
}

void App::BuildTitleBar(ui::Node* parent) {
  ui::Node* title_bar = parent->AddChild();
  MakeRow(title_bar->yoga_node());
  YGNodeStyleSetHeight(title_bar->yoga_node(), theme::kTitleBarHeight);
  YGNodeStyleSetFlexShrink(title_bar->yoga_node(), 0);
  YGNodeStyleSetAlignItems(title_bar->yoga_node(), YGAlignStretch);
  title_bar->style().background = palette_.background_secondary;

  ui::Node* brand = title_bar->AddChild();
  MakeRow(brand->yoga_node());
  YGNodeStyleSetFlexGrow(brand->yoga_node(), 1);
  YGNodeStyleSetGap(brand->yoga_node(), YGGutterColumn, theme::Spacing(2));
  YGNodeStyleSetPadding(brand->yoga_node(), YGEdgeLeft, theme::Spacing(4));
  const ui::Color brand_color =
      window_active_ ? palette_.text_primary : palette_.text_tertiary;
  BuildIcon(brand, ui::IconKind::kTulip,
            window_active_ ? palette_.high_contrast : palette_.text_tertiary,
            18);
  BuildLabel(brand, "Lale", label_strong_font_.get(), brand_color);

  BuildCaptionButton(title_bar, std::string(kMinimizeButtonId),
                     ui::IconKind::kWindowMinimize);
  BuildCaptionButton(title_bar, std::string(kMaximizeButtonId),
                     window_.is_maximized() ? ui::IconKind::kWindowRestore
                                            : ui::IconKind::kWindowMaximize);
  BuildCaptionButton(title_bar, std::string(kCloseButtonId),
                     ui::IconKind::kWindowClose);
  AddHorizontalRule(parent, palette_.border);
}

void App::BuildCaptionButton(ui::Node* parent, std::string id,
                             ui::IconKind icon) {
  const bool is_close = id == kCloseButtonId;
  const bool hovered = hovered_node_id_ == id;
  ui::Node* button = parent->AddChild(id);
  YGNodeStyleSetWidth(button->yoga_node(), theme::kCaptionButtonWidth);
  YGNodeStyleSetFlexShrink(button->yoga_node(), 0);
  button->style().background =
      is_close
          ? InteractiveFill(id, theme::kTransparent,
                            palette_.close_button_hover,
                            palette_.close_button_pressed)
          : InteractiveFill(id, theme::kTransparent, palette_.overlay_hover,
                            palette_.overlay_pressed);
  button->style().icon = icon;
  if (is_close && hovered) {
    button->style().icon_color = palette_.text_on_close_button;
  } else {
    button->style().icon_color = window_active_ || hovered
                                     ? palette_.text_primary
                                     : palette_.text_tertiary;
  }
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

ui::Node* App::BuildEditableText(ui::Node* parent, std::string_view field_id,
                                 const std::string& text,
                                 const ui::TextSelection& selection,
                                 std::string placeholder) {
  const bool focused = focused_field_id_ == field_id;
  ui::Node* text_node =
      parent->AddChild(std::string(field_id) + std::string(kTextNodeSuffix));
  ui::NodeStyle& style = text_node->style();
  style.text = text;
  style.font = body_font_.get();
  style.text_color = palette_.text_primary;
  style.wraps_text = true;
  style.placeholder = std::move(placeholder);
  style.placeholder_color = palette_.text_tertiary;
  style.cursor = CursorKind::kIBeam;
  style.shows_caret = focused;
  style.caret_byte = std::min(selection.caret, text.size());
  if (focused) {
    style.selection_begin = std::min(selection.begin(), text.size());
    style.selection_end = std::min(selection.end(), text.size());
    style.selection_color = palette_.text_selection;
  }
  text_node->EnableTextMeasurement();
  return text_node;
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

ui::Node* App::BuildKeyHint(ui::Node* parent, std::string keys) {
  ui::Node* key_hint = parent->AddChild();
  MakeRow(key_hint->yoga_node());
  YGNodeStyleSetHeight(key_hint->yoga_node(), 20);
  YGNodeStyleSetFlexShrink(key_hint->yoga_node(), 0);
  YGNodeStyleSetPadding(key_hint->yoga_node(), YGEdgeHorizontal,
                        theme::Spacing(1.5f));
  key_hint->style().corner_radius = theme::kKeyHintCornerRadius;
  key_hint->style().background = palette_.background_primary;
  key_hint->style().border = palette_.border;
  key_hint->style().border_width = 1;
  BuildLabel(key_hint, std::move(keys), monospace_caption_font_.get(),
             palette_.text_secondary);
  return key_hint;
}

ui::Node* App::BuildStatusDot(ui::Node* parent, ui::Color color) {
  ui::Node* dot = parent->AddChild();
  YGNodeStyleSetWidth(dot->yoga_node(), 8);
  YGNodeStyleSetHeight(dot->yoga_node(), 8);
  YGNodeStyleSetFlexShrink(dot->yoga_node(), 0);
  dot->style().corner_radius = 4;
  dot->style().background = color;
  return dot;
}

int App::ContextTokensInUse() const {
  const std::vector<ChatMessage>& messages =
      conversations_[active_conversation_index_].messages;
  for (auto message = messages.rbegin(); message != messages.rend();
       ++message) {
    if (message->role != MessageRole::kAssistant || message->is_notice) {
      continue;
    }
    const int used =
        message->prompt_token_count + message->generated_token_count;
    if (used > 0) return used;
  }
  return 0;
}

void App::BuildContextMeter(ui::Node* parent) {
  const int used = ContextTokensInUse();
  const int capacity = requested_model_options_.context_size;
  if (used <= 0 || capacity <= 0) return;
  const float fraction =
      static_cast<float>(used) / static_cast<float>(capacity);
  const bool nearly_full = fraction >= kContextWarningFraction;
  ui::Node* meter = parent->AddChild();
  MakeRow(meter->yoga_node());
  YGNodeStyleSetGap(meter->yoga_node(), YGGutterColumn, theme::Spacing(2));
  ui::Node* bar_holder = meter->AddChild();
  YGNodeStyleSetWidth(bar_holder->yoga_node(), 48);
  BuildProgressBar(
      bar_holder, fraction, 4,
      nearly_full ? palette_.warning_indicator : palette_.text_secondary);
  BuildLabel(meter,
             FormatWithThousands(used) + " / " + FormatWithThousands(capacity),
             monospace_caption_font_.get(),
             nearly_full ? palette_.warning_text : palette_.text_secondary);
}

void App::BuildProgressBar(ui::Node* parent, float fraction, float height) {
  BuildProgressBar(parent, fraction, height, palette_.high_contrast);
}

void App::BuildProgressBar(ui::Node* parent, float fraction, float height,
                           ui::Color fill_color) {
  ui::Node* track = parent->AddChild();
  YGNodeStyleSetHeight(track->yoga_node(), height);
  YGNodeStyleSetFlexShrink(track->yoga_node(), 0);
  YGNodeStyleSetFlexDirection(track->yoga_node(), YGFlexDirectionRow);
  track->style().corner_radius = height / 2;
  track->style().background = palette_.border;
  ui::Node* fill = track->AddChild();
  YGNodeStyleSetWidthPercent(fill->yoga_node(),
                             std::clamp(fraction, 0.0f, 1.0f) * 100);
  fill->style().corner_radius = height / 2;
  fill->style().background = fill_color;
}

void App::BuildNotice(ui::Node* parent, const std::string& text) {
  ui::Node* notice = parent->AddChild();
  YGNodeStyleSetFlexDirection(notice->yoga_node(), YGFlexDirectionRow);
  YGNodeStyleSetAlignItems(notice->yoga_node(), YGAlignFlexStart);
  YGNodeStyleSetGap(notice->yoga_node(), YGGutterColumn, theme::Spacing(2.5f));
  SetPadding(notice->yoga_node(), theme::Spacing(3.5f), theme::Spacing(3));
  notice->style().corner_radius = theme::kBaseCornerRadius;
  notice->style().background = palette_.warning_background;
  notice->style().border = palette_.warning_border;
  notice->style().border_width = 1;
  ui::Node* dot = BuildStatusDot(notice, palette_.warning_indicator);
  YGNodeStyleSetMargin(dot->yoga_node(), YGEdgeTop, 6);
  ui::Node* notice_text =
      BuildLabel(notice, text, label_font_.get(), palette_.warning_text, true);
  YGNodeStyleSetFlexGrow(notice_text->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(notice_text->yoga_node(), 1);
}

ui::Node* App::BuildIconButton(ui::Node* parent, std::string id,
                               ui::IconKind icon, float size) {
  const bool hovered = hovered_node_id_ == id;
  ui::Node* button = parent->AddChild(id);
  YGNodeStyleSetWidth(button->yoga_node(), size);
  YGNodeStyleSetHeight(button->yoga_node(), size);
  YGNodeStyleSetFlexShrink(button->yoga_node(), 0);
  button->style().corner_radius = theme::kBaseCornerRadius;
  button->style().cursor = CursorKind::kHand;
  button->style().background =
      InteractiveFill(id, theme::kTransparent, palette_.overlay_hover,
                      palette_.overlay_pressed);
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
  YGNodeStyleSetHeight(button->yoga_node(), theme::kControlHeight);
  YGNodeStyleSetPadding(
      button->yoga_node(), YGEdgeHorizontal,
      style == ButtonStyle::kLink ? theme::Spacing(1) : theme::Spacing(3));
  button->style().corner_radius = theme::kBaseCornerRadius;
  button->style().cursor = enabled ? CursorKind::kHand : CursorKind::kArrow;
  const std::string_view interactive_id = enabled ? id : std::string_view();
  const bool hovered = enabled && hovered_node_id_ == id;

  ui::Color text_color = palette_.text_primary;
  const ui::Font* font = label_strong_font_.get();
  switch (style) {
    case ButtonStyle::kPrimary:
      button->style().background =
          enabled ? InteractiveFill(interactive_id, palette_.high_contrast,
                                    palette_.high_contrast_hover,
                                    palette_.high_contrast_hover)
                  : palette_.component_background;
      text_color =
          enabled ? palette_.text_on_high_contrast : palette_.text_tertiary;
      break;
    case ButtonStyle::kSecondary:
      button->style().background = InteractiveFill(
          interactive_id, palette_.background_primary,
          palette_.component_background, palette_.component_active);
      button->style().border =
          hovered ? palette_.border_hover : palette_.border;
      button->style().border_width = 1;
      button->style().shadows = palette_.shadow_small;
      text_color = enabled ? palette_.text_primary : palette_.text_tertiary;
      break;
    case ButtonStyle::kLink:
      font = label_font_.get();
      text_color = !enabled  ? palette_.text_tertiary
                   : hovered ? palette_.text_primary
                             : palette_.accent_text;
      break;
    case ButtonStyle::kQuiet:
      YGNodeStyleSetHeight(button->yoga_node(), 24);
      YGNodeStyleSetPadding(button->yoga_node(), YGEdgeHorizontal,
                            theme::Spacing(1.5f));
      font = caption_font_.get();
      button->style().background =
          InteractiveFill(interactive_id, theme::kTransparent,
                          palette_.overlay_hover, palette_.overlay_pressed);
      text_color = !enabled  ? palette_.text_tertiary
                   : hovered ? palette_.text_primary
                             : palette_.text_secondary;
      break;
  }
  BuildLabel(button, std::move(label), font, text_color);
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
  YGNodeStyleSetPadding(row->yoga_node(), YGEdgeLeft, theme::Spacing(2.5f));
  YGNodeStyleSetPadding(row->yoga_node(), YGEdgeRight, theme::Spacing(1.5f));
  row->style().corner_radius = theme::kBaseCornerRadius;
  row->style().cursor = CursorKind::kHand;
  row->style().background =
      selected
          ? palette_.overlay_selected
          : InteractiveFill(id, theme::kTransparent, palette_.overlay_hover,
                            palette_.overlay_pressed);
  const bool emphasized = selected || hovered_node_id_ == id;
  if (leading_icon != ui::IconKind::kNone) {
    BuildIcon(row, leading_icon, palette_.text_secondary, 16);
  }
  ui::Node* title =
      BuildLabel(row, std::move(label), label_font_.get(),
                 emphasized ? palette_.text_primary : palette_.text_secondary);
  YGNodeStyleSetFlexGrow(title->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(title->yoga_node(), 1);
  title->style().elides_text = true;
  if (!trailing_hint.empty()) BuildKeyHint(row, std::move(trailing_hint));
  return row;
}

void App::BuildSidebar(ui::Node* parent) {
  ui::Node* sidebar = parent->AddChild();
  YGNodeRef sidebar_layout = sidebar->yoga_node();
  YGNodeStyleSetWidth(sidebar_layout, theme::kSidebarWidth - 1);
  YGNodeStyleSetFlexShrink(sidebar_layout, 0);
  YGNodeStyleSetPadding(sidebar_layout, YGEdgeAll, theme::Spacing(3));
  YGNodeStyleSetGap(sidebar_layout, YGGutterRow, theme::Spacing(0.5f));
  sidebar->style().background = palette_.background_secondary;

  const std::string new_chat_id = "new-chat";
  ui::Node* new_chat = sidebar->AddChild(new_chat_id);
  MakeRow(new_chat->yoga_node());
  YGNodeStyleSetHeight(new_chat->yoga_node(), 36);
  YGNodeStyleSetFlexShrink(new_chat->yoga_node(), 0);
  YGNodeStyleSetGap(new_chat->yoga_node(), YGGutterColumn, theme::Spacing(2));
  YGNodeStyleSetPadding(new_chat->yoga_node(), YGEdgeLeft, theme::Spacing(3));
  YGNodeStyleSetPadding(new_chat->yoga_node(), YGEdgeRight, theme::Spacing(2));
  YGNodeStyleSetMargin(new_chat->yoga_node(), YGEdgeBottom, theme::Spacing(4));
  new_chat->style().corner_radius = theme::kBaseCornerRadius;
  new_chat->style().cursor = CursorKind::kHand;
  new_chat->style().background =
      InteractiveFill(new_chat_id, palette_.background_primary,
                      palette_.component_background, palette_.component_active);
  new_chat->style().border =
      hovered_node_id_ == new_chat_id ? palette_.border_hover : palette_.border;
  new_chat->style().border_width = 1;
  new_chat->style().shadows = palette_.shadow_small;
  BuildIcon(new_chat, ui::IconKind::kPlus, palette_.text_primary, 16);
  ui::Node* new_chat_label = BuildLabel(
      new_chat, "New chat", label_strong_font_.get(), palette_.text_primary);
  YGNodeStyleSetFlexGrow(new_chat_label->yoga_node(), 1);
  BuildKeyHint(new_chat, "Ctrl N");

  ui::Node* section_caption = sidebar->AddChild();
  YGNodeStyleSetMargin(section_caption->yoga_node(), YGEdgeBottom,
                       theme::Spacing(1));
  YGNodeStyleSetPadding(section_caption->yoga_node(), YGEdgeHorizontal,
                        theme::Spacing(2.5f));
  BuildLabel(section_caption, "Chats", caption_font_.get(),
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

  AddHorizontalRule(sidebar, palette_.border);
  AddVerticalSpace(sidebar, theme::Spacing(2));
  BuildSidebarRow(sidebar, "settings-toggle", "Settings", ui::IconKind::kNone,
                  false, "Ctrl ,");
}

void App::BuildChatArea(ui::Node* parent) {
  ui::Node* chat_area = parent->AddChild();
  YGNodeStyleSetFlexGrow(chat_area->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(chat_area->yoga_node(), 1);
  YGNodeStyleSetMinWidth(chat_area->yoga_node(), 0);
  BuildTopBar(chat_area);
  if (model_state_ == ModelState::kLoading) {
    BuildProgressBar(chat_area, model_load_progress_, 1);
  } else {
    AddHorizontalRule(chat_area, palette_.border);
  }
  BuildMessageList(chat_area);
  BuildComposer(chat_area);
}

void App::BuildTopBar(ui::Node* parent) {
  ui::Node* top_bar = parent->AddChild();
  MakeRow(top_bar->yoga_node());
  YGNodeStyleSetHeight(top_bar->yoga_node(), theme::kTopBarHeight - 1);
  YGNodeStyleSetFlexShrink(top_bar->yoga_node(), 0);
  YGNodeStyleSetJustifyContent(top_bar->yoga_node(), YGJustifySpaceBetween);
  YGNodeStyleSetGap(top_bar->yoga_node(), YGGutterColumn, theme::Spacing(3));
  YGNodeStyleSetPadding(top_bar->yoga_node(), YGEdgeLeft, theme::Spacing(6));
  YGNodeStyleSetPadding(top_bar->yoga_node(), YGEdgeRight, theme::Spacing(4));

  ui::Node* title_group = top_bar->AddChild();
  MakeRow(title_group->yoga_node());
  YGNodeStyleSetFlexShrink(title_group->yoga_node(), 1);
  YGNodeStyleSetMinWidth(title_group->yoga_node(), 0);
  YGNodeStyleSetGap(title_group->yoga_node(), YGGutterColumn,
                    theme::Spacing(2));
  if (renaming_conversation_) {
    ui::Node* field_box = title_group->AddChild(std::string(kRenameFieldId));
    YGNodeStyleSetWidth(field_box->yoga_node(), 320);
    YGNodeStyleSetFlexShrink(field_box->yoga_node(), 1);
    SetPadding(field_box->yoga_node(), theme::Spacing(2.5f),
               theme::Spacing(1.5f));
    field_box->style().corner_radius = theme::kBaseCornerRadius;
    field_box->style().background = palette_.background_primary;
    field_box->style().border = palette_.border_hover;
    field_box->style().border_width = 1;
    field_box->style().cursor = CursorKind::kIBeam;
    if (focused_field_id_ == kRenameFieldId) {
      field_box->style().outer_ring = palette_.focus_halo;
      field_box->style().outer_ring_width = 3;
    }
    ui::Node* field_text =
        BuildEditableText(field_box, kRenameFieldId, rename_text_,
                          rename_selection_, "Chat name");
    field_text->style().wraps_text = false;
    BuildTextButton(title_group, "rename-save", "Save",
                    ButtonStyle::kSecondary);
    BuildTextButton(title_group, "rename-cancel", "Cancel", ButtonStyle::kLink);
  } else {
    const Conversation& conversation =
        conversations_[active_conversation_index_];
    ui::Node* title =
        BuildLabel(title_group, conversation.title, body_strong_font_.get(),
                   palette_.text_primary);
    YGNodeStyleSetFlexShrink(title->yoga_node(), 1);
    title->style().elides_text = true;
    if (!conversation.messages.empty()) {
      BuildTextButton(title_group, "rename", "Rename", ButtonStyle::kQuiet);
      const bool deletable = !(generating_ && generating_conversation_index_ ==
                                                  active_conversation_index_);
      BuildTextButton(
          title_group, "delete",
          delete_confirmation_pending_ ? "Confirm delete" : "Delete",
          ButtonStyle::kQuiet, deletable);
    }
  }

  ui::Node* trailing_group = top_bar->AddChild();
  MakeRow(trailing_group->yoga_node());
  YGNodeStyleSetFlexShrink(trailing_group->yoga_node(), 0);
  YGNodeStyleSetGap(trailing_group->yoga_node(), YGGutterColumn,
                    theme::Spacing(4));
  BuildContextMeter(trailing_group);

  const std::string model_picker_id = "model-picker";
  const bool model_chosen = !generation_settings_.model_path.empty();
  ui::Node* model_picker = trailing_group->AddChild(model_picker_id);
  MakeRow(model_picker->yoga_node());
  YGNodeStyleSetHeight(model_picker->yoga_node(), theme::kControlHeight);
  YGNodeStyleSetFlexShrink(model_picker->yoga_node(), 0);
  YGNodeStyleSetGap(model_picker->yoga_node(), YGGutterColumn,
                    theme::Spacing(2));
  YGNodeStyleSetPadding(model_picker->yoga_node(), YGEdgeLeft,
                        theme::Spacing(3));
  YGNodeStyleSetPadding(model_picker->yoga_node(), YGEdgeRight,
                        theme::Spacing(2.5f));
  model_picker->style().corner_radius = theme::kBaseCornerRadius;
  model_picker->style().cursor = CursorKind::kHand;
  model_picker->style().background =
      InteractiveFill(model_picker_id, palette_.background_primary,
                      palette_.component_background, palette_.component_active);
  model_picker->style().border = hovered_node_id_ == model_picker_id
                                     ? palette_.border_hover
                                     : palette_.border;
  model_picker->style().border_width = 1;
  model_picker->style().shadows = palette_.shadow_small;
  BuildStatusDot(model_picker, ModelLoaded() ? palette_.success_indicator
                                             : palette_.warning_indicator);
  ui::Node* model_name = BuildLabel(
      model_picker,
      model_chosen ? FileNameFromPath(generation_settings_.model_path)
                   : "No model",
      label_strong_font_.get(), palette_.text_primary);
  YGNodeStyleSetMaxWidth(model_name->yoga_node(), 240);
  model_name->style().elides_text = true;
  if (model_state_ == ModelState::kLoading) {
    BuildLabel(
        model_picker,
        model_load_progress_ >= 1
            ? std::string("…")
            : std::to_string(static_cast<int>(model_load_progress_ * 100)) +
                  "%",
        monospace_caption_font_.get(), palette_.text_secondary);
  }
  BuildIcon(model_picker, ui::IconKind::kChevronDown, palette_.text_secondary,
            12);
}

void App::BuildMessageList(ui::Node* parent) {
  ui::Node* message_region = parent->AddChild();
  YGNodeStyleSetFlexGrow(message_region->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(message_region->yoga_node(), 1);
  YGNodeStyleSetMinHeight(message_region->yoga_node(), 0);
  ui::Node* viewport =
      message_region->AddChild(std::string(kMessageViewportId));
  MakeScrollViewport(viewport, message_scroll_offset_);
  if (!message_scroll_pinned_to_bottom_) {
    ui::Node* jump_holder = message_region->AddChild();
    YGNodeStyleSetPositionType(jump_holder->yoga_node(),
                               YGPositionTypeAbsolute);
    YGNodeStyleSetPosition(jump_holder->yoga_node(), YGEdgeLeft, 0);
    YGNodeStyleSetPosition(jump_holder->yoga_node(), YGEdgeRight, 0);
    YGNodeStyleSetPosition(jump_holder->yoga_node(), YGEdgeBottom,
                           composer_dock_height_ + theme::Spacing(3));
    YGNodeStyleSetAlignItems(jump_holder->yoga_node(), YGAlignCenter);
    ui::Node* jump_button =
        BuildTextButton(jump_holder, "jump-to-latest", "Jump to latest",
                        ButtonStyle::kSecondary);
    jump_button->style().corner_radius = theme::kControlHeight / 2;
    jump_button->style().shadows = palette_.shadow_medium;
  }

  const Conversation& conversation = conversations_[active_conversation_index_];
  ui::Node* content = viewport->AddChild();
  YGNodeStyleSetFlexShrink(content->yoga_node(), 0);
  YGNodeStyleSetFlexGrow(content->yoga_node(), 1);
  YGNodeStyleSetAlignItems(content->yoga_node(), YGAlignCenter);
  YGNodeStyleSetPadding(content->yoga_node(), YGEdgeTop, theme::Spacing(8));
  YGNodeStyleSetPadding(content->yoga_node(), YGEdgeBottom,
                        composer_dock_height_ + theme::Spacing(4));
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

  for (size_t index = 0; index < conversation.messages.size(); ++index) {
    const ChatMessage& message = conversation.messages[index];
    ui::Node* group = column->AddChild();
    YGNodeStyleSetGap(group->yoga_node(), YGGutterRow, theme::Spacing(1.5f));
    if (message.role == MessageRole::kUser) {
      ui::Node* row = group->AddChild();
      YGNodeStyleSetFlexDirection(row->yoga_node(), YGFlexDirectionRow);
      YGNodeStyleSetJustifyContent(row->yoga_node(), YGJustifyFlexEnd);
      ui::Node* bubble = row->AddChild();
      YGNodeStyleSetMaxWidthPercent(bubble->yoga_node(), 80);
      YGNodeStyleSetFlexShrink(bubble->yoga_node(), 1);
      SetPadding(bubble->yoga_node(), theme::Spacing(4), theme::Spacing(2.5f));
      bubble->style().corner_radius = theme::kRaisedCornerRadius;
      bubble->style().background = palette_.glass_fill;
      bubble->style().border = palette_.glass_border;
      bubble->style().border_width = 1;
      bubble->style().shadows = palette_.shadow_medium;
      MakeSelectable(BuildLabel(bubble, message.text, body_font_.get(),
                                palette_.text_primary, true),
                     message.text, "\n\n");
    } else if (message.is_notice) {
      BuildNotice(group, message.text);
    } else {
      ui::Node* reply_card = group->AddChild();
      YGNodeStyleSetGap(reply_card->yoga_node(), YGGutterRow,
                        theme::Spacing(2));
      SetPadding(reply_card->yoga_node(), theme::Spacing(4), theme::Spacing(3));
      reply_card->style().corner_radius = theme::kRaisedCornerRadius;
      reply_card->style().background = palette_.glass_fill;
      reply_card->style().border = palette_.glass_border;
      reply_card->style().border_width = 1;
      reply_card->style().shadows = palette_.shadow_medium;
      const bool waiting_for_text =
          message.is_streaming &&
          (message.text.empty() || !generation_settings_.stream_tokens);
      if (waiting_for_text) {
        BuildLabel(reply_card, "Writing…", body_font_.get(),
                   palette_.text_tertiary);
      } else {
        BuildMarkdown(reply_card, message.text);
      }
      if (message.is_streaming) {
        BuildLabel(reply_card, StreamingStatusText(message),
                   caption_font_.get(), palette_.text_tertiary);
      }
    }
    BuildMessageActions(group, index,
                        index + 1 == conversation.messages.size());
  }
}

void App::BuildMessageActions(ui::Node* parent, size_t message_index,
                              bool is_last_message) {
  const std::vector<ChatMessage>& messages =
      conversations_[active_conversation_index_].messages;
  const ChatMessage& message = messages[message_index];
  if (message.is_streaming) return;
  const bool is_user = message.role == MessageRole::kUser;
  const bool is_last_user_message =
      is_user && std::none_of(messages.begin() + message_index + 1,
                              messages.end(), [](const ChatMessage& later) {
                                return later.role == MessageRole::kUser;
                              });
  const bool shows_edit = is_last_user_message && !generating_;
  const bool shows_copy = !is_user && !message.is_notice;
  const bool shows_regenerate = !is_user && is_last_message && !generating_;
  if (!shows_edit && !shows_copy && !shows_regenerate) return;

  ui::Node* actions = parent->AddChild();
  MakeRow(actions->yoga_node());
  YGNodeStyleSetGap(actions->yoga_node(), YGGutterColumn, theme::Spacing(2));
  YGNodeStyleSetJustifyContent(actions->yoga_node(),
                               is_user ? YGJustifyFlexEnd : YGJustifyFlexStart);
  if (shows_edit) {
    BuildTextButton(actions, "edit-message", "Edit", ButtonStyle::kQuiet);
  }
  if (shows_copy) {
    BuildCopyButton(actions, std::string(kCopyMessageIdPrefix) +
                                 std::to_string(message_index));
  }
  if (shows_regenerate) {
    BuildTextButton(actions, "regenerate",
                    message.is_notice ? "Try again" : "Regenerate",
                    ButtonStyle::kQuiet);
  }
  if (shows_copy && message.generated_token_count > 0) {
    ui::Node* statistics =
        BuildLabel(actions, FinishedStatisticsText(message),
                   caption_font_.get(), palette_.text_tertiary);
    YGNodeStyleSetMargin(statistics->yoga_node(), YGEdgeLeft,
                         theme::Spacing(1.5f));
  }
}

std::string App::StreamingStatusText(const ChatMessage& message) const {
  if (message.generated_token_count == 0) {
    if (message.prompt_token_count == 0) return "Starting…";
    return std::format("Reading {} tokens of conversation…",
                       message.prompt_token_count);
  }
  return std::format("{} tokens · {:.1f} tokens/s",
                     message.generated_token_count, TokensPerSecond(message));
}

std::string App::FinishedStatisticsText(const ChatMessage& message) const {
  return std::format("{} tokens · {:.1f} tokens/s · {:.1f} s",
                     message.generated_token_count, TokensPerSecond(message),
                     message.generation_seconds);
}

float App::TokensPerSecond(const ChatMessage& message) {
  if (message.generation_seconds <= 0) return 0;
  return static_cast<float>(message.generated_token_count) /
         message.generation_seconds;
}

ui::Node* App::BuildCopyButton(ui::Node* parent, std::string id) {
  const bool copied = copied_button_id_ == id;
  return BuildTextButton(parent, std::move(id), copied ? "Copied" : "Copy",
                         ButtonStyle::kQuiet);
}

ui::Node* App::BuildRichText(ui::Node* parent,
                             const ui::MarkdownInlines& inlines,
                             const ui::Font* regular_font,
                             const ui::Font* strong_font, ui::Color color) {
  ui::Node* text = parent->AddChild();
  for (const ui::MarkdownInline& piece : inlines) {
    ui::TextSpan span;
    span.text = piece.text;
    span.color = piece.link ? palette_.accent_text : color;
    span.font = piece.bold ? strong_font : regular_font;
    if (piece.italic && regular_font == body_font_.get()) {
      span.font =
          piece.bold ? body_strong_italic_font_.get() : body_italic_font_.get();
    }
    if (piece.code) {
      span.font = monospace_body_font_.get();
      span.background = palette_.overlay_selected;
    }
    text->style().spans.push_back(std::move(span));
  }
  if (text->style().spans.empty()) {
    text->style().spans.push_back({" ", regular_font, color});
  }
  YGNodeStyleSetFlexShrink(text->yoga_node(), 1);
  text->EnableTextMeasurement();
  MakeSelectable(text, ui::PlainTextOfSpans(text->style().spans), "\n\n");
  return text;
}

void App::MakeSelectable(ui::Node* node, std::string plain_text,
                         std::string_view separator) {
  const size_t block_index = selectable_texts_.size();
  std::string node_id =
      std::string(kSelectableIdPrefix) + std::to_string(block_index);
  node->set_id(node_id);
  node->style().cursor = CursorKind::kIBeam;
  if (HasMessageSelection()) {
    const auto [first, last] =
        std::minmax(message_selection_anchor_, message_selection_focus_);
    if (block_index >= first.block_index && block_index <= last.block_index) {
      node->style().selection_begin =
          block_index == first.block_index ? first.offset : 0;
      node->style().selection_end =
          block_index == last.block_index ? last.offset : plain_text.size();
      node->style().selection_color = palette_.text_selection;
    }
  }
  selectable_texts_.push_back(
      {std::move(node_id), std::move(plain_text), separator});
}

bool App::HasMessageSelection() const {
  return message_selection_anchor_ != message_selection_focus_;
}

void App::ClearMessageSelection() {
  message_selection_anchor_ = {};
  message_selection_focus_ = {};
  dragging_message_selection_ = false;
}

std::string App::SelectedMessageText() const {
  if (!HasMessageSelection()) return {};
  const auto [first, last] =
      std::minmax(message_selection_anchor_, message_selection_focus_);
  std::string selected;
  for (size_t index = first.block_index;
       index <= last.block_index && index < selectable_texts_.size(); ++index) {
    const SelectableText& block = selectable_texts_[index];
    const size_t begin = index == first.block_index
                             ? std::min(first.offset, block.text.size())
                             : 0;
    const size_t end = index == last.block_index
                           ? std::min(last.offset, block.text.size())
                           : block.text.size();
    if (index > first.block_index) selected += block.separator;
    if (end > begin) selected.append(block.text, begin, end - begin);
  }
  return selected;
}

std::optional<MessageTextPoint> App::MessageTextPointAt(float logical_x,
                                                        float logical_y) const {
  if (!interface_root_ || selectable_texts_.empty()) return std::nullopt;
  for (size_t index = 0; index < selectable_texts_.size(); ++index) {
    const ui::Node* node =
        interface_root_->FindById(selectable_texts_[index].node_id);
    if (!node) continue;
    const ui::Rect& bounds = node->bounds();
    if (logical_y < bounds.y) return MessageTextPoint{index, 0};
    if (logical_y >= bounds.y + bounds.height) continue;
    const ui::NodeStyle& style = node->style();
    const float local_x = logical_x - bounds.x;
    const float local_y = logical_y - bounds.y;
    size_t offset = 0;
    if (!style.spans.empty()) {
      offset = ui::RichTextOffsetAtPoint(
          style.spans, ui::LayoutRichText(style.spans, bounds.width), local_x,
          local_y);
    } else if (style.font) {
      offset = ui::ByteAtPoint(
          *style.font, style.text,
          ui::WrapForEditing(*style.font, style.text,
                             ui::TextWrapWidth(style, bounds.width)),
          local_x, local_y);
    }
    return MessageTextPoint{index, offset};
  }
  const size_t last_index = selectable_texts_.size() - 1;
  return MessageTextPoint{last_index,
                          selectable_texts_[last_index].text.size()};
}

void App::BuildMarkdown(ui::Node* parent, std::string_view markdown) {
  ui::Node* document = parent->AddChild();
  YGNodeStyleSetGap(document->yoga_node(), YGGutterRow, theme::Spacing(3));
  ui::Node* list = nullptr;
  for (const ui::MarkdownBlock& block : ui::ParseMarkdown(markdown)) {
    if (block.kind != ui::MarkdownBlockKind::kListItem) {
      list = nullptr;
      BuildMarkdownBlock(document, block);
      continue;
    }
    if (!list) {
      list = document->AddChild();
      YGNodeStyleSetGap(list->yoga_node(), YGGutterRow, theme::Spacing(1.5f));
    }
    BuildMarkdownBlock(list, block);
  }
}

void App::BuildMarkdownBlock(ui::Node* parent, const ui::MarkdownBlock& block) {
  using ui::MarkdownBlockKind;
  switch (block.kind) {
    case MarkdownBlockKind::kParagraph:
      BuildRichText(parent, block.inlines, body_font_.get(),
                    body_strong_font_.get(), palette_.text_primary);
      return;
    case MarkdownBlockKind::kHeading: {
      const ui::Font* font = block.level == 1   ? large_heading_font_.get()
                             : block.level == 2 ? medium_heading_font_.get()
                                                : body_strong_font_.get();
      ui::Node* heading = BuildRichText(parent, block.inlines, font, font,
                                        palette_.text_primary);
      YGNodeStyleSetMargin(heading->yoga_node(), YGEdgeTop,
                           block.level <= 2 ? theme::Spacing(2) : 0);
      return;
    }
    case MarkdownBlockKind::kCodeBlock: {
      ui::Node* box = parent->AddChild();
      YGNodeStyleSetGap(box->yoga_node(), YGGutterRow, theme::Spacing(2));
      SetPadding(box->yoga_node(), theme::Spacing(3.5f), theme::Spacing(3));
      box->style().corner_radius = theme::kBaseCornerRadius;
      box->style().background = palette_.component_background;
      box->style().border = palette_.border;
      box->style().border_width = 1;
      ui::Node* header = box->AddChild();
      MakeRow(header->yoga_node());
      YGNodeStyleSetJustifyContent(header->yoga_node(), YGJustifySpaceBetween);
      YGNodeStyleSetMargin(header->yoga_node(), YGEdgeVertical,
                           -theme::Spacing(1));
      YGNodeStyleSetMargin(header->yoga_node(), YGEdgeRight,
                           -theme::Spacing(1.5f));
      BuildLabel(header, block.language, monospace_caption_font_.get(),
                 palette_.text_tertiary);
      BuildCopyButton(header, std::string(kCopyCodeIdPrefix) +
                                  std::to_string(copyable_code_blocks_.size()));
      copyable_code_blocks_.push_back(block.code);
      const std::string shown_code = block.code.empty() ? " " : block.code;
      MakeSelectable(BuildLabel(box, shown_code, monospace_body_font_.get(),
                                palette_.text_primary, true),
                     shown_code, "\n\n");
      return;
    }
    case MarkdownBlockKind::kListItem: {
      ui::Node* row = parent->AddChild();
      YGNodeStyleSetFlexDirection(row->yoga_node(), YGFlexDirectionRow);
      YGNodeStyleSetPadding(
          row->yoga_node(), YGEdgeLeft,
          theme::kListMarkerWidth * static_cast<float>(block.level));
      ui::Node* marker = row->AddChild();
      YGNodeStyleSetWidth(marker->yoga_node(), theme::kListMarkerWidth);
      YGNodeStyleSetFlexShrink(marker->yoga_node(), 0);
      BuildLabel(
          marker,
          block.ordered ? std::to_string(block.number) + "." : "\xE2\x80\xA2",
          body_font_.get(), palette_.text_secondary);
      ui::Node* text =
          BuildRichText(row, block.inlines, body_font_.get(),
                        body_strong_font_.get(), palette_.text_primary);
      selectable_texts_.back().separator = "\n";
      YGNodeStyleSetFlexGrow(text->yoga_node(), 1);
      YGNodeStyleSetFlexBasis(text->yoga_node(), 0);
      return;
    }
    case MarkdownBlockKind::kQuote: {
      ui::Node* row = parent->AddChild();
      YGNodeStyleSetFlexDirection(row->yoga_node(), YGFlexDirectionRow);
      YGNodeStyleSetGap(row->yoga_node(), YGGutterColumn, theme::Spacing(3));
      ui::Node* bar = row->AddChild();
      YGNodeStyleSetWidth(bar->yoga_node(), theme::kQuoteBarWidth);
      YGNodeStyleSetFlexShrink(bar->yoga_node(), 0);
      bar->style().corner_radius = theme::kQuoteBarWidth / 2;
      bar->style().background = palette_.border_hover;
      ui::Node* text =
          BuildRichText(row, block.inlines, body_font_.get(),
                        body_strong_font_.get(), palette_.text_secondary);
      YGNodeStyleSetFlexGrow(text->yoga_node(), 1);
      YGNodeStyleSetFlexBasis(text->yoga_node(), 0);
      return;
    }
    case MarkdownBlockKind::kRule:
      AddHorizontalRule(parent, palette_.border);
      return;
    case MarkdownBlockKind::kTable:
      BuildMarkdownTable(parent, block);
      return;
  }
}

void App::BuildMarkdownTable(ui::Node* parent, const ui::MarkdownBlock& block) {
  if (block.table_rows.empty()) return;
  const size_t column_count = block.table_rows.front().size();
  ui::Node* table = parent->AddChild();
  table->style().corner_radius = theme::kBaseCornerRadius;
  table->style().border = palette_.border;
  table->style().border_width = 1;
  for (size_t row_index = 0; row_index < block.table_rows.size(); ++row_index) {
    if (row_index > 0) AddHorizontalRule(table, palette_.border);
    const bool is_header = row_index == 0;
    ui::Node* row = table->AddChild();
    YGNodeStyleSetFlexDirection(row->yoga_node(), YGFlexDirectionRow);
    if (is_header) {
      row->style().background = palette_.component_background;
      row->style().corner_radius = theme::kBaseCornerRadius;
    }
    const auto& cells = block.table_rows[row_index];
    for (size_t column = 0; column < column_count; ++column) {
      ui::Node* cell = row->AddChild();
      YGNodeStyleSetFlexGrow(cell->yoga_node(), 1);
      YGNodeStyleSetFlexBasis(cell->yoga_node(), 0);
      YGNodeStyleSetMinWidth(cell->yoga_node(), 0);
      SetPadding(cell->yoga_node(), theme::Spacing(3), theme::Spacing(2));
      const ui::MarkdownInlines empty_cell;
      BuildRichText(cell, column < cells.size() ? cells[column] : empty_cell,
                    is_header ? body_strong_font_.get() : body_font_.get(),
                    body_strong_font_.get(), palette_.text_primary);
      selectable_texts_.back().separator = column == 0 ? "\n" : "\t";
    }
  }
}

void App::BuildEmptyState(ui::Node* parent) {
  ui::Node* empty_state = parent->AddChild();
  YGNodeStyleSetAlignItems(empty_state->yoga_node(), YGAlignCenter);
  YGNodeStyleSetPadding(empty_state->yoga_node(), YGEdgeHorizontal,
                        theme::Spacing(6));
  const bool model_loaded = ModelLoaded();
  const bool model_loading = model_state_ == ModelState::kLoading;
  if (model_loaded) {
    ui::Node* brand = empty_state->AddChild();
    MakeRow(brand->yoga_node());
    YGNodeStyleSetGap(brand->yoga_node(), YGGutterColumn, theme::Spacing(3));
    BuildIcon(brand, ui::IconKind::kTulip, palette_.high_contrast,
              theme::kBrandMarkSize);
    BuildLabel(brand, "Lale", display_font_.get(), palette_.text_primary);
  } else {
    BuildLabel(
        empty_state,
        model_loading ? "Getting the model ready" : "Choose a model to begin",
        title_font_.get(), palette_.text_primary);
  }
  AddVerticalSpace(empty_state, theme::Spacing(3));
  BuildLabel(empty_state, "Lale runs llama.cpp models locally on this PC.",
             body_font_.get(), palette_.text_secondary);
  AddVerticalSpace(empty_state, theme::Spacing(1));
  BuildLabel(empty_state, "Nothing you type leaves your computer.",
             body_font_.get(), palette_.text_secondary);
  if (model_loaded) return;
  if (model_loading) {
    AddVerticalSpace(empty_state, theme::Spacing(6));
    ui::Node* progress = empty_state->AddChild();
    YGNodeStyleSetWidth(progress->yoga_node(), 280);
    YGNodeStyleSetAlignItems(progress->yoga_node(), YGAlignCenter);
    YGNodeStyleSetGap(progress->yoga_node(), YGGutterRow, theme::Spacing(2));
    ui::Node* bar_holder = progress->AddChild();
    YGNodeStyleSetWidthPercent(bar_holder->yoga_node(), 100);
    BuildProgressBar(bar_holder, model_load_progress_, 4);
    BuildLabel(progress, ModelLoadingText(), caption_font_.get(),
               palette_.text_tertiary);
    return;
  }

  AddVerticalSpace(empty_state, theme::Spacing(6));
  ui::Node* actions = empty_state->AddChild();
  MakeRow(actions->yoga_node());
  YGNodeStyleSetGap(actions->yoga_node(), YGGutterColumn, theme::Spacing(3));
  BuildTextButton(actions, "empty-open-settings", "Open Settings",
                  ButtonStyle::kPrimary);
  ui::Node* shortcut = actions->AddChild();
  MakeRow(shortcut->yoga_node());
  YGNodeStyleSetGap(shortcut->yoga_node(), YGGutterColumn,
                    theme::Spacing(1.5f));
  BuildLabel(shortcut, "or press", label_font_.get(), palette_.text_tertiary);
  BuildKeyHint(shortcut, "Ctrl ,");
}

void App::BuildComposer(ui::Node* parent) {
  const bool composer_focused = focused_field_id_ == kComposerFieldId;
  ui::Node* dock = parent->AddChild();
  composer_dock_node_ = dock;
  YGNodeStyleSetPositionType(dock->yoga_node(), YGPositionTypeAbsolute);
  YGNodeStyleSetPosition(dock->yoga_node(), YGEdgeLeft, 0);
  YGNodeStyleSetPosition(dock->yoga_node(), YGEdgeRight, 0);
  YGNodeStyleSetPosition(dock->yoga_node(), YGEdgeBottom, 0);
  YGNodeStyleSetAlignItems(dock->yoga_node(), YGAlignCenter);
  YGNodeStyleSetPadding(dock->yoga_node(), YGEdgeHorizontal, theme::Spacing(6));
  YGNodeStyleSetPadding(dock->yoga_node(), YGEdgeTop, theme::Spacing(2));
  YGNodeStyleSetPadding(dock->yoga_node(), YGEdgeBottom, theme::Spacing(6));

  ui::Node* field_box = dock->AddChild(std::string(kComposerFieldId));
  YGNodeStyleSetWidthPercent(field_box->yoga_node(), 100);
  YGNodeStyleSetMaxWidth(
      field_box->yoga_node(),
      theme::kMessageColumnMaximumWidth - 2 * theme::Spacing(6));
  YGNodeStyleSetPadding(field_box->yoga_node(), YGEdgeAll, theme::Spacing(3));
  YGNodeStyleSetPadding(field_box->yoga_node(), YGEdgeLeft, theme::Spacing(4));
  YGNodeStyleSetGap(field_box->yoga_node(), YGGutterRow, theme::Spacing(2));
  field_box->style().corner_radius = theme::kRaisedCornerRadius;
  field_box->style().background = palette_.glass_fill;
  field_box->style().backdrop_blur = theme::kGlassBlur;
  field_box->style().border =
      composer_focused ? palette_.border_focus : palette_.glass_border;
  field_box->style().border_width = 1;
  field_box->style().shadows = palette_.shadow_large;
  if (composer_focused) {
    field_box->style().outer_ring = palette_.focus_halo;
    field_box->style().outer_ring_width = 3;
  }
  field_box->style().cursor = CursorKind::kIBeam;

  ui::Node* field_text =
      BuildEditableText(field_box, kComposerFieldId, composer_text_,
                        composer_selection_, "Message Lale");
  YGNodeStyleSetMaxHeight(field_text->yoga_node(),
                          theme::kComposerMaximumTextHeight);
  YGNodeStyleSetMinHeight(field_text->yoga_node(), 22);
  YGNodeStyleSetMargin(field_text->yoga_node(), YGEdgeTop, theme::Spacing(1));

  ui::Node* toolbar = field_box->AddChild();
  MakeRow(toolbar->yoga_node());
  YGNodeStyleSetJustifyContent(toolbar->yoga_node(), YGJustifySpaceBetween);
  ui::Node* hints = toolbar->AddChild();
  MakeRow(hints->yoga_node());
  YGNodeStyleSetGap(hints->yoga_node(), YGGutterColumn, theme::Spacing(1.5f));
  BuildKeyHint(hints, "Enter");
  BuildLabel(hints, "send", caption_font_.get(), palette_.text_tertiary);
  ui::Node* hint_spacer = hints->AddChild();
  YGNodeStyleSetWidth(hint_spacer->yoga_node(), theme::Spacing(1.5f));
  BuildKeyHint(hints, "Shift Enter");
  BuildLabel(hints, "new line", caption_font_.get(), palette_.text_tertiary);

  const bool can_send = generating_ || !TrimWhitespace(composer_text_).empty();
  const std::string send_id =
      generating_ ? "stop" : (can_send ? "send" : std::string());
  ui::Node* send_button = toolbar->AddChild(send_id);
  YGNodeStyleSetWidth(send_button->yoga_node(), 32);
  YGNodeStyleSetHeight(send_button->yoga_node(), 32);
  YGNodeStyleSetFlexShrink(send_button->yoga_node(), 0);
  send_button->style().corner_radius = 16;
  send_button->style().cursor =
      can_send ? CursorKind::kHand : CursorKind::kIBeam;
  send_button->style().background =
      can_send ? InteractiveFill(send_id, palette_.high_contrast,
                                 palette_.high_contrast_hover,
                                 palette_.high_contrast_hover)
               : palette_.component_background;
  send_button->style().icon =
      generating_ ? ui::IconKind::kStop : ui::IconKind::kArrowUp;
  send_button->style().icon_color =
      can_send ? palette_.text_on_high_contrast : palette_.text_tertiary;
}

ui::Node* App::BuildSettingsSection(ui::Node* parent, std::string caption) {
  ui::Node* caption_box = parent->AddChild();
  YGNodeStyleSetMargin(caption_box->yoga_node(), YGEdgeTop, theme::Spacing(8));
  YGNodeStyleSetMargin(caption_box->yoga_node(), YGEdgeBottom,
                       theme::Spacing(3));
  BuildLabel(caption_box, std::move(caption), body_strong_font_.get(),
             palette_.text_primary);

  ui::Node* card = parent->AddChild();
  card->style().corner_radius = theme::kBaseCornerRadius;
  card->style().background = palette_.background_primary;
  card->style().border = palette_.border;
  card->style().border_width = 1;
  card->style().shadows = palette_.shadow_small;
  return card;
}

void App::BuildSettingsRowSeparator(ui::Node* card) {
  AddHorizontalRule(card, palette_.border);
}

ui::Node* App::BuildSettingsRow(ui::Node* card, std::string_view label,
                                std::string_view description) {
  ui::Node* row = card->AddChild();
  MakeRow(row->yoga_node());
  YGNodeStyleSetMinHeight(row->yoga_node(), theme::kSettingsRowHeight + 8);
  YGNodeStyleSetGap(row->yoga_node(), YGGutterColumn, theme::Spacing(6));
  SetPadding(row->yoga_node(), theme::Spacing(4), theme::Spacing(3));
  ui::Node* text = row->AddChild();
  YGNodeStyleSetFlexGrow(text->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(text->yoga_node(), 1);
  YGNodeStyleSetGap(text->yoga_node(), YGGutterRow, theme::Spacing(0.5f));
  BuildLabel(text, std::string(label), label_strong_font_.get(),
             palette_.text_primary, true);
  if (!description.empty()) {
    BuildLabel(text, std::string(description), caption_font_.get(),
               palette_.text_secondary, true);
  }
  return row;
}

void App::BuildSettingsNavigation(ui::Node* parent) {
  ui::Node* navigation = parent->AddChild();
  YGNodeRef navigation_layout = navigation->yoga_node();
  YGNodeStyleSetWidth(navigation_layout, theme::kSettingsNavigationWidth - 1);
  YGNodeStyleSetFlexShrink(navigation_layout, 0);
  YGNodeStyleSetPadding(navigation_layout, YGEdgeAll, theme::Spacing(3));
  YGNodeStyleSetGap(navigation_layout, YGGutterRow, theme::Spacing(0.5f));
  navigation->style().background = palette_.background_secondary;

  BuildSidebarRow(navigation, "settings-back", "Back to chat",
                  ui::IconKind::kArrowLeft, false, "Esc");

  ui::Node* section_caption = navigation->AddChild();
  YGNodeStyleSetMargin(section_caption->yoga_node(), YGEdgeTop,
                       theme::Spacing(5));
  YGNodeStyleSetMargin(section_caption->yoga_node(), YGEdgeBottom,
                       theme::Spacing(1));
  YGNodeStyleSetPadding(section_caption->yoga_node(), YGEdgeHorizontal,
                        theme::Spacing(2.5f));
  BuildLabel(section_caption, "Settings", caption_font_.get(),
             palette_.text_tertiary);

  struct NavigationEntry {
    SettingsSection section;
    const char* key;
    const char* label;
  };
  constexpr NavigationEntry kEntries[] = {
      {SettingsSection::kModel, "model", "Model"},
      {SettingsSection::kSampling, "sampling", "Sampling"},
      {SettingsSection::kAppearance, "appearance", "Appearance"},
      {SettingsSection::kShortcuts, "shortcuts", "Keyboard shortcuts"},
  };
  for (const NavigationEntry& entry : kEntries) {
    BuildSidebarRow(navigation,
                    std::string(kSettingsSectionIdPrefix) + entry.key,
                    entry.label, ui::IconKind::kNone,
                    settings_section_ == entry.section, {});
  }
}

void App::BuildSettingsPage(ui::Node* parent) {
  ui::Node* page = parent->AddChild();
  YGNodeStyleSetFlexGrow(page->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(page->yoga_node(), 1);
  YGNodeStyleSetMinWidth(page->yoga_node(), 0);

  ui::Node* viewport = page->AddChild(std::string(kSettingsViewportId));
  MakeScrollViewport(viewport, settings_scroll_offset_);
  ui::Node* content = viewport->AddChild();
  YGNodeStyleSetFlexShrink(content->yoga_node(), 0);
  YGNodeStyleSetAlignItems(content->yoga_node(), YGAlignCenter);
  YGNodeStyleSetPadding(content->yoga_node(), YGEdgeTop, theme::Spacing(10));
  YGNodeStyleSetPadding(content->yoga_node(), YGEdgeBottom, theme::Spacing(12));

  ui::Node* column = content->AddChild();
  YGNodeStyleSetWidthPercent(column->yoga_node(), 100);
  YGNodeStyleSetMaxWidth(column->yoga_node(),
                         theme::kSettingsColumnMaximumWidth);
  YGNodeStyleSetPadding(column->yoga_node(), YGEdgeHorizontal,
                        theme::Spacing(8));
  switch (settings_section_) {
    case SettingsSection::kModel:
      BuildModelSettings(column);
      break;
    case SettingsSection::kSampling:
      BuildSamplingSettings(column);
      break;
    case SettingsSection::kAppearance:
      BuildAppearanceSettings(column);
      break;
    case SettingsSection::kShortcuts:
      BuildShortcutSettings(column);
      break;
  }
}

void App::BuildSettingsPageHeading(ui::Node* column, std::string title,
                                   std::string description) {
  BuildLabel(column, std::move(title), title_font_.get(),
             palette_.text_primary);
  AddVerticalSpace(column, theme::Spacing(2));
  BuildLabel(column, std::move(description), body_font_.get(),
             palette_.text_secondary, true);
}

void App::BuildModelSettings(ui::Node* column) {
  BuildSettingsPageHeading(
      column, "Model",
      "Lale runs GGUF models with llama.cpp on this PC. Nothing is uploaded.");

  const std::string& model_path = generation_settings_.model_path;
  const bool model_chosen = !model_path.empty();
  ui::Node* file_card = BuildSettingsSection(column, "Model file");
  ui::Node* file_row = file_card->AddChild();
  YGNodeStyleSetFlexDirection(file_row->yoga_node(), YGFlexDirectionRow);
  YGNodeStyleSetAlignItems(file_row->yoga_node(), YGAlignCenter);
  YGNodeStyleSetGap(file_row->yoga_node(), YGGutterColumn, theme::Spacing(3));
  SetPadding(file_row->yoga_node(), theme::Spacing(4), theme::Spacing(4));
  BuildStatusDot(file_row, ModelLoaded() ? palette_.success_indicator
                                         : palette_.warning_indicator);
  ui::Node* file_text = file_row->AddChild();
  YGNodeStyleSetFlexGrow(file_text->yoga_node(), 1);
  YGNodeStyleSetFlexShrink(file_text->yoga_node(), 1);
  YGNodeStyleSetMinWidth(file_text->yoga_node(), 0);
  YGNodeStyleSetGap(file_text->yoga_node(), YGGutterRow, theme::Spacing(1));
  ui::Node* file_name = BuildLabel(
      file_text,
      model_chosen ? FileNameFromPath(model_path) : "No model selected",
      label_strong_font_.get(), palette_.text_primary);
  file_name->style().elides_text = true;
  ui::Node* file_detail = BuildLabel(
      file_text, model_chosen ? model_path : "Choose a .gguf file to use.",
      model_chosen ? monospace_caption_font_.get() : caption_font_.get(),
      palette_.text_secondary);
  file_detail->style().elides_text = true;
  if (model_chosen) {
    BuildTextButton(file_row, "model-clear", "Remove", ButtonStyle::kLink);
  }
  BuildTextButton(
      file_row, "model-choose", model_chosen ? "Change" : "Choose file",
      model_chosen ? ButtonStyle::kSecondary : ButtonStyle::kPrimary);

  if (model_state_ == ModelState::kLoading) {
    BuildSettingsRowSeparator(file_card);
    ui::Node* progress_row = file_card->AddChild();
    YGNodeStyleSetGap(progress_row->yoga_node(), YGGutterRow,
                      theme::Spacing(2));
    SetPadding(progress_row->yoga_node(), theme::Spacing(4), theme::Spacing(3));
    BuildLabel(progress_row, ModelLoadingText(), caption_font_.get(),
               palette_.text_secondary);
    BuildProgressBar(progress_row, model_load_progress_, 4);
  } else if (model_state_ == ModelState::kFailed) {
    AddVerticalSpace(column, theme::Spacing(4));
    BuildNotice(column, model_error_);
  }

  BuildRecentModels(column);

  ui::Node* runtime_card = BuildSettingsSection(column, "Runtime");
  const int gpu_layers = generation_settings_.gpu_layer_count;
  BuildStepperRow(runtime_card, "GPU layers",
                  inference_engine_->SupportsGpuOffload()
                      ? "Layers offloaded to the graphics card. More is faster "
                        "but uses more video memory."
                      : "This llama.cpp build runs on the CPU only, so this "
                        "has no effect.",
                  "gpu-layers",
                  gpu_layers >= GenerationSettings::kAllGpuLayers
                      ? "All"
                      : std::to_string(gpu_layers));
  BuildSettingsRowSeparator(runtime_card);
  const int threads = generation_settings_.thread_count;
  BuildStepperRow(runtime_card, "CPU threads",
                  "Threads used for work that stays on the processor.",
                  "threads",
                  threads == GenerationSettings::kAutomaticThreadCount
                      ? "Auto"
                      : std::to_string(threads));
  BuildSettingsRowSeparator(runtime_card);
  BuildStepperRow(runtime_card, "Context size",
                  "How many tokens of the conversation the model can see.",
                  "context-size",
                  std::to_string(generation_settings_.context_size));
  AddVerticalSpace(column, theme::Spacing(3));
  BuildLabel(column,
             "Runtime changes reload the model when you leave Settings.",
             caption_font_.get(), palette_.text_tertiary, true);
}

void App::BuildRecentModels(ui::Node* column) {
  std::vector<size_t> other_model_indices;
  for (size_t index = 0; index < recent_model_paths_.size(); ++index) {
    if (recent_model_paths_[index] != generation_settings_.model_path) {
      other_model_indices.push_back(index);
    }
  }
  if (other_model_indices.empty()) return;
  ui::Node* recent_card = BuildSettingsSection(column, "Recent models");
  for (size_t position = 0; position < other_model_indices.size(); ++position) {
    if (position > 0) BuildSettingsRowSeparator(recent_card);
    const size_t index = other_model_indices[position];
    const std::string& recent_path = recent_model_paths_[index];
    const std::string row_id =
        std::string(kRecentModelIdPrefix) + std::to_string(index);
    ui::Node* row = recent_card->AddChild(row_id);
    MakeRow(row->yoga_node());
    YGNodeStyleSetGap(row->yoga_node(), YGGutterColumn, theme::Spacing(3));
    SetPadding(row->yoga_node(), theme::Spacing(4), theme::Spacing(3));
    row->style().cursor = CursorKind::kHand;
    row->style().background =
        InteractiveFill(row_id, theme::kTransparent, palette_.overlay_hover,
                        palette_.overlay_pressed);
    ui::Node* text = row->AddChild();
    YGNodeStyleSetFlexGrow(text->yoga_node(), 1);
    YGNodeStyleSetFlexShrink(text->yoga_node(), 1);
    YGNodeStyleSetMinWidth(text->yoga_node(), 0);
    YGNodeStyleSetGap(text->yoga_node(), YGGutterRow, theme::Spacing(1));
    BuildLabel(text, FileNameFromPath(recent_path), label_strong_font_.get(),
               palette_.text_primary)
        ->style()
        .elides_text = true;
    BuildLabel(text, recent_path, monospace_caption_font_.get(),
               palette_.text_secondary)
        ->style()
        .elides_text = true;
    BuildLabel(row, "Use", label_font_.get(),
               hovered_node_id_ == row_id ? palette_.text_primary
                                          : palette_.text_secondary);
  }
}

void App::BuildSamplingSettings(ui::Node* column) {
  BuildSettingsPageHeading(column, "Sampling",
                           "How the model picks each next token.");

  ui::Node* sampling_card = BuildSettingsSection(column, "Randomness");
  BuildStepperRow(sampling_card, "Temperature",
                  "Higher values give more varied replies.", "temperature",
                  FormatDecimal(generation_settings_.temperature));
  BuildSettingsRowSeparator(sampling_card);
  BuildStepperRow(sampling_card, "Top P",
                  "Keeps the smallest set of tokens whose odds add up to P.",
                  "top-p", FormatDecimal(generation_settings_.top_p));
  BuildSettingsRowSeparator(sampling_card);
  BuildStepperRow(sampling_card, "Top K",
                  "Keeps only the K most likely tokens. 0 turns it off.",
                  "top-k", std::to_string(generation_settings_.top_k));
  BuildSettingsRowSeparator(sampling_card);
  BuildStepperRow(sampling_card, "Repeat penalty",
                  "Discourages repeating recent tokens. 1.00 turns it off.",
                  "repeat-penalty",
                  FormatDecimal(generation_settings_.repeat_penalty));

  ui::Node* length_card = BuildSettingsSection(column, "Replies");
  BuildStepperRow(length_card, "Max tokens",
                  "The longest reply the model may write.", "max-tokens",
                  std::to_string(generation_settings_.maximum_tokens));
  BuildSettingsRowSeparator(length_card);
  BuildToggleRow(length_card, "Stream tokens",
                 "Show the reply as it is written.", "stream",
                 generation_settings_.stream_tokens);

  ui::Node* prompt_card = BuildSettingsSection(column, "System prompt");
  const bool system_prompt_focused = focused_field_id_ == kSystemPromptFieldId;
  ui::Node* prompt_field =
      prompt_card->AddChild(std::string(kSystemPromptFieldId));
  YGNodeStyleSetMinHeight(prompt_field->yoga_node(), 120);
  SetPadding(prompt_field->yoga_node(), theme::Spacing(4), theme::Spacing(3));
  prompt_field->style().corner_radius = theme::kBaseCornerRadius;
  prompt_field->style().cursor = CursorKind::kIBeam;
  if (system_prompt_focused) {
    prompt_card->style().border = palette_.border_focus;
    prompt_card->style().outer_ring = palette_.focus_halo;
    prompt_card->style().outer_ring_width = 3;
  }
  BuildEditableText(prompt_field, kSystemPromptFieldId,
                    generation_settings_.system_prompt,
                    system_prompt_selection_, "No system prompt");

  ui::Node* reset_row = column->AddChild();
  MakeRow(reset_row->yoga_node());
  YGNodeStyleSetMargin(reset_row->yoga_node(), YGEdgeTop, theme::Spacing(6));
  YGNodeStyleSetGap(reset_row->yoga_node(), YGGutterColumn, theme::Spacing(3));
  BuildTextButton(reset_row, "reset-settings", "Reset to defaults",
                  ButtonStyle::kSecondary, !SettingsMatchDefaults());
  BuildLabel(reset_row, "Your model file is kept.", caption_font_.get(),
             palette_.text_tertiary);
}

void App::BuildAppearanceSettings(ui::Node* column) {
  BuildSettingsPageHeading(column, "Appearance",
                           "Choose how Lale looks on this PC.");
  ui::Node* theme_card = BuildSettingsSection(column, "Theme");
  ui::Node* theme_row =
      BuildSettingsRow(theme_card, "Color scheme",
                       "System follows the Windows light or dark setting.");
  const char* selected_key = "system";
  if (theme_preference_ == ThemePreference::kLight) selected_key = "light";
  if (theme_preference_ == ThemePreference::kDark) selected_key = "dark";
  BuildSegmentedControl(
      theme_row, kThemeIdPrefix,
      {{"system", "System"}, {"light", "Light"}, {"dark", "Dark"}},
      selected_key);
}

void App::BuildShortcutSettings(ui::Node* column) {
  BuildSettingsPageHeading(column, "Keyboard shortcuts",
                           "Shortcuts that work anywhere in Lale.");
  ui::Node* general_card = BuildSettingsSection(column, "General");
  BuildShortcutRow(general_card, "New chat", "Ctrl N");
  BuildSettingsRowSeparator(general_card);
  BuildShortcutRow(general_card, "Open or close Settings", "Ctrl ,");
  BuildSettingsRowSeparator(general_card);
  BuildShortcutRow(general_card, "Close Settings or leave a text field", "Esc");
  ui::Node* typing_card = BuildSettingsSection(column, "Writing");
  BuildShortcutRow(typing_card, "Send message", "Enter");
  BuildSettingsRowSeparator(typing_card);
  BuildShortcutRow(typing_card, "New line", "Shift Enter");
  BuildSettingsRowSeparator(typing_card);
  BuildShortcutRow(typing_card, "Paste", "Ctrl V");
}

void App::BuildShortcutRow(ui::Node* card, std::string_view action,
                           std::string keys) {
  ui::Node* row = card->AddChild();
  MakeRow(row->yoga_node());
  YGNodeStyleSetHeight(row->yoga_node(), theme::kSettingsRowHeight);
  YGNodeStyleSetJustifyContent(row->yoga_node(), YGJustifySpaceBetween);
  YGNodeStyleSetPadding(row->yoga_node(), YGEdgeHorizontal, theme::Spacing(4));
  BuildLabel(row, std::string(action), label_font_.get(),
             palette_.text_primary);
  BuildKeyHint(row, std::move(keys));
}

void App::BuildSegmentedControl(ui::Node* parent, std::string_view id_prefix,
                                const std::vector<SegmentOption>& options,
                                std::string_view selected_key) {
  ui::Node* control = parent->AddChild();
  MakeRow(control->yoga_node());
  YGNodeStyleSetFlexShrink(control->yoga_node(), 0);
  YGNodeStyleSetHeight(control->yoga_node(), theme::kControlHeight + 4);
  YGNodeStyleSetPadding(control->yoga_node(), YGEdgeAll, 3);
  YGNodeStyleSetGap(control->yoga_node(), YGGutterColumn, 2);
  control->style().corner_radius = theme::kBaseCornerRadius + 2;
  control->style().background = palette_.component_background;
  control->style().border = palette_.border;
  control->style().border_width = 1;
  for (const SegmentOption& option : options) {
    const std::string segment_id = std::string(id_prefix) + option.key;
    const bool selected = option.key == selected_key;
    const bool hovered = hovered_node_id_ == segment_id;
    ui::Node* segment = control->AddChild(segment_id);
    MakeRow(segment->yoga_node());
    YGNodeStyleSetHeight(segment->yoga_node(), theme::kControlHeight - 4);
    YGNodeStyleSetPadding(segment->yoga_node(), YGEdgeHorizontal,
                          theme::Spacing(3));
    segment->style().corner_radius = theme::kBaseCornerRadius;
    segment->style().cursor = CursorKind::kHand;
    if (selected) {
      segment->style().background = palette_.background_primary;
      segment->style().border = palette_.border;
      segment->style().border_width = 1;
      segment->style().shadows = palette_.shadow_small;
    } else {
      segment->style().background =
          InteractiveFill(segment_id, theme::kTransparent,
                          palette_.overlay_hover, palette_.overlay_pressed);
    }
    BuildLabel(
        segment, option.label,
        selected ? label_strong_font_.get() : label_font_.get(),
        selected || hovered ? palette_.text_primary : palette_.text_secondary);
  }
}

void App::BuildStepperRow(ui::Node* card, std::string_view label,
                          std::string_view description,
                          std::string_view setting_key,
                          std::string value_text) {
  ui::Node* row = BuildSettingsRow(card, label, description);
  ui::Node* stepper = row->AddChild();
  MakeRow(stepper->yoga_node());
  YGNodeStyleSetFlexShrink(stepper->yoga_node(), 0);
  YGNodeStyleSetHeight(stepper->yoga_node(), theme::kControlHeight);
  stepper->style().corner_radius = theme::kBaseCornerRadius;
  stepper->style().border = palette_.border;
  stepper->style().border_width = 1;

  std::string id_prefix =
      std::string(kStepperIdPrefix) + std::string(setting_key) + ":";
  BuildIconButton(stepper, id_prefix + "decrease", ui::IconKind::kMinus, 30);
  AddVerticalRule(stepper, palette_.border);
  ui::Node* value_box = stepper->AddChild();
  YGNodeStyleSetWidth(value_box->yoga_node(), 64);
  YGNodeStyleSetAlignItems(value_box->yoga_node(), YGAlignCenter);
  BuildLabel(value_box, std::move(value_text), monospace_body_font_.get(),
             palette_.text_primary);
  AddVerticalRule(stepper, palette_.border);
  BuildIconButton(stepper, id_prefix + "increase", ui::IconKind::kPlus, 30);
}

void App::BuildToggleRow(ui::Node* card, std::string_view label,
                         std::string_view description,
                         std::string_view setting_key, bool enabled) {
  const std::string toggle_id =
      std::string(kToggleIdPrefix) + std::string(setting_key);
  ui::Node* row = BuildSettingsRow(card, label, description);
  ui::Node* track = row->AddChild(toggle_id);
  YGNodeStyleSetFlexDirection(track->yoga_node(), YGFlexDirectionRow);
  YGNodeStyleSetAlignItems(track->yoga_node(), YGAlignCenter);
  YGNodeStyleSetFlexShrink(track->yoga_node(), 0);
  YGNodeStyleSetWidth(track->yoga_node(), 36);
  YGNodeStyleSetHeight(track->yoga_node(), 20);
  YGNodeStyleSetPadding(track->yoga_node(), YGEdgeAll, 2);
  YGNodeStyleSetJustifyContent(track->yoga_node(),
                               enabled ? YGJustifyFlexEnd : YGJustifyFlexStart);
  track->style().corner_radius = 10;
  track->style().cursor = CursorKind::kHand;
  track->style().background = enabled ? palette_.accent : palette_.toggle_off;

  ui::Node* knob = track->AddChild();
  YGNodeStyleSetWidth(knob->yoga_node(), 16);
  YGNodeStyleSetHeight(knob->yoga_node(), 16);
  knob->style().corner_radius = 8;
  knob->style().background = palette_.toggle_knob;
  knob->style().shadows = palette_.shadow_small;
}

bool App::IsTextField(std::string_view node_id) const {
  return node_id == kComposerFieldId || node_id == kSystemPromptFieldId ||
         node_id == kRenameFieldId;
}

void App::HandleClick(const std::string& node_id) {
  const bool delete_was_pending = delete_confirmation_pending_;
  delete_confirmation_pending_ = false;
  if (node_id == "rename") {
    StartRenamingConversation();
  } else if (node_id == "rename-save") {
    FinishRenamingConversation(true);
  } else if (node_id == "rename-cancel") {
    FinishRenamingConversation(false);
  } else if (node_id == "delete") {
    if (delete_was_pending) {
      DeleteActiveConversation();
    } else {
      delete_confirmation_pending_ = true;
    }
  } else if (node_id == "jump-to-latest") {
    message_scroll_pinned_to_bottom_ = true;
  } else if (node_id == "regenerate") {
    RegenerateLastReply();
  } else if (node_id == "edit-message") {
    EditLastUserMessage();
  } else if (StartsWith(node_id, kCopyMessageIdPrefix)) {
    const size_t index =
        std::stoul(node_id.substr(kCopyMessageIdPrefix.size()));
    const auto& messages = conversations_[active_conversation_index_].messages;
    if (index < messages.size()) CopyToClipboard(messages[index].text, node_id);
  } else if (StartsWith(node_id, kCopyCodeIdPrefix)) {
    const size_t index = std::stoul(node_id.substr(kCopyCodeIdPrefix.size()));
    if (index < copyable_code_blocks_.size()) {
      CopyToClipboard(copyable_code_blocks_[index], node_id);
    }
  } else if (node_id == kMinimizeButtonId) {
    window_.Minimize();
  } else if (node_id == kMaximizeButtonId) {
    window_.ToggleMaximize();
  } else if (node_id == kCloseButtonId) {
    window_.Close();
  } else if (node_id == "new-chat") {
    StartNewConversation();
  } else if (StartsWith(node_id, kConversationIdPrefix)) {
    size_t index = std::stoul(node_id.substr(kConversationIdPrefix.size()));
    if (index < conversations_.size()) {
      FinishRenamingConversation(true);
      active_conversation_index_ = index;
      message_scroll_pinned_to_bottom_ = true;
    }
  } else if (node_id == "settings-toggle") {
    OpenSettings(settings_section_);
  } else if (node_id == "settings-back") {
    CloseSettings();
  } else if (node_id == "model-picker" || node_id == "empty-open-settings") {
    OpenSettings(SettingsSection::kModel);
  } else if (StartsWith(node_id, kSettingsSectionIdPrefix)) {
    std::string_view key =
        std::string_view(node_id).substr(kSettingsSectionIdPrefix.size());
    if (key == "model") OpenSettings(SettingsSection::kModel);
    if (key == "sampling") OpenSettings(SettingsSection::kSampling);
    if (key == "appearance") OpenSettings(SettingsSection::kAppearance);
    if (key == "shortcuts") OpenSettings(SettingsSection::kShortcuts);
  } else if (node_id == "model-choose") {
    ChooseModelFile();
  } else if (StartsWith(node_id, kRecentModelIdPrefix)) {
    const size_t index =
        std::stoul(node_id.substr(kRecentModelIdPrefix.size()));
    if (index < recent_model_paths_.size()) {
      generation_settings_.model_path = recent_model_paths_[index];
      RequestModelLoad();
    }
  } else if (node_id == "model-clear") {
    generation_settings_.model_path.clear();
    RequestModelLoad();
  } else if (node_id == "stop") {
    StopGeneration();
  } else if (StartsWith(node_id, kThemeIdPrefix)) {
    std::string_view key =
        std::string_view(node_id).substr(kThemeIdPrefix.size());
    if (key == "system") SetThemePreference(ThemePreference::kSystem);
    if (key == "light") SetThemePreference(ThemePreference::kLight);
    if (key == "dark") SetThemePreference(ThemePreference::kDark);
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

void App::OpenSettings(SettingsSection section) {
  if (!settings_page_open_ || settings_section_ != section) {
    settings_scroll_offset_ = 0;
  }
  settings_page_open_ = true;
  settings_section_ = section;
  focused_field_id_.clear();
}

void App::CloseSettings() {
  SaveSettingsIfChanged();
  RequestModelLoad();
  settings_page_open_ = false;
  focused_field_id_ = std::string(kComposerFieldId);
}

void App::ChooseModelFile() {
  pressed_node_id_.clear();
  hovered_node_id_.clear();
  std::optional<std::string> chosen_path =
      ShowOpenFileDialog(window_.hwnd(), L"GGUF models (*.gguf)", L"*.gguf");
  if (chosen_path) {
    generation_settings_.model_path = std::move(*chosen_path);
    RequestModelLoad();
  }
  Invalidate();
}

void App::SetThemePreference(ThemePreference preference) {
  theme_preference_ = preference;
  ApplySystemAppearance();
}

void App::StepSetting(std::string_view setting_key, int direction) {
  GenerationSettings& settings = generation_settings_;
  const int hardware_threads =
      std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
  if (setting_key == "repeat-penalty") {
    settings.repeat_penalty = std::clamp(
        std::round((settings.repeat_penalty + 0.05f * direction) * 100) / 100,
        1.0f, 2.0f);
  } else if (setting_key == "gpu-layers") {
    settings.gpu_layer_count =
        std::clamp(settings.gpu_layer_count + 4 * direction, 0,
                   GenerationSettings::kAllGpuLayers);
  } else if (setting_key == "threads") {
    settings.thread_count =
        std::clamp(settings.thread_count + direction,
                   GenerationSettings::kAutomaticThreadCount, hardware_threads);
  } else if (setting_key == "temperature") {
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
  GenerationSettings defaults;
  defaults.model_path = generation_settings_.model_path;
  return generation_settings_ == defaults;
}

void App::ResetSettingsToDefaults() {
  std::string kept_model_path = generation_settings_.model_path;
  generation_settings_ = GenerationSettings();
  generation_settings_.model_path = std::move(kept_model_path);
}

void App::SendComposerMessage() {
  std::string message_text = TrimWhitespace(composer_text_);
  if (message_text.empty() || generating_) return;
  Conversation& conversation = conversations_[active_conversation_index_];
  if (conversation.messages.empty() &&
      conversation.title == kNewConversationTitle) {
    conversation.title = message_text.substr(0, message_text.find('\n'));
  }
  conversation.messages.push_back({MessageRole::kUser, message_text});
  composer_text_.clear();
  composer_selection_ = {};
  composer_history_.Clear();
  message_scroll_pinned_to_bottom_ = true;
  SaveConversationHistory();
  GenerateReply(&conversation);
}

void App::GenerateReply(Conversation* conversation_pointer) {
  Conversation& conversation = *conversation_pointer;
  if (!ModelLoaded()) {
    conversation.messages.push_back(
        {MessageRole::kAssistant,
         std::string(model_state_ == ModelState::kLoading ? kLoadingNotice
                                                          : kNoModelNotice),
         true});
    return;
  }

  std::vector<llm::PromptMessage> prompt;
  const std::string system_prompt =
      TrimWhitespace(generation_settings_.system_prompt);
  if (!system_prompt.empty()) prompt.push_back({"system", system_prompt});
  for (const ChatMessage& message : conversation.messages) {
    if (message.is_notice) continue;
    prompt.push_back({message.role == MessageRole::kUser ? "user" : "assistant",
                      message.text});
  }
  const GenerationSettings& settings = generation_settings_;
  llm::SamplingOptions sampling;
  sampling.temperature = settings.temperature;
  sampling.top_p = settings.top_p;
  sampling.top_k = settings.top_k;
  sampling.repeat_penalty = settings.repeat_penalty;
  sampling.maximum_tokens = settings.maximum_tokens;

  ChatMessage reply{MessageRole::kAssistant};
  reply.is_streaming = true;
  conversation.messages.push_back(std::move(reply));
  generating_ = true;
  generating_conversation_index_ = active_conversation_index_;
  inference_engine_->Generate(std::move(prompt), sampling);
}

void App::RegenerateLastReply() {
  if (generating_) return;
  Conversation& conversation = conversations_[active_conversation_index_];
  std::vector<ChatMessage>& messages = conversation.messages;
  while (!messages.empty() && messages.back().role != MessageRole::kUser) {
    messages.pop_back();
  }
  if (messages.empty()) return;
  message_scroll_pinned_to_bottom_ = true;
  SaveConversationHistory();
  GenerateReply(&conversation);
}

void App::EditLastUserMessage() {
  if (generating_) return;
  std::vector<ChatMessage>& messages =
      conversations_[active_conversation_index_].messages;
  auto last_user = std::find_if(messages.rbegin(), messages.rend(),
                                [](const ChatMessage& message) {
                                  return message.role == MessageRole::kUser;
                                });
  if (last_user == messages.rend()) return;
  composer_history_.Record(composer_text_, composer_selection_, false);
  composer_text_ = last_user->text;
  composer_selection_ = {composer_text_.size(), composer_text_.size()};
  messages.erase(std::prev(last_user.base()), messages.end());
  focused_field_id_ = std::string(kComposerFieldId);
  message_scroll_pinned_to_bottom_ = true;
  SaveConversationHistory();
  ResetCaretBlink();
}

void App::StartRenamingConversation() {
  rename_text_ = conversations_[active_conversation_index_].title;
  rename_selection_ = {rename_text_.size(), 0};
  rename_history_.Clear();
  renaming_conversation_ = true;
  focused_field_id_ = std::string(kRenameFieldId);
  ResetCaretBlink();
}

void App::FinishRenamingConversation(bool keep_changes) {
  if (!renaming_conversation_) return;
  renaming_conversation_ = false;
  if (focused_field_id_ == kRenameFieldId) {
    focused_field_id_ = std::string(kComposerFieldId);
  }
  std::string new_title = TrimWhitespace(rename_text_);
  std::replace(new_title.begin(), new_title.end(), '\n', ' ');
  if (!keep_changes || new_title.empty()) return;
  conversations_[active_conversation_index_].title = std::move(new_title);
  SaveConversationHistory();
}

void App::DeleteActiveConversation() {
  if (generating_ &&
      generating_conversation_index_ == active_conversation_index_) {
    return;
  }
  conversations_.erase(conversations_.begin() + active_conversation_index_);
  if (generating_ &&
      generating_conversation_index_ > active_conversation_index_) {
    --generating_conversation_index_;
  }
  const bool has_empty_chat =
      std::any_of(conversations_.begin(), conversations_.end(),
                  [](const Conversation& conversation) {
                    return conversation.messages.empty();
                  });
  if (!has_empty_chat) {
    conversations_.insert(conversations_.begin(),
                          Conversation{std::string(kNewConversationTitle), {}});
    if (generating_) ++generating_conversation_index_;
    active_conversation_index_ = 0;
  } else {
    active_conversation_index_ =
        std::min(active_conversation_index_, conversations_.size() - 1);
  }
  message_scroll_pinned_to_bottom_ = true;
  SaveConversationHistory();
}

void App::CopyToClipboard(std::string_view text, const std::string& button_id) {
  window_.SetClipboardText(text);
  copied_button_id_ = button_id;
}

void App::StartNewConversation() {
  FinishRenamingConversation(true);
  ClearMessageSelection();
  const Conversation& active = conversations_[active_conversation_index_];
  if (!active.messages.empty()) {
    conversations_.insert(conversations_.begin(),
                          Conversation{std::string(kNewConversationTitle), {}});
    active_conversation_index_ = 0;
    if (generating_) ++generating_conversation_index_;
  }
  composer_text_.clear();
  composer_selection_ = {};
  composer_history_.Clear();
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
  if (dragging_message_selection_) {
    std::optional<MessageTextPoint> point = MessageTextPointAt(
        x / device_pixels_per_point_, y / device_pixels_per_point_);
    if (point && *point != message_selection_focus_) {
      message_selection_focus_ = *point;
      Invalidate();
    }
    return;
  }
  if (dragging_text_selection_) {
    PlaceCaretAtPoint(x / device_pixels_per_point_,
                      y / device_pixels_per_point_, true);
    Invalidate();
    return;
  }
  const ui::Node* hit = HitTestDevicePoint(x, y);
  std::string hit_id = hit ? hit->id() : std::string();
  if (hit_id != hovered_node_id_) {
    if (hit_id != copied_button_id_) copied_button_id_.clear();
    hovered_node_id_ = std::move(hit_id);
    Invalidate();
  }
}

std::optional<TextFieldEditor> App::FocusedEditor() {
  TextFieldEditor editor;
  if (focused_field_id_ == kComposerFieldId) {
    editor = {&composer_text_, &composer_selection_, &composer_history_};
  } else if (focused_field_id_ == kSystemPromptFieldId) {
    editor = {&generation_settings_.system_prompt, &system_prompt_selection_,
              &system_prompt_history_};
  } else if (focused_field_id_ == kRenameFieldId && renaming_conversation_) {
    editor = {&rename_text_, &rename_selection_, &rename_history_};
  } else {
    return std::nullopt;
  }
  ui::ClampSelection(*editor.text, editor.selection);
  return editor;
}

const ui::Node* App::FocusedTextNode() const {
  if (!interface_root_ || focused_field_id_.empty()) return nullptr;
  return interface_root_->FindById(focused_field_id_ +
                                   std::string(kTextNodeSuffix));
}

std::vector<ui::TextLine> App::FocusedFieldLines() const {
  const ui::Node* text_node = FocusedTextNode();
  if (!text_node || !text_node->style().font) return {{0, 0, 0}};
  const ui::NodeStyle& style = text_node->style();
  return ui::WrapForEditing(
      *style.font, style.text,
      ui::TextWrapWidth(style, text_node->bounds().width));
}

void App::PlaceCaretAtPoint(float logical_x, float logical_y, bool extend) {
  std::optional<TextFieldEditor> editor = FocusedEditor();
  const ui::Node* text_node = FocusedTextNode();
  if (!editor || !text_node) return;
  const ui::Rect& bounds = text_node->bounds();
  const size_t byte = ui::ByteAtPoint(*text_node->style().font, *editor->text,
                                      FocusedFieldLines(), logical_x - bounds.x,
                                      logical_y - bounds.y);
  ui::MoveCaret(editor->selection, byte, extend);
  preferred_caret_x_ = -1;
  ResetCaretBlink();
}

void App::EditFocusedText(std::string_view replacement, bool is_typing) {
  std::optional<TextFieldEditor> editor = FocusedEditor();
  if (!editor) return;
  if (replacement.empty() && editor->selection->empty()) return;
  editor->history->Record(*editor->text, *editor->selection, is_typing);
  ui::ReplaceSelection(editor->text, editor->selection, replacement);
  preferred_caret_x_ = -1;
  ResetCaretBlink();
}

bool App::HandleEditingKey(UINT virtual_key, bool control_down,
                           bool shift_down) {
  std::optional<TextFieldEditor> editor = FocusedEditor();
  if (!editor) return false;
  std::string& text = *editor->text;
  ui::TextSelection& selection = *editor->selection;
  const bool keeps_preferred_x = virtual_key == VK_UP || virtual_key == VK_DOWN;
  if (!keeps_preferred_x) preferred_caret_x_ = -1;

  switch (virtual_key) {
    case VK_LEFT:
      if (!shift_down && !selection.empty()) {
        ui::MoveCaret(&selection, selection.begin(), false);
      } else {
        ui::MoveCaret(&selection,
                      control_down
                          ? ui::PreviousWordStart(text, selection.caret)
                          : ui::PreviousCharacter(text, selection.caret),
                      shift_down);
      }
      break;
    case VK_RIGHT:
      if (!shift_down && !selection.empty()) {
        ui::MoveCaret(&selection, selection.end(), false);
      } else {
        ui::MoveCaret(&selection,
                      control_down ? ui::NextWordEnd(text, selection.caret)
                                   : ui::NextCharacter(text, selection.caret),
                      shift_down);
      }
      break;
    case VK_HOME:
      ui::MoveCaret(&selection,
                    control_down ? 0
                                 : ui::VisualLineStart(FocusedFieldLines(),
                                                       selection.caret),
                    shift_down);
      break;
    case VK_END:
      ui::MoveCaret(&selection,
                    control_down ? text.size()
                                 : ui::VisualLineEnd(text, FocusedFieldLines(),
                                                     selection.caret),
                    shift_down);
      break;
    case VK_UP:
    case VK_DOWN: {
      const ui::Node* text_node = FocusedTextNode();
      if (!text_node) return true;
      const ui::Font& font = *text_node->style().font;
      const std::vector<ui::TextLine> lines = FocusedFieldLines();
      const ui::CaretLocation location =
          ui::LocateCaret(font, text, lines, selection.caret);
      if (preferred_caret_x_ < 0) preferred_caret_x_ = location.x;
      const bool moving_up = virtual_key == VK_UP;
      size_t target_byte = 0;
      if (moving_up && location.line_index == 0) {
        target_byte = 0;
      } else if (!moving_up && location.line_index + 1 >= lines.size()) {
        target_byte = text.size();
      } else {
        const size_t target_line =
            moving_up ? location.line_index - 1 : location.line_index + 1;
        target_byte = ui::ByteAtPoint(
            font, text, lines, preferred_caret_x_,
            (static_cast<float>(target_line) + 0.5f) * font.line_height());
      }
      ui::MoveCaret(&selection, target_byte, shift_down);
      break;
    }
    case VK_BACK:
      if (selection.empty()) {
        selection.anchor = control_down
                               ? ui::PreviousWordStart(text, selection.caret)
                               : ui::PreviousCharacter(text, selection.caret);
      }
      EditFocusedText({}, false);
      return true;
    case VK_DELETE:
      if (selection.empty()) {
        selection.anchor = control_down
                               ? ui::NextWordEnd(text, selection.caret)
                               : ui::NextCharacter(text, selection.caret);
      }
      EditFocusedText({}, false);
      return true;
    case VK_RETURN:
      if (focused_field_id_ == kComposerFieldId && !shift_down) {
        SendComposerMessage();
      } else if (focused_field_id_ == kRenameFieldId) {
        FinishRenamingConversation(true);
      } else {
        EditFocusedText("\n", false);
      }
      return true;
    default:
      if (!control_down) return false;
      if (virtual_key == 'A') {
        selection.anchor = 0;
        selection.caret = text.size();
      } else if (virtual_key == 'C' || virtual_key == 'X') {
        if (selection.empty()) return true;
        window_.SetClipboardText(ui::SelectedText(text, selection));
        if (virtual_key == 'X') EditFocusedText({}, false);
      } else if (virtual_key == 'V') {
        EditFocusedText(NormalizeLineEndings(window_.ClipboardText()), false);
      } else if (virtual_key == 'Z' && !shift_down) {
        editor->history->Undo(&text, &selection);
      } else if (virtual_key == 'Y' || virtual_key == 'Z') {
        editor->history->Redo(&text, &selection);
      } else {
        return false;
      }
      break;
  }
  ResetCaretBlink();
  return true;
}

void App::OnMouseLeave() {
  if (!hovered_node_id_.empty()) {
    hovered_node_id_.clear();
    Invalidate();
  }
}

void App::OnMouseDown(float x, float y, bool shift_down) {
  const ui::Node* hit = HitTestDevicePoint(x, y);
  std::string hit_id = hit ? hit->id() : std::string();
  if (hit_id.ends_with(kTextNodeSuffix)) {
    hit_id.resize(hit_id.size() - kTextNodeSuffix.size());
  }
  const bool keeps_rename_open = hit_id == kRenameFieldId ||
                                 hit_id == "rename-save" ||
                                 hit_id == "rename-cancel";
  if (renaming_conversation_ && !keeps_rename_open) {
    FinishRenamingConversation(true);
  }
  const bool hits_message_text = StartsWith(hit_id, kSelectableIdPrefix);
  if (hits_message_text) {
    std::optional<MessageTextPoint> point = MessageTextPointAt(
        x / device_pixels_per_point_, y / device_pixels_per_point_);
    if (point) {
      if (!shift_down) message_selection_anchor_ = *point;
      message_selection_focus_ = *point;
      dragging_message_selection_ = true;
    }
  } else {
    ClearMessageSelection();
  }
  if (IsTextField(hit_id)) {
    const bool extend = shift_down && focused_field_id_ == hit_id;
    focused_field_id_ = hit_id;
    PlaceCaretAtPoint(x / device_pixels_per_point_,
                      y / device_pixels_per_point_, extend);
    dragging_text_selection_ = true;
  } else if (hit_id != "send") {
    focused_field_id_.clear();
  }
  pressed_node_id_ =
      IsTextField(hit_id) || hits_message_text ? std::string() : hit_id;
  hovered_node_id_ = hit_id;
  ResetCaretBlink();
}

void App::OnMouseUp(float x, float y) {
  dragging_text_selection_ = false;
  dragging_message_selection_ = false;
  const ui::Node* hit = HitTestDevicePoint(x, y);
  std::string released_id = hit ? hit->id() : std::string();
  std::string pressed_id = std::move(pressed_node_id_);
  pressed_node_id_.clear();
  if (!pressed_id.empty() && pressed_id == released_id) {
    HandleClick(pressed_id);
  }
  SaveSettingsIfChanged();
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
  EditFocusedText(utf8_text, true);
}

void App::OnKey(UINT virtual_key, bool control_down, bool shift_down) {
  if (control_down && virtual_key == 'N') {
    StartNewConversation();
    ResetCaretBlink();
    return;
  }
  if (control_down && virtual_key == VK_OEM_COMMA) {
    if (settings_page_open_) {
      CloseSettings();
    } else {
      OpenSettings(settings_section_);
    }
    ResetCaretBlink();
    return;
  }
  if (control_down && !FocusedEditor()) {
    if (virtual_key == 'C' && HasMessageSelection()) {
      window_.SetClipboardText(SelectedMessageText());
      return;
    }
    if (virtual_key == 'A' && !settings_page_open_ &&
        !selectable_texts_.empty()) {
      message_selection_anchor_ = {};
      message_selection_focus_ = {selectable_texts_.size() - 1,
                                  selectable_texts_.back().text.size()};
      Invalidate();
      return;
    }
  }
  if (virtual_key == VK_ESCAPE) {
    delete_confirmation_pending_ = false;
    ClearMessageSelection();
    if (renaming_conversation_) {
      FinishRenamingConversation(false);
    } else if (settings_page_open_ && focused_field_id_.empty()) {
      CloseSettings();
    } else {
      focused_field_id_.clear();
    }
    Invalidate();
    return;
  }
  if (HandleEditingKey(virtual_key, control_down, shift_down)) Invalidate();
}

void App::OnCaretTimer() {
  if (focused_field_id_.empty()) return;
  caret_visible_ = !caret_visible_;
  Invalidate();
}

WindowRegion App::RegionAt(float x, float y) {
  if (y / device_pixels_per_point_ >= theme::kTitleBarHeight) {
    return WindowRegion::kClient;
  }
  const ui::Node* node = HitTestDevicePoint(x, y);
  return node ? WindowRegion::kClient : WindowRegion::kCaption;
}

void App::OnActivationChanged(bool active) {
  window_active_ = active;
  Invalidate();
}

CursorKind App::CursorAt(float x, float y) {
  const ui::Node* hit = HitTestDevicePoint(x, y);
  return hit ? hit->style().cursor : CursorKind::kArrow;
}

}
