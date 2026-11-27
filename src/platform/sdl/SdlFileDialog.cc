#include <SDL3/SDL.h>

#include <memory>
#include <utility>

#include "platform/FileDialog.h"

namespace lale {

namespace {

struct OpenFileRequest {
  std::string filter_name;
  std::string file_extension;
  SDL_DialogFileFilter filter{};
  FileDialogCallback on_finished;
  std::optional<std::string> chosen_path;
};

void SDLCALL FinishOnMainThread(void* user_data) {
  std::unique_ptr<OpenFileRequest> request(
      static_cast<OpenFileRequest*>(user_data));
  request->on_finished(std::move(request->chosen_path));
}

void SDLCALL ReceiveChosenFiles(void* user_data, const char* const* file_list,
                                int) {
  auto* request = static_cast<OpenFileRequest*>(user_data);
  if (file_list && file_list[0]) request->chosen_path = file_list[0];
  SDL_RunOnMainThread(&FinishOnMainThread, request, false);
}

}

void ShowOpenFileDialog(const Window& owner, std::string_view filter_name,
                        std::string_view file_extension,
                        FileDialogCallback on_finished) {
  auto request = std::make_unique<OpenFileRequest>();
  request->filter_name = filter_name;
  request->file_extension = file_extension;
  request->filter = {request->filter_name.c_str(),
                     request->file_extension.c_str()};
  request->on_finished = std::move(on_finished);
  OpenFileRequest* pending_request = request.release();
  SDL_ShowOpenFileDialog(&ReceiveChosenFiles, pending_request,
                         static_cast<SDL_Window*>(owner.native_handle()),
                         &pending_request->filter, 1, nullptr, false);
}

}
