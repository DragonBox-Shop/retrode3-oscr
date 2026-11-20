#ifndef RETRODE3_JSON_V3_H
#define RETRODE3_JSON_V3_H
/* Small bounded JSON reader for control-plane data. C++11, no dependencies.
 * Strict object keys, nesting and integer handling; never evaluate rules. */
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace retrode3 {
struct JsonV3 {
    enum Kind { Null, Boolean, Integer, String, Object, Array };
    Kind kind;
    bool boolean;
    std::uint64_t integer;
    std::string string;
    std::map<std::string, JsonV3> object;
    std::vector<JsonV3> array;
    JsonV3() : kind(Null), boolean(false), integer(0) {}
    const JsonV3 &at(const char *key) const {
        std::map<std::string, JsonV3>::const_iterator it = object.find(key);
        if (kind != Object || it == object.end())
            throw std::runtime_error(std::string("missing JSON field: ") + key);
        return it->second;
    }
    bool has(const char *key) const {
        return kind == Object && object.count(key) != 0;
    }
    std::string text() const {
        if (kind != String) throw std::runtime_error("expected JSON string");
        return string;
    }
    std::uint64_t number() const {
        if (kind != Integer) throw std::runtime_error("expected JSON integer");
        return integer;
    }
};
class JsonReaderV3 {
    const std::string &s;
    std::size_t p;
    unsigned nodes;
    char peek() const { return p == s.size() ? '\0' : s[p]; }
    void ws() { while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n') ++p; }
    void expect(char ch) {
        if (peek() != ch) throw std::runtime_error("invalid JSON syntax");
        ++p;
    }
    bool take(char ch) { if (peek() != ch) return false; ++p; return true; }
    static int digit(char c) {
        return c >= '0' && c <= '9' ? c - '0' :
               c >= 'a' && c <= 'f' ? c - 'a' + 10 :
               c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    }
    unsigned unicode() {
        unsigned cp = 0;
        for (int i = 0; i != 4; ++i) {
            int n = digit(peek());
            if (n < 0) throw std::runtime_error("invalid JSON unicode escape");
            ++p;
            cp = (cp << 4) | static_cast<unsigned>(n);
        }
        return cp;
    }
    static void utf8(std::string &s, unsigned cp) {
        if (cp <= 0x7f) s += static_cast<char>(cp);
        else if (cp <= 0x7ff) {
            s += static_cast<char>(0xc0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 63));
        } else if (cp <= 0xffff) {
            s += static_cast<char>(0xe0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 63));
            s += static_cast<char>(0x80 | (cp & 63));
        } else {
            s += static_cast<char>(0xf0 | (cp >> 18));
            s += static_cast<char>(0x80 | ((cp >> 12) & 63));
            s += static_cast<char>(0x80 | ((cp >> 6) & 63));
            s += static_cast<char>(0x80 | (cp & 63));
        }
    }
    std::string str() {
        expect('"');
        std::string out;
        while (true) {
            if (p == s.size()) throw std::runtime_error("unterminated JSON string");
            unsigned char c = static_cast<unsigned char>(s[p++]);
            if (c == '"') break;
            if (c < 32) throw std::runtime_error("JSON control character");
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (p == s.size()) throw std::runtime_error("unterminated JSON escape");
            char e = s[p++];
            switch (e) {
            case '"': case '\\': case '/': out += e; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = unicode();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    expect('\\'); expect('u');
                    unsigned lo = unicode();
                    if (lo < 0xdc00 || lo > 0xdfff)
                        throw std::runtime_error("invalid JSON surrogate");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + lo - 0xdc00;
                } else if (cp >= 0xdc00 && cp <= 0xdfff)
                    throw std::runtime_error("unpaired JSON surrogate");
                utf8(out, cp);
                break;
            }
            default: throw std::runtime_error("invalid JSON escape");
            }
        }
        return out;
    }
    JsonV3 value(unsigned depth) {
        if (depth > 20 || ++nodes > 2048)
            throw std::runtime_error("JSON nesting/complexity exceeded");
        ws();
        JsonV3 v;
        if (take('{')) {
            v.kind = JsonV3::Object;
            ws();
            if (take('}')) return v;
            do {
                ws();
                std::string key = str();
                ws(); expect(':');
                JsonV3 child = value(depth + 1);
                if (!v.object.insert(std::make_pair(key, child)).second)
                    throw std::runtime_error("duplicate JSON key");
                ws();
                if (take('}')) break;
                expect(',');
            } while (true);
        } else if (take('[')) {
            v.kind = JsonV3::Array;
            ws();
            if (take(']')) return v;
            do {
                v.array.push_back(value(depth + 1));
                ws();
                if (take(']')) break;
                expect(',');
            } while (true);
        } else if (peek() == '"') {
            v.kind = JsonV3::String; v.string = str();
        } else if (peek() >= '0' && peek() <= '9') {
            v.kind = JsonV3::Integer;
            do {
                unsigned n = static_cast<unsigned>(s[p++] - '0');
                if (v.integer > (std::numeric_limits<std::uint64_t>::max() - n) / 10)
                    throw std::runtime_error("JSON integer overflow");
                v.integer = v.integer * 10 + n;
            } while (peek() >= '0' && peek() <= '9');
            if (peek() == '.' || peek() == 'e' || peek() == 'E')
                throw std::runtime_error("JSON floating point not supported");
        } else {
            const char *word = NULL;
            if (peek() == 't') { word = "true"; v.kind = JsonV3::Boolean; v.boolean = true; }
            else if (peek() == 'f') { word = "false"; v.kind = JsonV3::Boolean; }
            else if (peek() == 'n') { word = "null"; }
            else throw std::runtime_error("invalid JSON value");
            while (*word) expect(*word++);
        }
        return v;
    }
public:
    explicit JsonReaderV3(const std::string &input) : s(input), p(0), nodes(0) {}
    JsonV3 parse() {
        JsonV3 v = value(0);
        ws();
        if (p != s.size()) throw std::runtime_error("trailing JSON data");
        return v;
    }
};
inline JsonV3 parse_json_v3(const std::string &s) {
    if (s.size() > 65536) throw std::runtime_error("JSON document too large");
    return JsonReaderV3(s).parse();
}
inline std::string json_quote_v3(const std::string &s) {
    static const char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (std::size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) {
            out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15];
        } else out += static_cast<char>(c);
    }
    return out + "\"";
}
inline std::string stringify_json_v3(const JsonV3 &v) {
    switch (v.kind) {
    case JsonV3::Null: return "null";
    case JsonV3::Boolean: return v.boolean ? "true" : "false";
    case JsonV3::Integer: return std::to_string(v.integer);
    case JsonV3::String: return json_quote_v3(v.string);
    case JsonV3::Object: {
        std::string s = "{";
        for (std::map<std::string, JsonV3>::const_iterator i = v.object.begin();
             i != v.object.end(); ++i) {
            if (i != v.object.begin()) s += ",";
            s += json_quote_v3(i->first) + ":" + stringify_json_v3(i->second);
        }
        return s + "}";
    }
    case JsonV3::Array: {
        std::string s = "[";
        for (std::size_t i = 0; i < v.array.size(); ++i) {
            if (i) s += ",";
            s += stringify_json_v3(v.array[i]);
        }
        return s + "]";
    }
    }
    throw std::runtime_error("invalid JSON type");
}
}
#endif
