#ifndef CORE_LAUNCHER_JSON_HPP
#define CORE_LAUNCHER_JSON_HPP

#include <map>
#include <string>
#include <vector>

namespace Json {

enum class Type { Null, Boolean, Number, String, Array, Object };

struct Value {
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    double integer = 0.0;
    bool isInteger = false;
    std::string string;
    std::vector<Value> array;
    std::map<std::string, Value> object;

    const Value* find(const std::string& key) const;
    std::string asString(const std::string& fallback = "") const;
    bool asBool(bool fallback = false) const;
};

Value Parse(const std::string& text);
std::string Dump(const Value& value);

}

#endif
