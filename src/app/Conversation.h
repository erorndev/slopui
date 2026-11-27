#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace lale {

enum class MessageRole { kUser, kAssistant };

struct ChatMessage {
  MessageRole role = MessageRole::kUser;
  std::string text;
  bool is_notice = false;
  bool is_streaming = false;
  int prompt_token_count = 0;
  int generated_token_count = 0;
  float generation_seconds = 0;
};

struct Conversation {
  std::string title;
  std::vector<ChatMessage> messages;
};

std::filesystem::path ConversationsFilePath();
std::vector<Conversation> LoadConversations(
    const std::filesystem::path& file_path);
bool SaveConversations(const std::vector<Conversation>& conversations,
                       const std::filesystem::path& file_path);

}
