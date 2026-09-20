#pragma once
#include "white/document.hpp"
#include <filesystem>
#include <optional>
#include <string_view>

namespace white {
inline constexpr std::size_t max_scene_bytes=4*1024*1024;
std::string scene_json(const Scene&);
Scene parse_scene_json(std::string_view);
Scene read_scene(const std::filesystem::path&);
enum class SaveFault {none,before_write,before_publish};
// Fault points exercise the same production cleanup/publish path in tests.
void save_scene_atomic(const Scene&,const std::filesystem::path&,SaveFault fault=SaveFault::none);

class EditorSession {
public:
    explicit EditorSession(Scene scene = {}):document_(std::move(scene)) {}
    const Document& document() const {return document_;}
    bool modified() const {return !saved_ || document_.scene()!=*saved_;}
    bool can_undo() const {return !drag_&&!undo_.empty();}
    bool can_redo() const {return !drag_&&!redo_.empty();}
    bool apply(Scene scene);
    Id add_cell(Cell cell = {});
    bool remove_cell(Id);
    void begin_drag();
    void end_drag();
    void cancel_drag();
    bool undo();
    bool redo();
    void save(const std::filesystem::path&,SaveFault fault=SaveFault::none);
    void load(const std::filesystem::path&,bool discard_unsaved=false);
private:
    Document document_;
    std::optional<Scene> saved_,drag_;
    std::vector<Scene> undo_,redo_;
};
}
