#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct llama_model;
struct llama_context;

namespace lale::llm {

struct ModelOptions {
  std::string model_path;
  int gpu_layer_count = 0;
  int thread_count = 0;
  int context_size = 4096;

  bool operator==(const ModelOptions&) const = default;
};

struct SamplingOptions {
  float temperature = 0.8f;
  float top_p = 0.95f;
  int top_k = 40;
  float repeat_penalty = 1.1f;
  int maximum_tokens = 512;
};

struct PromptMessage {
  std::string role;
  std::string content;
};

enum class EngineEventKind {
  kModelLoadProgress,
  kModelLoaded,
  kModelLoadFailed,
  kModelUnloaded,
  kPromptStarted,
  kTokenText,
  kGenerationFinished,
  kGenerationFailed,
};

struct EngineEvent {
  EngineEventKind kind;
  std::string text;
  float progress = 0;
  int token_count = 0;
  float seconds = 0;
};

class InferenceEngine {
 public:
  explicit InferenceEngine(std::function<void()> wake_owner);
  ~InferenceEngine();
  InferenceEngine(const InferenceEngine&) = delete;
  InferenceEngine& operator=(const InferenceEngine&) = delete;

  static bool SupportsGpuOffload();

  void LoadModel(const ModelOptions& options);
  void UnloadModel();
  void Generate(std::vector<PromptMessage> messages,
                const SamplingOptions& sampling);
  void CancelGeneration();
  std::vector<EngineEvent> TakeEvents();

 private:
  struct Command {
    enum class Kind { kLoad, kUnload, kGenerate, kStop } kind;
    ModelOptions model_options;
    std::vector<PromptMessage> messages;
    SamplingOptions sampling;
  };

  void RunWorker();
  void Enqueue(Command command);
  void Publish(EngineEventKind kind, std::string text = {});
  void PublishEvent(EngineEvent event);
  void PublishLoadProgress(float progress);
  static bool ReportLoadProgress(float progress, void* engine);
  void LoadOnWorker(const ModelOptions& options);
  void UnloadOnWorker();
  void GenerateOnWorker(const std::vector<PromptMessage>& messages,
                        const SamplingOptions& sampling);
  std::string FormatPrompt(const std::vector<PromptMessage>& messages) const;

  std::function<void()> wake_owner_;
  std::mutex command_mutex_;
  std::condition_variable command_available_;
  std::deque<Command> pending_commands_;
  std::mutex event_mutex_;
  std::vector<EngineEvent> pending_events_;
  std::atomic<bool> cancel_requested_ = false;
  int last_reported_load_percent_ = -1;

  llama_model* model_ = nullptr;
  llama_context* context_ = nullptr;
  std::thread worker_;
};

}
