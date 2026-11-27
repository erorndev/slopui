#pragma once

#include <cstdint>
#include <vector>

#include "ui/Node.h"

namespace tvg {
struct SwCanvas;
struct Scene;
}

namespace lale::ui {

class Renderer {
 public:
  Renderer();
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  void Resize(int width, int height);
  void Render(const Node& root, bool caret_visible,
              float device_pixels_per_point);

  const uint32_t* pixels() const { return pixels_.data(); }
  int width() const { return width_; }
  int height() const { return height_; }

 private:
  void DrawNode(const Node& node, tvg::Scene* target_scene, bool caret_visible);
  bool DrawNodesBefore(const Node& node, const Node& stop_node,
                       tvg::Scene* target_scene);
  void DrawNodeContents(const Node& node, tvg::Scene* target_scene,
                        bool caret_visible);
  void DrawNodesFrom(const Node& node, const Node& start_node,
                     tvg::Scene* target_scene, bool caret_visible,
                     bool* reached);
  void BlurBehind(const Node& node, float device_pixels_per_point);
  static const Node* FindBackdropNode(const Node& node);
  void DrawText(const Node& node, tvg::Scene* target_scene, bool caret_visible);

  tvg::SwCanvas* canvas_ = nullptr;
  std::vector<uint32_t> pixels_;
  std::vector<uint32_t> backdrop_pixels_;
  int width_ = 0;
  int height_ = 0;
};

}
