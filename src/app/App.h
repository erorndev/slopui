#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/Conversation.h"
#include "app/Settings.h"
#include "app/Theme.h"
#include "gfx/Presenter.h"
#include "llm/InferenceEngine.h"
#include "platform/Window.h"
#include "ui/Font.h"
#include "ui/Markdown.h"
#include "ui/Node.h"
#include "ui/Renderer.h"
#include "ui/TextEditing.h"

namespace lale {

enum class ModelState { kNone, kLoading, kLoaded, kFailed };

enum class ButtonStyle { kPrimary, kSecondary, kLink, kQuiet };

enum class SettingsSection { kModel, kSampling, kAppearance, kShortcuts };

struct TextFieldEditor {
  std::string* text = nullptr;
  ui::TextSelection* selection = nullptr;
  ui::TextEditHistory* history = nullptr;
};

struct SegmentOption {
  std::string key;
  std::string label;
};

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
  void OnMouseDown(float x, float y, bool shift_down) override;
  void OnMouseUp(float x, float y) override;
  void OnMouseWheel(float x, float y, float wheel_steps) override;
  void OnText(const std::string& utf8_text) override;
  void OnKey(UINT virtual_key, bool control_down, bool shift_down) override;
  void OnCaretTimer() override;
  void OnSystemAppearanceChanged() override;
  CursorKind CursorAt(float x, float y) override;
  WindowRegion RegionAt(float x, float y) override;
  void OnWake() override;
  void OnActivationChanged(bool active) override;

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
  void BuildTitleBar(ui::Node* parent);
  void BuildCaptionButton(ui::Node* parent, std::string id, ui::IconKind icon);
  void BuildSidebar(ui::Node* parent);
  ui::Node* BuildSidebarRow(ui::Node* parent, std::string id, std::string label,
                            ui::IconKind leading_icon, bool selected,
                            std::string trailing_hint);
  void BuildChatArea(ui::Node* parent);
  void BuildTopBar(ui::Node* parent);
  void BuildMessageList(ui::Node* parent);
  void BuildMarkdown(ui::Node* parent, std::string_view markdown);
  void BuildMarkdownBlock(ui::Node* parent, const ui::MarkdownBlock& block);
  void BuildMarkdownTable(ui::Node* parent, const ui::MarkdownBlock& block);
  ui::Node* BuildRichText(ui::Node* parent, const ui::MarkdownInlines& inlines,
                          const ui::Font* regular_font,
                          const ui::Font* strong_font, ui::Color color);
  void BuildEmptyState(ui::Node* parent);
  void BuildComposer(ui::Node* parent);
  void BuildSettingsNavigation(ui::Node* parent);
  void BuildSettingsPage(ui::Node* parent);
  void BuildSettingsPageHeading(ui::Node* column, std::string title,
                                std::string description);
  void BuildModelSettings(ui::Node* column);
  void BuildSamplingSettings(ui::Node* column);
  void BuildAppearanceSettings(ui::Node* column);
  void BuildShortcutSettings(ui::Node* column);
  void BuildShortcutRow(ui::Node* card, std::string_view action,
                        std::string keys);
  void BuildSegmentedControl(ui::Node* parent, std::string_view id_prefix,
                             const std::vector<SegmentOption>& options,
                             std::string_view selected_key);
  ui::Node* BuildSettingsSection(ui::Node* parent, std::string caption);
  ui::Node* BuildSettingsRow(ui::Node* card, std::string_view label,
                             std::string_view description);
  void BuildSettingsRowSeparator(ui::Node* card);
  void BuildStepperRow(ui::Node* card, std::string_view label,
                       std::string_view description,
                       std::string_view setting_key, std::string value_text);
  void BuildToggleRow(ui::Node* card, std::string_view label,
                      std::string_view description,
                      std::string_view setting_key, bool enabled);
  ui::Node* BuildIconButton(ui::Node* parent, std::string id, ui::IconKind icon,
                            float size);
  ui::Node* BuildTextButton(ui::Node* parent, std::string id, std::string label,
                            ButtonStyle style, bool enabled = true);
  ui::Node* BuildEditableText(ui::Node* parent, std::string_view field_id,
                              const std::string& text,
                              const ui::TextSelection& selection,
                              std::string placeholder);
  ui::Node* BuildIcon(ui::Node* parent, ui::IconKind icon, ui::Color color,
                      float size);
  ui::Node* BuildKeyHint(ui::Node* parent, std::string keys);
  ui::Node* BuildStatusDot(ui::Node* parent, ui::Color color);
  void BuildProgressBar(ui::Node* parent, float fraction, float height);
  void BuildRecentModels(ui::Node* column);
  void RememberRecentModel(const std::string& model_path);
  std::string ModelLoadingText() const;
  void BuildNotice(ui::Node* parent, const std::string& text);
  ui::Node* BuildLabel(ui::Node* parent, std::string text, const ui::Font* font,
                       ui::Color color, bool wraps_text = false);
  ui::Color InteractiveFill(std::string_view node_id, ui::Color resting,
                            ui::Color hovered, ui::Color pressed) const;
  void LayoutInterface(ui::Node* root);

  void HandleClick(const std::string& node_id);
  void OpenSettings(SettingsSection section);
  void CloseSettings();
  void ChooseModelFile();
  void SetThemePreference(ThemePreference preference);
  void SaveSettingsIfChanged();
  void SaveConversationHistory();
  llm::ModelOptions CurrentModelOptions() const;
  void RequestModelLoad();
  void HandleEngineEvent(const llm::EngineEvent& event);
  bool ModelLoaded() const;
  void StopGeneration();
  void StepSetting(std::string_view setting_key, int direction);
  bool SettingsMatchDefaults() const;
  void ResetSettingsToDefaults();
  void SendComposerMessage();
  void GenerateReply(Conversation* conversation);
  void RegenerateLastReply();
  void EditLastUserMessage();
  void StartRenamingConversation();
  void FinishRenamingConversation(bool keep_changes);
  void DeleteActiveConversation();
  void CopyToClipboard(std::string_view text, const std::string& button_id);
  void BuildMessageActions(ui::Node* parent, size_t message_index,
                           bool is_last_message);
  ui::Node* BuildCopyButton(ui::Node* parent, std::string id);
  void StartNewConversation();
  bool IsTextField(std::string_view node_id) const;
  std::optional<TextFieldEditor> FocusedEditor();
  const ui::Node* FocusedTextNode() const;
  std::vector<ui::TextLine> FocusedFieldLines() const;
  void PlaceCaretAtPoint(float logical_x, float logical_y, bool extend);
  void EditFocusedText(std::string_view replacement, bool is_typing);
  bool HandleEditingKey(UINT virtual_key, bool control_down, bool shift_down);

  Window window_;
  Presenter presenter_;
  ui::Renderer renderer_;
  theme::Palette palette_;
  std::unique_ptr<ui::Font> caption_font_;
  std::unique_ptr<ui::Font> caption_strong_font_;
  std::unique_ptr<ui::Font> label_font_;
  std::unique_ptr<ui::Font> label_strong_font_;
  std::unique_ptr<ui::Font> monospace_caption_font_;
  std::unique_ptr<ui::Font> monospace_body_font_;
  std::unique_ptr<ui::Font> body_font_;
  std::unique_ptr<ui::Font> body_strong_font_;
  std::unique_ptr<ui::Font> headline_font_;
  std::unique_ptr<ui::Font> title_font_;
  std::unique_ptr<ui::Font> body_italic_font_;
  std::unique_ptr<ui::Font> body_strong_italic_font_;
  std::unique_ptr<ui::Font> large_heading_font_;
  std::unique_ptr<ui::Font> medium_heading_font_;
  std::unique_ptr<ui::Node> interface_root_;
  std::unique_ptr<llm::InferenceEngine> inference_engine_;

  std::vector<Conversation> conversations_;
  size_t active_conversation_index_ = 0;
  GenerationSettings generation_settings_;
  std::string composer_text_;
  std::filesystem::path settings_file_path_;
  std::filesystem::path conversations_file_path_;
  StoredSettings saved_settings_;
  bool settings_page_open_ = false;
  SettingsSection settings_section_ = SettingsSection::kModel;
  ThemePreference theme_preference_ = ThemePreference::kSystem;
  ModelState model_state_ = ModelState::kNone;
  std::string model_error_;
  llm::ModelOptions requested_model_options_;
  int pending_model_request_count_ = 0;
  float model_load_progress_ = 0;
  std::vector<std::string> recent_model_paths_;
  bool generating_ = false;
  size_t generating_conversation_index_ = 0;
  std::string focused_field_id_;
  std::string hovered_node_id_;
  std::string pressed_node_id_;
  bool caret_visible_ = true;
  ui::TextSelection composer_selection_;
  ui::TextEditHistory composer_history_;
  ui::TextSelection system_prompt_selection_;
  ui::TextEditHistory system_prompt_history_;
  std::string rename_text_;
  ui::TextSelection rename_selection_;
  ui::TextEditHistory rename_history_;
  bool renaming_conversation_ = false;
  bool delete_confirmation_pending_ = false;
  std::string copied_button_id_;
  std::vector<std::string> copyable_code_blocks_;
  bool dragging_text_selection_ = false;
  float preferred_caret_x_ = -1;
  bool window_active_ = true;
  float message_scroll_offset_ = 0;
  bool message_scroll_pinned_to_bottom_ = true;
  float settings_scroll_offset_ = 0;
  bool needs_render_ = true;
  float device_pixels_per_point_ = 1.0f;
};

}
