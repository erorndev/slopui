#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "app/Theme.h"
#include "gfx/Presenter.h"
#include "platform/Window.h"
#include "ui/Font.h"
#include "ui/Node.h"
#include "ui/Renderer.h"

namespace lale {

enum class MessageRole { kUser, kAssistant };

struct ChatMessage {
  MessageRole role = MessageRole::kUser;
  std::string text;
  bool is_notice = false;
};

struct Conversation {
  std::string title;
  std::vector<ChatMessage> messages;
};

struct GenerationSettings {
  std::string model_path;
  float temperature = 0.8f;
  float top_p = 0.95f;
  int top_k = 40;
  int maximum_tokens = 512;
  int context_size = 4096;
  bool stream_tokens = true;
  std::string system_prompt = "You are a helpful assistant.";
};

enum class ButtonStyle { kPrimary, kSecondary, kPlain };

class App : public WindowHandler {
 public:
  App();
  ~App() override;
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  bool Initialize(int show_command);
  int Run();

  void OnResize(int client_width, int client_height) override;
  void OnDpiChanged(UINT dots_per_inch) override;
  void OnPaint() override;
  void OnMouseMove(float x, float y) override;
  void OnMouseLeave() override;
  void OnMouseDown(float x, float y) override;
  void OnMouseUp(float x, float y) override;
  void OnMouseWheel(float x, float y, float wheel_steps) override;
  void OnText(const std::string& utf8_text) override;
  void OnKey(UINT virtual_key, bool control_down, bool shift_down) override;
  void OnCaretTimer() override;
  void OnSystemAppearanceChanged() override;
  CursorKind CursorAt(float x, float y) override;

 private:
  void LoadFonts();
  void ApplySystemAppearance();
  void RenderFrame();
  float LogicalClientWidth() const;
  float LogicalClientHeight() const;
  const ui::Node* HitTestDevicePoint(float device_x, float device_y) const;
  void Invalidate();
  void ResetCaretBlink();

  std::unique_ptr<ui::Node> BuildInterface();
  void BuildSidebar(ui::Node* parent);
  ui::Node* BuildSidebarRow(ui::Node* parent, std::string id, std::string label,
                            ui::IconKind leading_icon, bool selected,
                            std::string trailing_hint);
  void BuildChatArea(ui::Node* parent);
  void BuildTopBar(ui::Node* parent);
  void BuildMessageList(ui::Node* parent);
  void BuildEmptyState(ui::Node* parent);
  void BuildComposer(ui::Node* parent);
  void BuildSettingsPanel(ui::Node* parent);
  ui::Node* BuildSettingsSection(ui::Node* parent, std::string caption);
  void BuildSettingsRowSeparator(ui::Node* card);
  void BuildStepperRow(ui::Node* card, std::string_view label,
                       std::string_view setting_key, std::string value_text);
  void BuildToggleRow(ui::Node* card, std::string_view label,
                      std::string_view setting_key, bool enabled);
  ui::Node* BuildIconButton(ui::Node* parent, std::string id, ui::IconKind icon,
                            float size);
  ui::Node* BuildTextButton(ui::Node* parent, std::string id, std::string label,
                            ButtonStyle style, bool enabled = true);
  ui::Node* BuildIcon(ui::Node* parent, ui::IconKind icon, ui::Color color,
                      float size);
  ui::Node* BuildLabel(ui::Node* parent, std::string text, const ui::Font* font,
                       ui::Color color, bool wraps_text = false);
  ui::Color InteractiveFill(std::string_view node_id, ui::Color resting,
                            ui::Color hovered, ui::Color pressed) const;
  void LayoutInterface(ui::Node* root);

  void HandleClick(const std::string& node_id);
  void StepSetting(std::string_view setting_key, int direction);
  bool SettingsMatchDefaults() const;
  void ResetSettingsToDefaults();
  void SendComposerMessage();
  void StartNewConversation();
  std::string* FocusedText();
  bool IsTextField(std::string_view node_id) const;

  Window window_;
  Presenter presenter_;
  ui::Renderer renderer_;
  theme::Palette palette_;
  std::unique_ptr<ui::Font> caption_font_;
  std::unique_ptr<ui::Font> caption_strong_font_;
  std::unique_ptr<ui::Font> body_font_;
  std::unique_ptr<ui::Font> body_strong_font_;
  std::unique_ptr<ui::Font> headline_font_;
  std::unique_ptr<ui::Font> title_font_;
  std::unique_ptr<ui::Node> interface_root_;

  std::vector<Conversation> conversations_;
  size_t active_conversation_index_ = 0;
  GenerationSettings generation_settings_;
  std::string composer_text_;
  bool settings_panel_open_ = false;
  std::string focused_field_id_;
  std::string hovered_node_id_;
  std::string pressed_node_id_;
  bool caret_visible_ = true;
  float message_scroll_offset_ = 0;
  bool message_scroll_pinned_to_bottom_ = true;
  float settings_scroll_offset_ = 0;
  bool needs_render_ = true;
  float device_pixels_per_point_ = 1.0f;
};

}
