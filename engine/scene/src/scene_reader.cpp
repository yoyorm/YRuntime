#include <yr/scene/scene_reader.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <iterator>
#include <string>
#include <system_error>
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
#include <yr/serialize/variant_codec.h>

namespace yr::scene {

  namespace {

    using Severity = SceneDiagnostic::Severity;

    struct Attribute {
      std::string key;
      std::string value;
      std::size_t offset = 0;
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
    std::size_t currentNode = kInvalidParent;
    const obj::ClassInfo* currentInfo = nullptr;
    std::string rootName;
    std::unordered_map<std::string, std::size_t> paths;
    std::unordered_set<std::string> seenProperties;
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

      if (pos < line.size()) {
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

          std::vector<Attribute> attributes;
          // 未知 section 只跳过、不解析其字段；否则未来的 section 语法会变成硬错误。
          if (section == "scene" || section == "node") {
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
            if (!result.scene.nodes.empty()) {
              errorAt(sectionStart, "[scene] 必须是第一个 section");
              return result;
            }

            sawHeader = true;
            skipSection = false;
            currentNode = kInvalidParent;
            currentInfo = nullptr;

            bool sawFormat = false;
            std::int64_t format = 0;
            for (const Attribute& attribute : attributes) {
              if (attribute.key != "format") {
                warningAt(attribute.offset, "未知 header 字段 '" + attribute.key + "'，跳过");
                continue;
              }
              if (sawFormat) {
                errorAt(attribute.offset, "重复的 format");
                return result;
              }
              sawFormat = true;
              const auto parsed =
                  std::from_chars(attribute.value.data(), attribute.value.data() + attribute.value.size(), format);
              if (parsed.ec != std::errc{} || parsed.ptr != attribute.value.data() + attribute.value.size()) {
                errorAt(attribute.offset, "format 必须是整数");
                return result;
              }
            }
            if (!sawFormat) {
              errorAt(sectionStart, "缺少 format");
              return result;
            }
            if (format != kSceneFormatVersion) {
              errorAt(sectionStart, "只支持 format=1");
              return result;
            }
          } else if (section == "node") {
            if (!sawHeader) {
              errorAt(sectionStart, "第一个 section 必须是 [scene format=1]");
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
            currentNode = index;
            currentInfo = info;
            seenProperties.clear();
          } else {
            if (!sawHeader) {
              errorAt(sectionStart, "第一个 section 必须是 [scene format=1]");
              return result;
            }
            warningAt(sectionStart, "未知 section '" + std::string(section) + "'，跳过");
            skipSection = true;
            currentNode = kInvalidParent;
            currentInfo = nullptr;
          }
        } else if (!skipSection && sawHeader && currentNode != kInvalidParent) {
          const std::size_t nameStart = pos;
          if (!isNameStart(line[pos])) {
            errorAt(pos, "属性名非法");
            return result;
          }
          ++pos;
          while (pos < line.size() && isNameChar(line[pos])) {
            ++pos;
          }
          const std::string_view propertyName = line.substr(nameStart, pos - nameStart);

          skipSpaces(line, pos);
          if (pos >= line.size() || line[pos] != '=') {
            errorAt(pos, "属性行缺少 '='");
            return result;
          }
          ++pos;
          skipSpaces(line, pos);

          const std::size_t valueStart = pos;
          if (valueStart >= line.size()) {
            errorAt(valueStart, "属性缺少值");
            return result;
          }
          const std::string_view valueText = line.substr(valueStart);
          const yr::ser::VariantDecodeResult decoded = yr::ser::VariantCodec::decode(valueText);
          if (!decoded.ok) {
            errorAt(valueStart + decoded.error_offset, decoded.error);
            return result;
          }
          if (decoded.consumed != valueText.size()) {
            errorAt(valueStart + decoded.consumed, "值后存在多余字符");
            return result;
          }

          if (!seenProperties.insert(std::string(propertyName)).second) {
            errorAt(nameStart, "重复属性：'" + std::string(propertyName) + "'");
            return result;
          }

          const core::StringId propertyId = interner.intern(propertyName);
          const obj::PropertyInfo* const property = currentInfo->findProperty(propertyId);
          if (property == nullptr) {
            warningAt(nameStart, "未知属性 '" + std::string(propertyName) + "'，跳过");
          } else if ((property->flags & obj::kReadOnly) != 0) {
            warningAt(nameStart, "只读属性 '" + std::string(propertyName) + "'，跳过");
          } else if (decoded.value.type() != property->type) {
            errorAt(valueStart, "属性类型不匹配：'" + std::string(propertyName) + "'");
            return result;
          } else {
            result.scene.nodes[currentNode].properties.push_back(SceneProperty{propertyId, decoded.value});
          }
        } else if (!skipSection && !sawHeader) {
          errorAt(pos, "第一个 section 必须是 [scene format=1]");
          return result;
        } else if (!skipSection) {
          errorAt(pos, "属性行出现在 node 之前");
          return result;
        }
      }

      if (newline == std::string_view::npos) {
        break;
      }
      start = newline + 1;
    }

    if (!sawHeader) {
      addDiagnostic(result, Severity::kError, lineNumber == 0 ? 1 : lineNumber, 1, "缺少 [scene format=1]");
    } else if (result.scene.nodes.empty()) {
      addDiagnostic(result, Severity::kError, lineNumber == 0 ? 1 : lineNumber, 1, "没有根节点");
    }
    return result;
  }

  SceneLoadResult SceneReader::read(std::istream& stream) const {
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return read(text);
  }

} // namespace yr::scene