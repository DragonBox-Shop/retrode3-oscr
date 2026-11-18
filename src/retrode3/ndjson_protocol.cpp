#include "retrode3/ndjson_protocol.h"

#include <cerrno>
#include <cstdio>
#include <limits>
#include <map>
#include <poll.h>
#include <stdexcept>
#include <unistd.h>

namespace retrode3 {
namespace {
struct Value {
    bool numeric;
    std::string text;
    Value() : numeric(false) {}
};

class Parser {
public:
    explicit Parser(std::string const &s) : s_(s), pos_(0) {}
    std::map<std::string, Value> object() {
        skip(); expect('{');
        std::map<std::string, Value> result;
        skip();
        if (consume('}')) { skip(); finish(); return result; }
        do {
            skip();
            std::string key = string();
            skip(); expect(':'); skip();
            Value val;
            if (peek() == '"') val.text = string();
            else {
                val.numeric = true;
                std::size_t start = pos_;
                if (peek() < '0' || peek() > '9') fail();
                if (peek() == '0') ++pos_;
                else while (peek() >= '0' && peek() <= '9') ++pos_;
                val.text = s_.substr(start, pos_ - start);
            }
            if (!result.insert(std::make_pair(key, val)).second)
                throw std::runtime_error("duplicate JSON command field");
            skip();
            if (consume('}')) { skip(); finish(); return result; }
            expect(',');
        } while (true);
    }
private:
    std::string const &s_;
    std::size_t pos_;
    void fail() const { throw std::runtime_error("malformed JSON command"); }
    char peek() const { return pos_ < s_.size() ? s_[pos_] : '\0'; }
    void skip() { while (peek() == ' ' || peek() == '\t' || peek() == '\r') ++pos_; }
    bool consume(char x) { if (peek() != x) return false; ++pos_; return true; }
    void expect(char x) { if (!consume(x)) fail(); }
    void finish() { if (pos_ != s_.size()) fail(); }
    std::string string() {
        expect('"');
        std::string out;
        while (true) {
            char c = peek();
            if (!c) fail();
            ++pos_;
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 32) fail();
            if (c != '\\') { out += c; continue; }
            c = peek(); if (!c) fail(); ++pos_;
            switch(c) {
            case '"': case '\\': case '/': out += c; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            default: throw std::runtime_error("unsupported JSON escape (use UTF-8)");
            }
        }
    }
};

std::string value(std::map<std::string, Value> const &obj,
                  std::string const &key, bool required = false) {
    std::map<std::string, Value>::const_iterator i = obj.find(key);
    if (i == obj.end()) {
        if (required) throw std::runtime_error("missing command field: " + key);
        return "";
    }
    if (i->second.numeric) throw std::runtime_error("expected string: " + key);
    return i->second.text;
}

std::size_t positive_size(Value const &v) {
    if (!v.numeric) throw std::runtime_error("size_bytes must be an integer");
    std::size_t n = 0;
    for (std::size_t k = 0; k < v.text.size(); ++k) {
        unsigned d = static_cast<unsigned>(v.text[k] - '0');
        if (n > (std::numeric_limits<std::size_t>::max() - d) / 10)
            throw std::runtime_error("size_bytes overflow");
        n = n * 10 + d;
    }
    if (n < 131072 || n > 4194304)
        throw std::runtime_error("size_bytes outside supported SNES range");
    return n;
}
}

NdjsonProtocol::NdjsonProtocol(bool interactive)
    : interactive_(interactive), cancelled_(false), eof_(false) {}

std::string NdjsonProtocol::quote(std::string const &s) {
    static char const hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (std::size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) {
            out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15];
        } else out += static_cast<char>(c);
    }
    out += '"';
    return out;
}
std::string NdjsonProtocol::id(std::string const &id_) {
    return "\"request_id\":" + quote(id_);
}
void NdjsonProtocol::emit(std::string const &json) {
    if (std::printf("%s\n", json.c_str()) < 0 || std::fflush(stdout) != 0)
        throw std::runtime_error("NDJSON output pipe unavailable");
}

WebCommand NdjsonProtocol::parse(std::string const &line) {
    std::map<std::string, Value> obj = Parser(line).object();
    for (std::map<std::string, Value>::const_iterator i = obj.begin(); i != obj.end(); ++i)
        if (i->first != "command" && i->first != "request_id" &&
            i->first != "output" && i->first != "mapping" && i->first != "size_bytes" &&
            i->first != "core")
            throw std::runtime_error("unknown command field: " + i->first);
    WebCommand cmd;
    cmd.action = value(obj, "command", true);
    cmd.core = value(obj, "core");
    if (cmd.core.size() > 48) throw std::runtime_error("invalid core identifier");
    for (std::size_t k = 0; k < cmd.core.size(); ++k) {
        unsigned char c = static_cast<unsigned char>(cmd.core[k]);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            throw std::runtime_error("invalid core identifier");
    }
    cmd.request_id = value(obj, "request_id", true);
    if (cmd.request_id.empty() || cmd.request_id.size() > 64)
        throw std::runtime_error("invalid request_id length");
    for (std::size_t i = 0; i < cmd.request_id.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(cmd.request_id[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') || c == '_' || c == '-'))
            throw std::runtime_error("invalid request_id character");
    }
    cmd.output = value(obj, "output");
    cmd.mapping = value(obj, "mapping");
    if (cmd.mapping.size() && cmd.mapping != "lo" && cmd.mapping != "hi")
        throw std::runtime_error("invalid mapping");
    if (cmd.output.size() > 1024 || cmd.output.find('\n') != std::string::npos ||
        cmd.output.find('\r') != std::string::npos)
        throw std::runtime_error("invalid output path");
    std::map<std::string, Value>::const_iterator sz = obj.find("size_bytes");
    if (sz != obj.end()) { cmd.size_bytes = positive_size(sz->second); cmd.has_size = true; }
    if (cmd.action != "probe" && cmd.action != "read_rom" && cmd.action != "cancel"
        && cmd.action != "get_capabilities" && cmd.action != "list_cores")
        throw std::runtime_error("unsupported command");
    if (cmd.action == "list_cores" && !cmd.core.empty())
        throw std::runtime_error("list_cores takes no core identifier");
    if (cmd.action == "read_rom" && cmd.output.empty())
        throw std::runtime_error("read_rom requires output");
    if (cmd.action != "read_rom" &&
        (!cmd.output.empty() || !cmd.mapping.empty() || cmd.has_size))
        throw std::runtime_error("unexpected parameters for command");
    return cmd;
}

bool NdjsonProtocol::read_line(std::string &line, bool block) {
    while (true) {
        std::size_t end = input_.find('\n');
        if (end != std::string::npos) {
            line = input_.substr(0, end);
            input_.erase(0, end + 1);
            if (line.size() > 8192) throw std::runtime_error("JSON command too long");
            return true;
        }
        if (input_.size() > 8192) throw std::runtime_error("JSON command too long");
        if (eof_) {
            if (!input_.empty()) throw std::runtime_error("unterminated JSON command");
            return false;
        }
        struct pollfd pfd = {0, POLLIN, 0};
        int result;
        do { result = ::poll(&pfd, 1, block ? -1 : 0); }
        while (result < 0 && errno == EINTR);
        if (result < 0) throw std::runtime_error("stdin poll failed");
        if (!result) return false;
        char buffer[4096];
        ssize_t n;
        do { n = ::read(0, buffer, sizeof(buffer)); }
        while (n < 0 && errno == EINTR);
        if (n < 0) throw std::runtime_error("stdin read failed");
        if (n == 0) { eof_ = true; continue; }
        input_.append(buffer, static_cast<std::size_t>(n));
    }
}

bool NdjsonProtocol::next(WebCommand &command) {
    std::string line;
    while (read_line(line, true)) {
        try { command = parse(line); return true; }
        catch (std::exception const &e) {
            emit("{\"event\":\"error\",\"code\":\"invalid_command\",\"message\":"
                 + quote(e.what()) + "}");
        }
    }
    return false;
}

void NdjsonProtocol::poll_cancel(std::string const &request_id) {
    if (!interactive_) return;
    std::string line;
    while (read_line(line, false)) {
        try {
            WebCommand command = parse(line);
            if (command.action == "cancel" && command.request_id == request_id)
                cancelled_ = true;
            else emit("{\"event\":\"error\",\"code\":\"busy\","
                      + id(command.request_id) + "}");
        } catch (std::exception const &e) {
            emit("{\"event\":\"error\",\"code\":\"invalid_command\","
                 "\"message\":" + quote(e.what()) + "}");
        }
    }
}
}
