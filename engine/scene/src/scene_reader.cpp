#include <yr/scene/scene_reader.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <yr/core/string_id.h>
#include <yr/core/string_interner.h>
#include <yr/object/class_db.h>
#include <yr/object/class_info.h>
#include <yr/object/property_info.h>
#include <yr/object/variant.h>
#include <yr/scene/node.h>
#include <yr/scene/scene_classes.h>
#include <yr/scene/scene_format.h>
#include <yr/scene/scene_reference.h>
#include <yr/serialize/variant_codec.h>

namespace yr::scene {

  namespace {

    using Severity = SceneDiagnostic::Severity;

    enum class SectionKind { kNone, kScene, kNode, kExtResource, kSubResource, kConnection };

    struct Attribute {
      std::string key;
      std::string value;
      std::size_t offset = 0;
    };

    struct ParsedProperty {
      std::string_view name;
      std::string_view value;
      std::size_t name_start = 0;
      std::size_t value_start = 0;
      std::size_t error_offset = 0;
      std::string error;
      bool ok = false;
    };

    struct ParsedReference {
      bool ok = false;
      ReferenceKind kind = ReferenceKind::kNone;
      std::string id;
      std::size_t consumed = 0;
      std::size_t error_offset = 0;
      std::string error;
    };

    [[nodiscard]] bool isSpace(char c) noexcept {
      return c == ' ' || c == '\t';
    }

    [[nodiscard]] bool isNameStart(char c) noexcept {
      return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    }

    [[nodiscard]] bool isNameChar(char c) noexcept {
      return isNameStart(c) || (c >= '0' && c <= '9');
    }

    void addDiagnostic(SceneLoadResult& result, Severity severity, int line, int column, std::string message) {
      result.diagnostics.push_back(SceneDiagnostic{severity, line, column, std::move(message)});
    }

    void appendDiagnostics(SceneLoadResult& result, std::vector<SceneDiagnostic>&& diagnostics) {
      for (SceneDiagnostic& diagnostic : diagnostics) {
        result.diagnostics.push_back(std::move(diagnostic));
      }
    }

    void skipSpaces(std::string_view text, std::size_t& pos) noexcept {
      while (pos < text.size() && isSpace(text[pos])) {
        ++pos;
      }
    }

    [[nodiscard]] std::string_view trimRight(std::string_view text) noexcept {
      while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
      }
      return text;
    }

    // '#' 到行尾是注释，但引号内的 '#' 是字符串内容。
    [[nodiscard]] std::string_view stripComment(std::string_view line) noexcept {
      bool inString = false;
      bool escaped = false;
      for (std::size_t index = 0; index < line.size(); ++index) {
        const char c = line[index];
        if (inString) {
          if (escaped) {
            escaped = false;
          } else if (c == '\\') {
            escaped = true;
          } else if (c == '"') {
            inString = false;
          }
          continue;
        }
        if (c == '"') {
          inString = true;
        } else if (c == '#') {
          return line.substr(0, index);
        }
      }
      return line;
    }

    [[nodiscard]] bool parseQuoted(std::string_view line, std::size_t& pos, std::string& out, std::string& error) {
      ++pos; // 开引号
      while (pos < line.size()) {
        const char c = line[pos];
        if (c == '"') {
          ++pos;
          return true;
        }
        if (c == '\n' || c == '\r') {
          error = "字符串不能跨行";
          return false;
        }
        if (c == '\\') {
          ++pos;
          if (pos >= line.size()) {
            error = "字符串转义未完成";
            return false;
          }
          switch (line[pos]) {
          case '"':
            out.push_back('"');
            break;
          case '\\':
            out.push_back('\\');
            break;
          case 'n':
            out.push_back('\n');
            break;
          case 't':
            out.push_back('\t');
            break;
          case 'r':
            out.push_back('\r');
            break;
          default:
            error = "未知转义序列";
            return false;
          }
          ++pos;
          continue;
        }
        out.push_back(c);
        ++pos;
      }
      error = "字符串未闭合";
      return false;
    }

    // 解析 section 内的 key=value 属性，直到并消费 ']'。
    [[nodiscard]] bool parseAttributes(std::string_view line, std::size_t& pos, std::vector<Attribute>& out,
                                       std::string& error) {
      while (true) {
        skipSpaces(line, pos);
        if (pos >= line.size()) {
          error = "section 缺少 ']'";
          return false;
        }
        if (line[pos] == ']') {
          ++pos;
          break;
        }

        const std::size_t keyStart = pos;
        if (!isNameStart(line[pos])) {
          error = "期望属性名";
          return false;
        }
        ++pos;
        while (pos < line.size() && isNameChar(line[pos])) {
          ++pos;
        }
        std::string key(line.substr(keyStart, pos - keyStart));

        skipSpaces(line, pos);
        if (pos >= line.size() || line[pos] != '=') {
          error = "属性缺少 '='";
          return false;
        }
        ++pos;
        skipSpaces(line, pos);

        std::string value;
        if (pos < line.size() && line[pos] == '"') {
          if (!parseQuoted(line, pos, value, error)) {
            return false;
          }
        } else {
          const std::size_t valueStart = pos;
          while (pos < line.size() && !isSpace(line[pos]) && line[pos] != ']' && line[pos] != '=') {
            ++pos;
          }
          if (valueStart == pos) {
            error = "属性缺少值";
            return false;
          }
          value.assign(line.substr(valueStart, pos - valueStart));
        }
        out.push_back(Attribute{std::move(key), std::move(value), keyStart});
      }

      skipSpaces(line, pos);
      if (pos != line.size()) {
        error = "section 后存在多余字符";
        return false;
      }
      return true;
    }

    // 解析 `name = value`；value 一直取到行尾（已去注释/右空白）。
    [[nodiscard]] ParsedProperty parsePropertyLine(std::string_view line, std::size_t pos) {
      ParsedProperty parsed;
      parsed.name_start = pos;
      if (pos >= line.size() || !isNameStart(line[pos])) {
        parsed.error = "属性名非法";
        parsed.error_offset = pos;
        return parsed;
      }

      ++pos;
      while (pos < line.size() && isNameChar(line[pos])) {
        ++pos;
      }
      parsed.name = line.substr(parsed.name_start, pos - parsed.name_start);

      skipSpaces(line, pos);
      if (pos >= line.size() || line[pos] != '=') {
        parsed.error = "属性行缺少 '='";
        parsed.error_offset = pos;
        return parsed;
      }
      ++pos;
      skipSpaces(line, pos);

      parsed.value_start = pos;
      if (pos >= line.size()) {
        parsed.error = "属性缺少值";
        parsed.error_offset = pos;
        return parsed;
      }
      parsed.value = line.substr(pos);
      parsed.ok = true;
      return parsed;
    }

    [[nodiscard]] bool looksLikeReference(std::string_view value) noexcept {
      return value.substr(0, 11) == "ExtResource" || value.substr(0, 11) == "SubResource";
    }

    // 解析 ExtResource("id") / SubResource("id")；括号内允许空格。
    [[nodiscard]] ParsedReference parseReference(std::string_view text) {
      ParsedReference parsed;
      std::size_t pos = 0;
      if (text.substr(0, 11) == "ExtResource") {
        parsed.kind = ReferenceKind::kExtResource;
      } else if (text.substr(0, 11) == "SubResource") {
        parsed.kind = ReferenceKind::kSubResource;
      } else {
        parsed.error = "不是引用值";
        return parsed;
      }
      pos = 11;

      skipSpaces(text, pos);
      if (pos >= text.size() || text[pos] != '(') {
        parsed.error = "引用缺少 '('";
        parsed.error_offset = pos;
        return parsed;
      }
      ++pos;
      skipSpaces(text, pos);
      if (pos >= text.size() || text[pos] != '"') {
        parsed.error = "引用 id 缺少开引号";
        parsed.error_offset = pos;
        return parsed;
      }
      ++pos;
      const std::size_t idStart = pos;
      while (pos < text.size() && text[pos] != '"') {
        ++pos;
      }
      if (pos >= text.size()) {
        parsed.error = "引用 id 未闭合";
        parsed.error_offset = idStart;
        return parsed;
      }
      parsed.id = std::string(text.substr(idStart, pos - idStart));
      ++pos;

      skipSpaces(text, pos);
      if (pos >= text.size() || text[pos] != ')') {
        parsed.error = "引用缺少 ')'";
        parsed.error_offset = pos;
        return parsed;
      }
      ++pos;
      skipSpaces(text, pos);
      if (pos != text.size()) {
        parsed.error = "引用后存在多余字符";
        parsed.error_offset = pos;
        return parsed;
      }
      if (!isValidReferenceId(parsed.id)) {
        parsed.error = "引用 id 非法";
        parsed.error_offset = idStart;
        return parsed;
      }

      parsed.consumed = pos;
      parsed.ok = true;
      return parsed;
    }

    [[nodiscard]] std::string registeredClassList(const obj::ClassDB& classes) {
      std::vector<std::string_view> names;
      for (const core::StringId id : classes.allClasses()) {
        names.push_back(id.view());
      }
      std::sort(names.begin(), names.end());
      std::string list;
      for (std::size_t index = 0; index < names.size(); ++index) {
        if (index != 0) {
          list += ", ";
        }
        list += names[index];
      }
      return list;
    }

  } // namespace

  SceneLoadResult SceneReader::read(std::string_view text) const {
    SceneLoadResult result;
    bool sawHeader = false;
    bool skipSection = false;
    SectionKind sectionKind = SectionKind::kNone;
    bool sawNode = false;
    bool sawConnection = false;
    std::size_t currentNode = kInvalidIndex;
    std::size_t currentSub = kInvalidIndex;
    const obj::ClassInfo* currentInfo = nullptr;
    std::string rootName;
    std::unordered_map<std::string, std::size_t> paths;
    std::unordered_set<std::string> seenProperties;
    std::unordered_set<std::string> extIds;
    std::unordered_set<std::string> subIds;
    core::StringInterner& interner = core::globalStringInterner();

    int lineNumber = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
      const std::size_t newline = text.find('\n', start);
      const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
      std::string_view line = text.substr(start, end - start);
      ++lineNumber;
      if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
      }

      std::size_t columnOffset = 0;
      if (lineNumber == 1 && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
          static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
        line.remove_prefix(3);
        columnOffset = 3;
      }

      line = trimRight(stripComment(line));
      std::size_t pos = 0;
      skipSpaces(line, pos);

      const auto errorAt = [&result, lineNumber, columnOffset](std::size_t linePos, std::string message) {
        addDiagnostic(result, Severity::kError, lineNumber, static_cast<int>(columnOffset + linePos + 1),
                      std::move(message));
      };
      const auto warningAt = [&result, lineNumber, columnOffset](std::size_t linePos, std::string message) {
        addDiagnostic(result, Severity::kWarning, lineNumber, static_cast<int>(columnOffset + linePos + 1),
                      std::move(message));
      };

      if (pos >= line.size()) {
        if (newline == std::string_view::npos) {
          break;
        }
        start = newline + 1;
        continue;
      }

      if (line[pos] == '[') {
        std::size_t cursor = pos + 1;
        skipSpaces(line, cursor);
        const std::size_t sectionStart = cursor;
        while (cursor < line.size() && !isSpace(line[cursor]) && line[cursor] != ']' && line[cursor] != '=') {
          ++cursor;
        }
        if (sectionStart == cursor) {
          errorAt(pos, "section 缺少名字");
          return result;
        }
        const std::string_view section = line.substr(sectionStart, cursor - sectionStart);
        const bool knownSection = section == "scene" || section == "ext_resource" || section == "sub_resource" ||
                                  section == "node" || section == "connection";

        std::vector<Attribute> attributes;
        // 未知 section 只跳过、不解析其字段；否则未来的 section 语法会变成硬错误。
        if (knownSection) {
          std::string parseError;
          if (!parseAttributes(line, cursor, attributes, parseError)) {
            errorAt(cursor, std::move(parseError));
            return result;
          }
        }

        if (section == "scene") {
          if (sawHeader) {
            errorAt(sectionStart, "重复的 [scene]");
            return result;
          }
          if (!result.scene.nodes.empty() || !result.scene.ext_resources.empty() ||
              !result.scene.sub_resources.empty() || !result.scene.connections.empty()) {
            errorAt(sectionStart, "[scene] 必须是第一个 section");
            return result;
          }

          sawHeader = true;
          skipSection = false;
          sectionKind = SectionKind::kScene;
          currentNode = kInvalidIndex;
          currentSub = kInvalidIndex;
          currentInfo = nullptr;
          seenProperties.clear();

          for (const Attribute& attribute : attributes) {
            warningAt(attribute.offset, "未知 header 字段 '" + attribute.key + "'，跳过");
          }
        } else if (section == "ext_resource") {
          if (!sawHeader) {
            errorAt(sectionStart, "第一个 section 必须是 [scene]");
            return result;
          }
          if (sawNode) {
            errorAt(sectionStart, "ext_resource 必须在 node 之前");
            return result;
          }

          skipSection = false;
          sectionKind = SectionKind::kExtResource;
          currentNode = kInvalidIndex;
          currentSub = kInvalidIndex;
          currentInfo = nullptr;
          seenProperties.clear();

          PackedExtResource resource;
          bool sawType = false;
          bool sawId = false;
          bool sawPath = false;
          bool sawUid = false;
          std::size_t idOffset = sectionStart;
          for (const Attribute& attribute : attributes) {
            if (attribute.key == "type") {
              if (sawType) {
                errorAt(attribute.offset, "重复的 ext_resource.type");
                return result;
              }
              sawType = true;
              resource.type = interner.intern(attribute.value);
            } else if (attribute.key == "id") {
              if (sawId) {
                errorAt(attribute.offset, "重复的 ext_resource.id");
                return result;
              }
              sawId = true;
              resource.id = attribute.value;
              idOffset = attribute.offset;
            } else if (attribute.key == "path") {
              if (sawPath) {
                errorAt(attribute.offset, "重复的 ext_resource.path");
                return result;
              }
              sawPath = true;
              resource.path = attribute.value;
            } else if (attribute.key == "uid") {
              if (sawUid) {
                errorAt(attribute.offset, "重复的 ext_resource.uid");
                return result;
              }
              sawUid = true;
              resource.uid = attribute.value;
            } else {
              warningAt(attribute.offset, "未知 ext_resource 字段 '" + attribute.key + "'，跳过");
            }
          }

          if (!sawType) {
            errorAt(sectionStart, "ext_resource 缺少 type");
            return result;
          }
          if (resource.type.view().empty()) {
            errorAt(sectionStart, "ext_resource.type 不能为空");
            return result;
          }
          if (!sawId) {
            errorAt(sectionStart, "ext_resource 缺少 id");
            return result;
          }
          if (!isValidReferenceId(resource.id)) {
            errorAt(sectionStart, "ext_resource.id 非法：只允许 [A-Za-z0-9_.-]");
            return result;
          }
          if (!extIds.insert(resource.id).second) {
            errorAt(idOffset, "重复的 ext_resource id '" + resource.id + "'");
            return result;
          }
          if (!sawPath) {
            errorAt(sectionStart, "ext_resource 缺少 path");
            return result;
          }
          if (resource.path.empty() || !resource.path.starts_with("res://")) {
            errorAt(sectionStart, "ext_resource.path 必须以 res:// 开头");
            return result;
          }

          result.scene.ext_resources.push_back(std::move(resource));
        } else if (section == "sub_resource") {
          if (!sawHeader) {
            errorAt(sectionStart, "第一个 section 必须是 [scene]");
            return result;
          }
          if (sawNode) {
            errorAt(sectionStart, "sub_resource 必须在 node 之前");
            return result;
          }

          skipSection = false;
          sectionKind = SectionKind::kSubResource;
          currentNode = kInvalidIndex;
          currentSub = kInvalidIndex;
          currentInfo = nullptr;
          seenProperties.clear();

          PackedSubResource resource;
          bool sawType = false;
          bool sawId = false;
          std::size_t idOffset = sectionStart;
          for (const Attribute& attribute : attributes) {
            if (attribute.key == "type") {
              if (sawType) {
                errorAt(attribute.offset, "重复的 sub_resource.type");
                return result;
              }
              sawType = true;
              resource.type = interner.intern(attribute.value);
            } else if (attribute.key == "id") {
              if (sawId) {
                errorAt(attribute.offset, "重复的 sub_resource.id");
                return result;
              }
              sawId = true;
              resource.id = attribute.value;
              idOffset = attribute.offset;
            } else {
              warningAt(attribute.offset, "未知 sub_resource 字段 '" + attribute.key + "'，跳过");
            }
          }

          if (!sawType) {
            errorAt(sectionStart, "sub_resource 缺少 type");
            return result;
          }
          if (resource.type.view().empty()) {
            errorAt(sectionStart, "sub_resource.type 不能为空");
            return result;
          }
          if (!sawId) {
            errorAt(sectionStart, "sub_resource 缺少 id");
            return result;
          }
          if (!isValidReferenceId(resource.id)) {
            errorAt(sectionStart, "sub_resource.id 非法：只允许 [A-Za-z0-9_.-]");
            return result;
          }
          if (!subIds.insert(resource.id).second) {
            errorAt(idOffset, "重复的 sub_resource id '" + resource.id + "'");
            return result;
          }

          result.scene.sub_resources.push_back(std::move(resource));
          currentSub = result.scene.sub_resources.size() - 1;
        } else if (section == "node") {
          if (!sawHeader) {
            errorAt(sectionStart, "第一个 section 必须是 [scene]");
            return result;
          }
          if (sawConnection) {
            errorAt(sectionStart, "node 必须在 connection 之前");
            return result;
          }

          skipSection = false;
          PackedNode packed;
          bool sawName = false;
          bool sawType = false;
          bool sawParent = false;

          for (const Attribute& attribute : attributes) {
            if (attribute.key == "name") {
              if (sawName) {
                errorAt(attribute.offset, "重复的 node.name");
                return result;
              }
              sawName = true;
              packed.name = attribute.value;
            } else if (attribute.key == "type") {
              if (sawType) {
                errorAt(attribute.offset, "重复的 node.type");
                return result;
              }
              sawType = true;
              packed.type = interner.intern(attribute.value);
            } else if (attribute.key == "parent") {
              if (sawParent) {
                errorAt(attribute.offset, "重复的 node.parent");
                return result;
              }
              sawParent = true;
              packed.parent_path = attribute.value;
            } else {
              warningAt(attribute.offset, "未知 node 字段 '" + attribute.key + "'，跳过");
            }
          }

          if (!sawName) {
            errorAt(sectionStart, "node 缺少 name");
            return result;
          }
          if (!isValidNodeName(packed.name)) {
            errorAt(sectionStart, "node name 非法：不能为空、不能是 '.' 或 '..'、不能含 '/'、'\"' 或换行");
            return result;
          }
          if (!sawType) {
            errorAt(sectionStart, "node 缺少 type");
            return result;
          }

          const obj::ClassInfo* const info = classes_.getClass(packed.type);
          if (info == nullptr) {
            errorAt(sectionStart,
                    "未知 class '" + std::string(packed.type.view()) + "'；已注册：" + registeredClassList(classes_));
            return result;
          }
          if (!info->isA(&nodeClassInfo())) {
            errorAt(sectionStart, "class '" + std::string(packed.type.view()) + "' 不是 Node");
            return result;
          }

          const bool isRoot = result.scene.nodes.empty();
          std::string fullPath;
          if (isRoot) {
            if (sawParent) {
              errorAt(sectionStart, "根节点不能有 parent");
              return result;
            }
            fullPath = packed.name;
          } else {
            if (!sawParent || packed.parent_path.empty()) {
              errorAt(sectionStart, "非根节点缺少 parent");
              return result;
            }

            if (packed.parent_path == ".") {
              packed.parent_index = 0;
              fullPath = rootName + "/" + packed.name;
            } else {
              const std::string parentPath = rootName + "/" + packed.parent_path;
              const auto parent = paths.find(parentPath);
              if (parent == paths.end()) {
                errorAt(sectionStart, "父路径不存在或父在子后：'" + packed.parent_path + "'");
                return result;
              }
              packed.parent_index = parent->second;
              fullPath = parentPath + "/" + packed.name;
            }
          }

          if (paths.find(fullPath) != paths.end()) {
            errorAt(sectionStart, "同级重名：'" + packed.name + "'");
            return result;
          }

          result.scene.nodes.push_back(std::move(packed));
          const std::size_t index = result.scene.nodes.size() - 1;
          if (isRoot) {
            rootName = result.scene.nodes.front().name;
          }
          paths.emplace(std::move(fullPath), index);
          sawNode = true;
          sectionKind = SectionKind::kNode;
          currentNode = index;
          currentSub = kInvalidIndex;
          currentInfo = info;
          seenProperties.clear();
        } else if (section == "connection") {
          if (!sawHeader) {
            errorAt(sectionStart, "第一个 section 必须是 [scene]");
            return result;
          }
          if (!sawNode || result.scene.nodes.empty()) {
            errorAt(sectionStart, "connection 必须在 node 之后");
            return result;
          }

          skipSection = false;
          sectionKind = SectionKind::kConnection;
          currentNode = kInvalidIndex;
          currentSub = kInvalidIndex;
          currentInfo = nullptr;
          seenProperties.clear();

          std::string from;
          std::string to;
          std::string signalName;
          std::string methodName;
          bool sawFrom = false;
          bool sawTo = false;
          bool sawSignal = false;
          bool sawMethod = false;
          for (const Attribute& attribute : attributes) {
            if (attribute.key == "from") {
              if (sawFrom) {
                errorAt(attribute.offset, "重复的 connection.from");
                return result;
              }
              sawFrom = true;
              from = attribute.value;
            } else if (attribute.key == "to") {
              if (sawTo) {
                errorAt(attribute.offset, "重复的 connection.to");
                return result;
              }
              sawTo = true;
              to = attribute.value;
            } else if (attribute.key == "signal") {
              if (sawSignal) {
                errorAt(attribute.offset, "重复的 connection.signal");
                return result;
              }
              sawSignal = true;
              signalName = attribute.value;
            } else if (attribute.key == "method") {
              if (sawMethod) {
                errorAt(attribute.offset, "重复的 connection.method");
                return result;
              }
              sawMethod = true;
              methodName = attribute.value;
            } else {
              warningAt(attribute.offset, "未知 connection 字段 '" + attribute.key + "'，跳过");
            }
          }

          if (!sawFrom || from.empty()) {
            errorAt(sectionStart, "connection 缺少 from");
            return result;
          }
          if (!sawTo || to.empty()) {
            errorAt(sectionStart, "connection 缺少 to");
            return result;
          }
          if (!sawSignal || signalName.empty()) {
            errorAt(sectionStart, "connection 缺少 signal");
            return result;
          }
          if (!sawMethod || methodName.empty()) {
            errorAt(sectionStart, "connection 缺少 method");
            return result;
          }

          const auto resolveNodePath = [&paths, &rootName](const std::string& path, std::size_t& index) -> bool {
            if (path == ".") {
              index = 0;
              return true;
            }
            const auto found = paths.find(rootName + "/" + path);
            if (found == paths.end()) {
              return false;
            }
            index = found->second;
            return true;
          };

          std::size_t fromIndex = kInvalidIndex;
          std::size_t toIndex = kInvalidIndex;
          if (!resolveNodePath(from, fromIndex)) {
            errorAt(sectionStart, "connection.from 找不到节点路径：'" + from + "'");
            return result;
          }
          if (!resolveNodePath(to, toIndex)) {
            errorAt(sectionStart, "connection.to 找不到节点路径：'" + to + "'");
            return result;
          }

          const core::StringId signalId = interner.intern(signalName);
          const core::StringId methodId = interner.intern(methodName);
          for (const PackedConnection& existing : result.scene.connections) {
            if (existing.from == fromIndex && existing.to == toIndex && existing.signal == signalId &&
                existing.method == methodId) {
              errorAt(sectionStart, "重复的 connection");
              return result;
            }
          }

          result.scene.connections.push_back(PackedConnection{fromIndex, toIndex, signalId, methodId});
          sawConnection = true;
        } else {
          if (!sawHeader) {
            errorAt(sectionStart, "第一个 section 必须是 [scene]");
            return result;
          }
          warningAt(sectionStart, "未知 section '" + std::string(section) + "'，跳过");
          skipSection = true;
          sectionKind = SectionKind::kNone;
          currentNode = kInvalidIndex;
          currentSub = kInvalidIndex;
          currentInfo = nullptr;
        }
      } else {
        // 属性行归属当前 section。
        if (!skipSection) {
          if (!sawHeader) {
            errorAt(pos, "第一个 section 必须是 [scene]");
            return result;
          }

          switch (sectionKind) {
          case SectionKind::kNode: {
            const ParsedProperty parsed = parsePropertyLine(line, pos);
            if (!parsed.ok) {
              errorAt(parsed.error_offset, std::move(parsed.error));
              return result;
            }
            if (!seenProperties.insert(std::string(parsed.name)).second) {
              errorAt(parsed.name_start, "重复属性：'" + std::string(parsed.name) + "'");
              return result;
            }
            if (looksLikeReference(parsed.value)) {
              errorAt(parsed.value_start, "node 属性暂不支持引用");
              return result;
            }

            const yr::ser::VariantDecodeResult decoded = yr::ser::VariantCodec::decode(parsed.value);
            if (!decoded.ok) {
              errorAt(parsed.value_start + decoded.error_offset, decoded.error);
              return result;
            }
            if (decoded.consumed != parsed.value.size()) {
              errorAt(parsed.value_start + decoded.consumed, "值后存在多余字符");
              return result;
            }

            const core::StringId propertyId = interner.intern(parsed.name);
            const obj::PropertyInfo* const property = currentInfo->findProperty(propertyId);
            if (property == nullptr) {
              warningAt(parsed.name_start, "未知属性 '" + std::string(parsed.name) + "'，跳过");
            } else if ((property->flags & obj::kReadOnly) != 0) {
              warningAt(parsed.name_start, "只读属性 '" + std::string(parsed.name) + "'，跳过");
            } else if (decoded.value.type() != property->type) {
              errorAt(parsed.value_start, "属性类型不匹配：'" + std::string(parsed.name) + "'");
              return result;
            } else {
              result.scene.nodes[currentNode].properties.push_back(SceneProperty{propertyId, decoded.value});
            }
            break;
          }
          case SectionKind::kSubResource: {
            const ParsedProperty parsed = parsePropertyLine(line, pos);
            if (!parsed.ok) {
              errorAt(parsed.error_offset, std::move(parsed.error));
              return result;
            }
            if (!seenProperties.insert(std::string(parsed.name)).second) {
              errorAt(parsed.name_start, "重复属性：'" + std::string(parsed.name) + "'");
              return result;
            }

            SceneValue value;
            if (looksLikeReference(parsed.value)) {
              const ParsedReference reference = parseReference(parsed.value);
              if (!reference.ok) {
                errorAt(parsed.value_start + reference.error_offset, reference.error);
                return result;
              }
              value.reference.kind = reference.kind;
              value.reference.id = reference.id;
              value.reference.index = kInvalidIndex;
              value.reference.source_line = lineNumber;
              value.reference.source_column = static_cast<int>(columnOffset + parsed.value_start + 1);
            } else {
              const yr::ser::VariantDecodeResult decoded = yr::ser::VariantCodec::decode(parsed.value);
              if (!decoded.ok) {
                errorAt(parsed.value_start + decoded.error_offset, decoded.error);
                return result;
              }
              if (decoded.consumed != parsed.value.size()) {
                errorAt(parsed.value_start + decoded.consumed, "值后存在多余字符");
                return result;
              }
              value.value = decoded.value;
            }

            result.scene.sub_resources[currentSub].properties.push_back(
                PackedSubResourceProperty{interner.intern(parsed.name), std::move(value)});
            break;
          }
          case SectionKind::kScene:
            errorAt(pos, "scene 段不允许属性行");
            return result;
          case SectionKind::kExtResource:
            errorAt(pos, "ext_resource 段不允许属性行");
            return result;
          case SectionKind::kConnection:
            errorAt(pos, "connection 段不允许属性行");
            return result;
          case SectionKind::kNone:
            errorAt(pos, "属性行出现在 node 之前");
            return result;
          }
        }
      }

      if (newline == std::string_view::npos) {
        break;
      }
      start = newline + 1;
    }

    if (!sawHeader) {
      addDiagnostic(result, Severity::kError, lineNumber == 0 ? 1 : lineNumber, 1, "缺少 [scene]");
    } else if (result.scene.nodes.empty()) {
      addDiagnostic(result, Severity::kError, lineNumber == 0 ? 1 : lineNumber, 1, "没有根节点");
    }

    if (!result.hasErrors()) {
      ResolveResult resolution = resolveReferences(result.scene);
      appendDiagnostics(result, std::move(resolution.diagnostics));
    }
    return result;
  }

  SceneLoadResult SceneReader::read(std::istream& stream) const {
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return read(text);
  }

} // namespace yr::scene
