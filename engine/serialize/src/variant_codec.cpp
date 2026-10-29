#include <yr/serialize/variant_codec.h>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <system_error>
#include <utility>

namespace yr::ser {

  namespace {

    using yr::obj::Variant;

    constexpr int kFloatDigits = std::numeric_limits<float>::max_digits10;
    constexpr int kDoubleDigits = std::numeric_limits<double>::max_digits10;

    [[nodiscard]] bool isSpace(char c) noexcept {
      return c == ' ' || c == '\t';
    }

    [[nodiscard]] bool isDigit(char c) noexcept {
      return c >= '0' && c <= '9';
    }

    [[nodiscard]] VariantEncodeResult encodeError(std::string message) {
      VariantEncodeResult result;
      result.error = std::move(message);
      return result;
    }

    [[nodiscard]] VariantEncodeResult encodeText(std::string text) {
      VariantEncodeResult result;
      result.ok = true;
      result.text = std::move(text);
      return result;
    }

    [[nodiscard]] VariantDecodeResult decodeError(std::size_t offset, std::string message) {
      VariantDecodeResult result;
      result.error_offset = offset;
      result.error = std::move(message);
      return result;
    }

    [[nodiscard]] bool formatInteger(std::int64_t value, std::string& out) {
      char buffer[32] = {};
      const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
      if (result.ec != std::errc{}) {
        return false;
      }
      out.assign(buffer, result.ptr);
      return true;
    }

    template <typename T> [[nodiscard]] bool formatFloating(T value, int precision, std::string& out) {
      char buffer[64] = {};
      const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general, precision);
      if (result.ec != std::errc{}) {
        return false;
      }
      out.assign(buffer, result.ptr);
      return true;
    }

    // 无类型标签的文本格式：double 若恰好格式化成整数形式（如 1 / -0），补 ".0"
    // 以便解码回 kFloat 而不是 kInt。Vector3 分量不需要，类型由 Vector3 本身确定。
    void ensureFloatingMarker(std::string& text) {
      if (text.find_first_of(".eE") == std::string::npos) {
        text += ".0";
      }
    }

    [[nodiscard]] std::string escapeString(std::string_view text) {
      std::string out;
      out.reserve(text.size() + 2);
      out.push_back('"');
      for (const char c : text) {
        switch (c) {
        case '"':
          out += "\\\"";
          break;
        case '\\':
          out += "\\\\";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\t':
          out += "\\t";
          break;
        case '\r':
          out += "\\r";
          break;
        default:
          out.push_back(c);
          break;
        }
      }
      out.push_back('"');
      return out;
    }

    [[nodiscard]] std::string_view typeName(Variant::Type type) noexcept {
      switch (type) {
      case Variant::Type::kNull:
        return "null";
      case Variant::Type::kBool:
        return "bool";
      case Variant::Type::kInt:
        return "int";
      case Variant::Type::kFloat:
        return "double";
      case Variant::Type::kStringId:
        return "StringId";
      case Variant::Type::kObjectID:
        return "ObjectID";
      case Variant::Type::kString:
        return "string";
      case Variant::Type::kArray:
        return "array";
      case Variant::Type::kDict:
        return "dict";
      case Variant::Type::kVec3:
        return "Vector3";
      }
      return "unknown";
    }

    [[nodiscard]] bool isValueBoundary(std::string_view text, std::size_t pos) noexcept {
      if (pos >= text.size()) {
        return true;
      }
      const char c = text[pos];
      return isSpace(c) || c == ',' || c == ')';
    }

    [[nodiscard]] VariantDecodeResult decodeString(std::string_view text, std::size_t offset, std::size_t pos) {
      std::string out;
      ++pos; // 开引号
      while (pos < text.size()) {
        const char c = text[pos];
        if (c == '"') {
          VariantDecodeResult result;
          result.ok = true;
          result.value = Variant(std::move(out));
          result.consumed = pos + 1 - offset;
          return result;
        }
        if (c == '\n' || c == '\r') {
          return decodeError(pos, "字符串不能跨行");
        }
        if (c == '\\') {
          const std::size_t escapeOffset = pos;
          ++pos;
          if (pos >= text.size()) {
            return decodeError(escapeOffset, "字符串转义未完成");
          }
          switch (text[pos]) {
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
            return decodeError(escapeOffset, "未知转义序列");
          }
          ++pos;
          continue;
        }
        out.push_back(c);
        ++pos;
      }
      return decodeError(pos, "字符串未闭合");
    }

    [[nodiscard]] VariantDecodeResult decodeVector3(std::string_view text, std::size_t offset, std::size_t pos) {
      pos += 7; // "Vector3"
      while (pos < text.size() && isSpace(text[pos])) {
        ++pos;
      }
      if (pos >= text.size() || text[pos] != '(') {
        return decodeError(pos, "Vector3 缺少 '('");
      }
      ++pos;

      float components[3] = {};
      for (int index = 0; index < 3; ++index) {
        while (pos < text.size() && isSpace(text[pos])) {
          ++pos;
        }
        double component = 0.0;
        const auto result =
            std::from_chars(text.data() + pos, text.data() + text.size(), component, std::chars_format::general);
        if (result.ec != std::errc{} || result.ptr == text.data() + pos) {
          return decodeError(pos, "Vector3 分量不是合法数字");
        }
        if (!std::isfinite(component)) {
          return decodeError(pos, "v1 不允许 NaN/Inf");
        }
        const float narrowed = static_cast<float>(component);
        if (!std::isfinite(narrowed)) {
          return decodeError(pos, "Vector3 分量超出 float 范围");
        }
        components[index] = narrowed;
        pos = static_cast<std::size_t>(result.ptr - text.data());
        while (pos < text.size() && isSpace(text[pos])) {
          ++pos;
        }
        if (index < 2) {
          if (pos >= text.size() || text[pos] != ',') {
            return decodeError(pos, "Vector3 需要三个分量");
          }
          ++pos;
        }
      }

      if (pos >= text.size() || text[pos] != ')') {
        return decodeError(pos, "Vector3 缺少 ')' 或存在多余分量");
      }
      ++pos;

      VariantDecodeResult result;
      result.ok = true;
      result.value = Variant(yr::math::Vec3{components[0], components[1], components[2]});
      result.consumed = pos - offset;
      return result;
    }

    [[nodiscard]] VariantDecodeResult decodeNumber(std::string_view text, std::size_t offset, std::size_t pos) {
      std::size_t end = pos;
      while (end < text.size() && !isSpace(text[end]) && text[end] != ',' && text[end] != ')') {
        ++end;
      }
      const std::string_view token = text.substr(pos, end - pos);
      const bool looksFloating = token.find_first_of(".eE") != std::string_view::npos;

      if (!looksFloating) {
        std::int64_t intValue = 0;
        const auto result = std::from_chars(token.data(), token.data() + token.size(), intValue);
        if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
          return decodeError(pos, "不是合法整数: " + std::string(token));
        }
        VariantDecodeResult decoded;
        decoded.ok = true;
        decoded.value = Variant(intValue);
        decoded.consumed = end - offset;
        return decoded;
      }

      double doubleValue = 0.0;
      const auto result =
          std::from_chars(token.data(), token.data() + token.size(), doubleValue, std::chars_format::general);
      if (result.ec != std::errc{} || result.ptr != token.data() + token.size()) {
        return decodeError(pos, "不是合法浮点数: " + std::string(token));
      }
      if (!std::isfinite(doubleValue)) {
        return decodeError(pos, "v1 不允许 NaN/Inf");
      }
      VariantDecodeResult decoded;
      decoded.ok = true;
      decoded.value = Variant(doubleValue);
      decoded.consumed = end - offset;
      return decoded;
    }

  } // namespace

  VariantEncodeResult VariantCodec::encode(const Variant& value) {
    switch (value.type()) {
    case Variant::Type::kBool:
      return encodeText(value.as<bool>() ? "true" : "false");
    case Variant::Type::kInt: {
      std::string text;
      if (!formatInteger(value.as<std::int64_t>(), text)) {
        return encodeError("int 格式化失败");
      }
      return encodeText(std::move(text));
    }
    case Variant::Type::kFloat: {
      const double number = value.as<double>();
      if (!std::isfinite(number)) {
        return encodeError("v1 不允许 NaN/Inf");
      }
      std::string text;
      if (!formatFloating(number, kDoubleDigits, text)) {
        return encodeError("double 格式化失败");
      }
      ensureFloatingMarker(text);
      return encodeText(std::move(text));
    }
    case Variant::Type::kString:
      return encodeText(escapeString(value.as<std::string>()));
    case Variant::Type::kVec3: {
      const yr::math::Vec3 vector = value.as<yr::math::Vec3>();
      if (!std::isfinite(vector.x) || !std::isfinite(vector.y) || !std::isfinite(vector.z)) {
        return encodeError("v1 不允许 NaN/Inf");
      }
      std::string x;
      std::string y;
      std::string z;
      if (!formatFloating(vector.x, kFloatDigits, x) || !formatFloating(vector.y, kFloatDigits, y) ||
          !formatFloating(vector.z, kFloatDigits, z)) {
        return encodeError("float 格式化失败");
      }
      return encodeText("Vector3(" + x + ", " + y + ", " + z + ")");
    }
    default:
      return encodeError("v1 不支持 Variant 类型 " + std::string(typeName(value.type())));
    }
  }

  VariantDecodeResult VariantCodec::decode(std::string_view text, std::size_t offset) {
    if (offset > text.size()) {
      return decodeError(offset, "offset 越界");
    }

    std::size_t pos = offset;
    while (pos < text.size() && isSpace(text[pos])) {
      ++pos;
    }
    if (pos >= text.size()) {
      return decodeError(pos, "缺少值");
    }

    const char first = text[pos];
    if (first == '"') {
      return decodeString(text, offset, pos);
    }
    if (text.substr(pos, 7) == "Vector3") {
      return decodeVector3(text, offset, pos);
    }
    if (text.substr(pos, 4) == "true" && isValueBoundary(text, pos + 4)) {
      VariantDecodeResult result;
      result.ok = true;
      result.value = Variant(true);
      result.consumed = pos + 4 - offset;
      return result;
    }
    if (text.substr(pos, 5) == "false" && isValueBoundary(text, pos + 5)) {
      VariantDecodeResult result;
      result.ok = true;
      result.value = Variant(false);
      result.consumed = pos + 5 - offset;
      return result;
    }
    if (first == '-' || first == '.' || isDigit(first)) {
      return decodeNumber(text, offset, pos);
    }
    return decodeError(pos, "无法识别的值");
  }

} // namespace yr::ser