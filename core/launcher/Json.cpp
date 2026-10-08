#include "Json.hpp"

#include <cctype>
#include <cstdio>
#include <stdexcept>

namespace Json {

const Value* Value::find(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

std::string Value::asString(const std::string& fallback) const {
    if (type == Type::String) return string;
    if (type == Type::Number) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), isInteger ? "%.0f" : "%g", number);
        return buffer;
    }
    if (type == Type::Boolean) return boolean ? "true" : "false";
    return fallback;
}

bool Value::asBool(bool fallback) const {
    if (type == Type::Boolean) return boolean;
    if (type == Type::Number) return number != 0.0;
    return fallback;
}

namespace {

struct Parser {
    const std::string& text;
    std::size_t position = 0;

    [[noreturn]] void fail(const std::string& reason) const {
        throw std::runtime_error("JSON: " + reason + " at offset " + std::to_string(position));
    }

    void skipSpace() {
        while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position]))) position++;
    }

    char peek() {
        skipSpace();
        if (position >= text.size()) fail("unexpected end of input");
        return text[position];
    }

    char take() {
        const char value = peek();
        position++;
        return value;
    }

    void expect(char value) {
        if (take() != value) fail(std::string("expected '") + value + "'");
    }

    std::string parseString() {
        expect('"');
        std::string out;
        for (;;) {
            if (position >= text.size()) fail("unterminated string");
            const char value = text[position++];
            if (value == '"') return out;
            if (value != '\\') {
                out += value;
                continue;
            }
            if (position >= text.size()) fail("unterminated escape");
            const char escape = text[position++];
            switch (escape) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (position + 4 > text.size()) fail("truncated escape");
                    unsigned code = 0;
                    for (int i = 0; i < 4; i++) {
                        const char digit = text[position++];
                        code <<= 4;
                        if (digit >= '0' && digit <= '9') code |= static_cast<unsigned>(digit - '0');
                        else if (digit >= 'a' && digit <= 'f') code |= static_cast<unsigned>(digit - 'a' + 10);
                        else if (digit >= 'A' && digit <= 'F') code |= static_cast<unsigned>(digit - 'A' + 10);
                        else fail("invalid unicode escape");
                    }
                    if (code < 0x80) {
                        out += static_cast<char>(code);
                    } else if (code < 0x800) {
                        out += static_cast<char>(0xC0 | (code >> 6));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (code >> 12));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default: fail("invalid escape");
            }
        }
    }

    Value parseNumber() {
        const std::size_t start = position;
        if (position < text.size() && text[position] == '-') position++;
        bool digits = false;
        while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position]))) {
            position++;
            digits = true;
        }
        bool integral = digits;
        if (position < text.size() && text[position] == '.') {
            position++;
            integral = false;
            while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position]))) {
                position++;
                digits = true;
            }
        }
        if (position < text.size() && (text[position] == 'e' || text[position] == 'E')) {
            position++;
            integral = false;
            if (position < text.size() && (text[position] == '+' || text[position] == '-')) position++;
            while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position]))) position++;
        }
        if (!digits) fail("invalid number");
        Value value;
        value.type = Type::Number;
        value.number = std::stod(text.substr(start, position - start));
        value.isInteger = integral;
        value.integer = value.number;
        return value;
    }

    Value parseValue() {
        const char ahead = peek();
        if (ahead == '"') {
            Value value;
            value.type = Type::String;
            value.string = parseString();
            return value;
        }
        if (ahead == '{') {
            take();
            Value value;
            value.type = Type::Object;
            if (peek() == '}') {
                take();
                return value;
            }
            for (;;) {
                const std::string key = parseString();
                expect(':');
                value.object[key] = parseValue();
                const char next = take();
                if (next == '}') return value;
                if (next != ',') fail("expected ',' or '}'");
            }
        }
        if (ahead == '[') {
            take();
            Value value;
            value.type = Type::Array;
            if (peek() == ']') {
                take();
                return value;
            }
            for (;;) {
                value.array.push_back(parseValue());
                const char next = take();
                if (next == ']') return value;
                if (next != ',') fail("expected ',' or ']'");
            }
        }
        if (ahead == 't') {
            if (text.compare(position, 4, "true") != 0) fail("invalid literal");
            position += 4;
            Value value;
            value.type = Type::Boolean;
            value.boolean = true;
            return value;
        }
        if (ahead == 'f') {
            if (text.compare(position, 5, "false") != 0) fail("invalid literal");
            position += 5;
            Value value;
            value.type = Type::Boolean;
            value.boolean = false;
            return value;
        }
        if (ahead == 'n') {
            if (text.compare(position, 4, "null") != 0) fail("invalid literal");
            position += 4;
            return Value{};
        }
        if (ahead == '-' || std::isdigit(static_cast<unsigned char>(ahead))) return parseNumber();
        fail("unexpected character");
    }
};

void dumpString(const std::string& text, std::string& out) {
    out += '"';
    for (const char value : text) {
        switch (value) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(value) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(value));
                    out += buffer;
                } else {
                    out += value;
                }
        }
    }
    out += '"';
}

void dumpValue(const Value& value, std::string& out) {
    switch (value.type) {
        case Type::Null: out += "null"; break;
        case Type::Boolean: out += value.boolean ? "true" : "false"; break;
        case Type::Number: {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), value.isInteger ? "%.0f" : "%g", value.number);
            out += buffer;
            break;
        }
        case Type::String: dumpString(value.string, out); break;
        case Type::Array: {
            out += '[';
            bool first = true;
            for (const auto& item : value.array) {
                if (!first) out += ',';
                first = false;
                dumpValue(item, out);
            }
            out += ']';
            break;
        }
        case Type::Object: {
            out += '{';
            bool first = true;
            for (const auto& [key, item] : value.object) {
                if (!first) out += ',';
                first = false;
                dumpString(key, out);
                out += ':';
                dumpValue(item, out);
            }
            out += '}';
            break;
        }
    }
}

}

Value Parse(const std::string& text) {
    Parser parser{text};
    Value value = parser.parseValue();
    parser.skipSpace();
    if (parser.position != text.size()) parser.fail("trailing content");
    return value;
}

std::string Dump(const Value& value) {
    std::string out;
    dumpValue(value, out);
    return out;
}

}

