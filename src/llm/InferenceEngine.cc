#include "llm/InferenceEngine.h"

#include <ggml-backend.h>
#include <llama.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string_view>
#include <utility>

#include "platform/Platform.h"

namespace lale::llm {

namespace {

constexpr int kPenaltyWindowTokens = 64;

struct TemplateFamily {
  const char* marker_token;
  const char* template_name;
};

constexpr TemplateFamily kTemplateFamilies[] = {
    {"<|start_header_id|>", "llama3"}, {"<start_of_turn>", "gemma"},
    {"<|im_start|>", "chatml"},        {"<|user|>", "phi3"},
    {"[INST]", "mistral-v7"},
};

constexpr std::string_view kEndOfTurnMarkers[] = {
    "<|im_end|>",    "<|im_start|>",
    "<|eot_id|>",    "<|start_header_id|>",
    "<end_of_turn>", "<start_of_turn>",
    "<|end|>",       "<|user|>",
    "<|endoftext|>", "</s>",
    "[INST]",
};

bool HasSpecialToken(const llama_vocab* vocab, const char* text) {
  llama_token tokens[4];
  const int length = static_cast<int>(std::strlen(text));
  return llama_tokenize(vocab, text, length, tokens, 4, false, true) == 1;
}

size_t FindEndOfTurnMarker(std::string_view text) {
  size_t earliest = std::string_view::npos;
  for (std::string_view marker : kEndOfTurnMarkers) {
    earliest = std::min(earliest, text.find(marker));
  }
  return earliest;
}

size_t LongestMarkerLength() {
  size_t longest = 0;
  for (std::string_view marker : kEndOfTurnMarkers) {
    longest = std::max(longest, marker.size());
  }
  return longest;
}

size_t SafePublishEnd(const std::string& text, size_t published) {
  const size_t held_back = LongestMarkerLength() - 1;
  if (text.size() <= published + held_back) return published;
  size_t end = text.size() - held_back;
  while (end > published &&
         (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
    --end;
  }
  return end;
}

float SecondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<float>(std::chrono::steady_clock::now() - start)
      .count();
}

void DiscardLog(ggml_log_level, const char*, void*) {}

bool AbortRequested(void* cancel_flag) {
  return static_cast<std::atomic<bool>*>(cancel_flag)->load();
}

size_t CompleteUtf8Length(const std::string& bytes) {
  size_t length = bytes.size();
  size_t index = length;
  size_t continuation_count = 0;
  while (index > 0 && continuation_count < 4) {
    unsigned char byte = static_cast<unsigned char>(bytes[index - 1]);
    if ((byte & 0xC0) != 0x80) {
      size_t expected = 1;
      if ((byte & 0xE0) == 0xC0) expected = 2;
      if ((byte & 0xF0) == 0xE0) expected = 3;
      if ((byte & 0xF8) == 0xF0) expected = 4;
      return continuation_count + 1 >= expected ? length : index - 1;
    }
    ++continuation_count;
    --index;
  }
  return length;
}

llama_sampler* MakeSampler(const llama_vocab* vocab,
                           const SamplingOptions& sampling) {
  llama_sampler* chain =
      llama_sampler_chain_init(llama_sampler_chain_default_params());
  llama_sampler_chain_add(
      chain, llama_sampler_init_penalties(llama_vocab_n_tokens(vocab),
                                          kPenaltyWindowTokens,
                                          sampling.repeat_penalty, 0, 0));
  if (sampling.temperature <= 0) {
    llama_sampler_chain_add(chain, llama_sampler_init_greedy());
    return chain;
  }
  llama_sampler_chain_add(chain, llama_sampler_init_top_k(sampling.top_k));
  llama_sampler_chain_add(chain, llama_sampler_init_top_p(sampling.top_p, 1));
  llama_sampler_chain_add(chain, llama_sampler_init_temp(sampling.temperature));
  llama_sampler_chain_add(chain, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
  return chain;
}

}

InferenceEngine::InferenceEngine(std::function<void()> wake_owner)
    : wake_owner_(std::move(wake_owner)) {
  llama_log_set(DiscardLog, nullptr);
  worker_ = std::thread(&InferenceEngine::RunWorker, this);
}

void InferenceEngine::LoadBackendsOnWorker() {
  if (backends_loaded_) return;
  const std::string backend_directory = LoadedLibraryDirectory("ggml").string();
  if (backend_directory.empty()) {
    ggml_backend_load_all();
  } else {
    ggml_backend_load_all_from_path(backend_directory.c_str());
  }
  llama_backend_init();
  gpu_offload_supported_ = llama_supports_gpu_offload();
  backends_loaded_ = true;
}

InferenceEngine::~InferenceEngine() {
  cancel_requested_ = true;
  {
    std::lock_guard lock(command_mutex_);
    pending_commands_.clear();
    pending_commands_.push_back({Command::Kind::kStop});
  }
  command_available_.notify_one();
  worker_.join();
  if (backends_loaded_) llama_backend_free();
}

void InferenceEngine::LoadModel(const ModelOptions& options) {
  cancel_requested_ = true;
  Command command{Command::Kind::kLoad};
  command.model_options = options;
  Enqueue(std::move(command));
}

void InferenceEngine::UnloadModel() {
  cancel_requested_ = true;
  Enqueue({Command::Kind::kUnload});
}

void InferenceEngine::Generate(std::vector<PromptMessage> messages,
                               const SamplingOptions& sampling) {
  cancel_requested_ = false;
  Command command{Command::Kind::kGenerate};
  command.messages = std::move(messages);
  command.sampling = sampling;
  Enqueue(std::move(command));
}

void InferenceEngine::CancelGeneration() { cancel_requested_ = true; }

std::vector<EngineEvent> InferenceEngine::TakeEvents() {
  std::lock_guard lock(event_mutex_);
  return std::exchange(pending_events_, {});
}

void InferenceEngine::Enqueue(Command command) {
  {
    std::lock_guard lock(command_mutex_);
    pending_commands_.push_back(std::move(command));
  }
  command_available_.notify_one();
}

void InferenceEngine::Publish(EngineEventKind kind, std::string text) {
  PublishEvent({kind, std::move(text)});
}

void InferenceEngine::PublishEvent(EngineEvent event) {
  {
    std::lock_guard lock(event_mutex_);
    pending_events_.push_back(std::move(event));
  }
  wake_owner_();
}

void InferenceEngine::PublishLoadProgress(float progress) {
  PublishEvent({EngineEventKind::kModelLoadProgress, {}, progress});
}

bool InferenceEngine::ReportLoadProgress(float progress, void* engine_pointer) {
  auto* engine = static_cast<InferenceEngine*>(engine_pointer);
  const int percent = static_cast<int>(progress * 100);
  if (percent != engine->last_reported_load_percent_) {
    engine->last_reported_load_percent_ = percent;
    engine->PublishLoadProgress(progress);
  }
  return !engine->cancel_requested_;
}

void InferenceEngine::RunWorker() {
  LowerCurrentThreadPriority();
  while (true) {
    Command command;
    {
      std::unique_lock lock(command_mutex_);
      command_available_.wait(lock,
                              [this] { return !pending_commands_.empty(); });
      command = std::move(pending_commands_.front());
      pending_commands_.pop_front();
    }
    switch (command.kind) {
      case Command::Kind::kLoad:
        LoadOnWorker(command.model_options);
        break;
      case Command::Kind::kUnload:
        UnloadOnWorker();
        Publish(EngineEventKind::kModelUnloaded);
        break;
      case Command::Kind::kGenerate:
        GenerateOnWorker(command.messages, command.sampling);
        break;
      case Command::Kind::kStop:
        UnloadOnWorker();
        return;
    }
  }
}

void InferenceEngine::UnloadOnWorker() {
  cached_tokens_.clear();
  if (context_) llama_free(context_);
  if (model_) llama_model_free(model_);
  context_ = nullptr;
  model_ = nullptr;
}

void InferenceEngine::LoadOnWorker(const ModelOptions& options) {
  UnloadOnWorker();
  LoadBackendsOnWorker();
  cancel_requested_ = false;
  llama_model_params model_parameters = llama_model_default_params();
  model_parameters.n_gpu_layers = options.gpu_layer_count;
  model_parameters.progress_callback = ReportLoadProgress;
  model_parameters.progress_callback_user_data = this;
  last_reported_load_percent_ = -1;
  model_ =
      llama_model_load_from_file(options.model_path.c_str(), model_parameters);
  if (!model_ && cancel_requested_) {
    Publish(EngineEventKind::kModelLoadFailed, "Loading was cancelled.");
    return;
  }
  if (!model_) {
    Publish(EngineEventKind::kModelLoadFailed,
            "Could not load this file. It may be damaged or not a GGUF "
            "model.");
    return;
  }
  llama_context_params context_parameters = llama_context_default_params();
  context_parameters.n_ctx = static_cast<uint32_t>(options.context_size);
  context_parameters.n_batch = static_cast<uint32_t>(options.context_size);
  if (options.thread_count > 0) {
    context_parameters.n_threads = options.thread_count;
    context_parameters.n_threads_batch = options.thread_count;
  }
  context_parameters.abort_callback = AbortRequested;
  context_parameters.abort_callback_data = &cancel_requested_;
  context_ = llama_init_from_model(model_, context_parameters);
  if (!context_) {
    UnloadOnWorker();
    Publish(EngineEventKind::kModelLoadFailed,
            "The model loaded, but there was not enough memory for this "
            "context size.");
    return;
  }
  Publish(EngineEventKind::kModelLoaded);
}

std::string InferenceEngine::FormatPrompt(
    const std::vector<PromptMessage>& messages) const {
  std::vector<llama_chat_message> chat_messages;
  chat_messages.reserve(messages.size());
  for (const PromptMessage& message : messages) {
    chat_messages.push_back({message.role.c_str(), message.content.c_str()});
  }
  auto apply_template = [&](const char* chat_template) -> std::string {
    std::vector<char> formatted(4096);
    int length = llama_chat_apply_template(
        chat_template, chat_messages.data(), chat_messages.size(), true,
        formatted.data(), static_cast<int32_t>(formatted.size()));
    if (length > static_cast<int>(formatted.size())) {
      formatted.resize(static_cast<size_t>(length));
      length = llama_chat_apply_template(
          chat_template, chat_messages.data(), chat_messages.size(), true,
          formatted.data(), static_cast<int32_t>(formatted.size()));
    }
    if (length < 0) return {};
    return std::string(formatted.data(), static_cast<size_t>(length));
  };
  if (const char* model_template = llama_model_chat_template(model_, nullptr)) {
    std::string prompt = apply_template(model_template);
    if (!prompt.empty()) return prompt;
  }
  const llama_vocab* vocab = llama_model_get_vocab(model_);
  for (const TemplateFamily& family : kTemplateFamilies) {
    if (!HasSpecialToken(vocab, family.marker_token)) continue;
    std::string prompt = apply_template(family.template_name);
    if (!prompt.empty()) return prompt;
  }
  return apply_template("chatml");
}

void InferenceEngine::GenerateOnWorker(
    const std::vector<PromptMessage>& messages,
    const SamplingOptions& sampling) {
  if (!model_ || !context_) {
    Publish(EngineEventKind::kGenerationFailed, "No model is loaded.");
    return;
  }
  const std::string prompt = FormatPrompt(messages);
  if (prompt.empty()) {
    Publish(EngineEventKind::kGenerationFailed,
            "This model's chat template could not be applied.");
    return;
  }

  const llama_vocab* vocab = llama_model_get_vocab(model_);
  const int prompt_length = static_cast<int>(prompt.size());
  const int token_count = -llama_tokenize(vocab, prompt.c_str(), prompt_length,
                                          nullptr, 0, true, true);
  std::vector<llama_token> prompt_tokens(static_cast<size_t>(token_count));
  if (llama_tokenize(vocab, prompt.c_str(), prompt_length, prompt_tokens.data(),
                     token_count, true, true) < 0) {
    Publish(EngineEventKind::kGenerationFailed,
            "The conversation could not be tokenized.");
    return;
  }

  const int context_size = static_cast<int>(llama_n_ctx(context_));
  if (token_count >= context_size) {
    Publish(EngineEventKind::kGenerationFailed,
            "This conversation is longer than the context size. Start a new "
            "chat or raise the context size in Settings.");
    return;
  }

  llama_memory_t memory = llama_get_memory(context_);
  size_t reused_count = 0;
  while (reused_count < cached_tokens_.size() &&
         reused_count < prompt_tokens.size() &&
         cached_tokens_[reused_count] == prompt_tokens[reused_count]) {
    ++reused_count;
  }
  if (reused_count > 0 && reused_count == prompt_tokens.size()) {
    --reused_count;
  }
  if (!llama_memory_seq_rm(memory, 0, static_cast<llama_pos>(reused_count),
                           -1)) {
    llama_memory_clear(memory, true);
    reused_count = 0;
  }
  cached_tokens_.assign(prompt_tokens.begin(),
                        prompt_tokens.begin() + reused_count);
  llama_sampler* sampler = MakeSampler(vocab, sampling);
  llama_batch_ext* batch = llama_batch_ext_init(context_);
  const int batch_capacity = static_cast<int>(llama_n_batch(context_));

  auto process = [&](const llama_token* tokens, int count,
                     llama_pos first_position) {
    llama_batch_ext_clear(batch);
    for (int index = 0; index < count; ++index) {
      const int32_t batch_index =
          llama_batch_ext_add_token(batch, 0, tokens[index]);
      const llama_pos position = first_position + index;
      llama_batch_ext_set_pos(batch, batch_index, &position);
    }
    llama_batch_ext_set_output_logits(batch, count - 1, true);
    return llama_process(context_, LLAMA_PROCESS_TYPE_DECODE, batch);
  };

  PublishEvent({EngineEventKind::kPromptStarted, {}, 0, token_count});
  bool failed = false;
  llama_pos next_position = static_cast<llama_pos>(reused_count);
  for (int offset = static_cast<int>(reused_count);
       offset < token_count && !failed; offset += batch_capacity) {
    const int count = std::min(batch_capacity, token_count - offset);
    failed = process(prompt_tokens.data() + offset, count, next_position) != 0;
    if (!failed) {
      cached_tokens_.insert(cached_tokens_.end(),
                            prompt_tokens.begin() + offset,
                            prompt_tokens.begin() + offset + count);
    }
    next_position += count;
  }

  const auto writing_started = std::chrono::steady_clock::now();
  std::string pending_bytes;
  std::string reply_text;
  size_t published_length = 0;
  int generated_count = 0;
  auto publish_text = [&](size_t end) {
    if (end <= published_length) return;
    PublishEvent({EngineEventKind::kTokenText,
                  reply_text.substr(published_length, end - published_length),
                  0, generated_count, SecondsSince(writing_started)});
    published_length = end;
  };
  while (!failed && !cancel_requested_ &&
         generated_count < sampling.maximum_tokens &&
         next_position < context_size) {
    llama_token token = llama_sampler_sample(sampler, context_, -1);
    if (llama_vocab_is_eog(vocab, token)) break;
    ++generated_count;
    char piece[256];
    const int piece_length =
        llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, false);
    if (piece_length > 0) {
      pending_bytes.append(piece, static_cast<size_t>(piece_length));
      const size_t complete = CompleteUtf8Length(pending_bytes);
      reply_text.append(pending_bytes, 0, complete);
      pending_bytes.erase(0, complete);
      const size_t marker = FindEndOfTurnMarker(reply_text);
      if (marker != std::string::npos) {
        reply_text.resize(marker);
        break;
      }
      publish_text(SafePublishEnd(reply_text, published_length));
    }
    failed = process(&token, 1, next_position) != 0;
    if (!failed) cached_tokens_.push_back(token);
    ++next_position;
  }
  publish_text(reply_text.size());
  if (failed) {
    llama_memory_clear(memory, true);
    cached_tokens_.clear();
  }

  llama_batch_ext_free(batch);
  llama_sampler_free(sampler);
  if (failed && !cancel_requested_) {
    Publish(EngineEventKind::kGenerationFailed,
            "llama.cpp stopped with an error while writing the reply.");
    return;
  }
  PublishEvent({EngineEventKind::kGenerationFinished,
                {},
                0,
                generated_count,
                SecondsSince(writing_started)});
}

}
