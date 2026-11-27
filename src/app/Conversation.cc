#include "app/Conversation.h"

#include <charconv>
#include <sstream>
#include <string_view>

#include "app/DataFile.h"

namespace lale {

namespace {

constexpr std::string_view kConversationKey = "conversation";
constexpr std::string_view kUserKey = "user";
constexpr std::string_view kAssistantKey = "assistant";
constexpr std::string_view kNoticeKey = "notice";
constexpr std::string_view kStatisticsKey = "stats";

}

std::filesystem::path ConversationsFilePath() {
  return AppDataFilePath(L"conversations.ini");
}

std::vector<Conversation> LoadConversations(
    const std::filesystem::path& file_path) {
  std::vector<Conversation> conversations;
  for (const KeyValueLine& entry : ReadKeyValueLines(file_path)) {
    std::string value = UnescapeLineValue(entry.second);
    if (entry.first == kConversationKey) {
      conversations.push_back({std::move(value), {}});
      continue;
    }
    if (conversations.empty()) continue;
    std::vector<ChatMessage>& messages = conversations.back().messages;
    if (entry.first == kUserKey) {
      messages.push_back({MessageRole::kUser, std::move(value)});
    } else if (entry.first == kAssistantKey) {
      messages.push_back({MessageRole::kAssistant, std::move(value)});
    } else if (entry.first == kNoticeKey) {
      messages.push_back({MessageRole::kAssistant, std::move(value), true});
    } else if (entry.first == kStatisticsKey && !messages.empty()) {
      ChatMessage& message = messages.back();
      std::istringstream fields(value);
      fields >> message.prompt_token_count >> message.generated_token_count >>
          message.generation_seconds;
    }
  }
  return conversations;
}

bool SaveConversations(const std::vector<Conversation>& conversations,
                       const std::filesystem::path& file_path) {
  std::string contents;
  for (const Conversation& conversation : conversations) {
    if (conversation.messages.empty()) continue;
    contents.append(kConversationKey)
        .append("=")
        .append(EscapeLineValue(conversation.title))
        .append("\n");
    for (const ChatMessage& message : conversation.messages) {
      if (message.is_streaming) continue;
      const std::string_view key = message.is_notice ? kNoticeKey
                                   : message.role == MessageRole::kUser
                                       ? kUserKey
                                       : kAssistantKey;
      contents.append(key)
          .append("=")
          .append(EscapeLineValue(message.text))
          .append("\n");
      if (message.generated_token_count > 0) {
        char seconds[32];
        auto result = std::to_chars(seconds, seconds + sizeof(seconds),
                                    message.generation_seconds,
                                    std::chars_format::fixed, 2);
        contents.append(kStatisticsKey)
            .append("=")
            .append(std::to_string(message.prompt_token_count))
            .append(" ")
            .append(std::to_string(message.generated_token_count))
            .append(" ")
            .append(seconds, result.ptr)
            .append("\n");
      }
    }
  }
  return WriteFileAtomically(file_path, contents);
}

}
