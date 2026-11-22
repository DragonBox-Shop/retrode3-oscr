#include "retrode3/ndjson_protocol.h"
#include <cerrno>
#include <cstdio>
#include <limits>
#include <poll.h>
#include <stdexcept>
#include <unistd.h>

namespace retrode3 {
namespace {
std::string field(const JsonV3 &obj, const char *key, bool required = false) {
    if (!obj.has(key)) {
        if (required) throw std::runtime_error(std::string("missing command field: ") + key);
        return "";
    }
    return obj.at(key).text();
}
unsigned integer_field(const JsonV3 &obj, const char *key, unsigned max) {
    std::uint64_t n = obj.at(key).number();
    if (n > max) throw std::runtime_error(std::string("out-of-range integer: ") + key);
    return static_cast<unsigned>(n);
}
bool safe_token(const std::string &s) {
    if (s.empty() || s.size() > 63) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.'))
            return false;
    }
    return true;
}
void check_selection(WebCommand &cmd, const JsonV3 &v) {
    if (v.kind != JsonV3::Object)
        throw std::runtime_error("selection must be an object");
    for (std::map<std::string, JsonV3>::const_iterator i = v.object.begin();
         i != v.object.end(); ++i)
        if (i->first != "version" && i->first != "schema_id" &&
            i->first != "mode" && i->first != "candidate_id" &&
            i->first != "layout_id" && i->first != "parameters")
            throw std::runtime_error("unknown selection field: " + i->first);
    if (integer_field(v, "version", 100) != 1)
        throw std::runtime_error("unsupported selection version");
    cmd.selection_schema = field(v, "schema_id", true);
    cmd.selection_mode = field(v, "mode", true);
    if (!safe_token(cmd.selection_schema))
        throw std::runtime_error("invalid selection schema_id");
    cmd.selection_candidate = field(v, "candidate_id");
    cmd.selection_layout = field(v, "layout_id");
    if ((!cmd.selection_candidate.empty() && !safe_token(cmd.selection_candidate)) ||
        (!cmd.selection_layout.empty() && !safe_token(cmd.selection_layout)))
        throw std::runtime_error("invalid selection candidate/layout ID");
    if (v.has("parameters")) {
        const JsonV3 &params = v.at("parameters");
        if (params.kind != JsonV3::Object || params.object.size() > 32)
            throw std::runtime_error("invalid selection parameters");
        for (std::map<std::string, JsonV3>::const_iterator i = params.object.begin();
             i != params.object.end(); ++i) {
            if (!safe_token(i->first) || i->first.size() > 47 ||
                (i->second.kind != JsonV3::Integer &&
                 i->second.kind != JsonV3::Boolean &&
                 i->second.kind != JsonV3::String) ||
                (i->second.kind == JsonV3::Integer &&
                 i->second.integer > 0xffffffffull) ||
                (i->second.kind == JsonV3::String &&
                 i->second.string.size() > 127))
                throw std::runtime_error("invalid selection parameter");
        }
        cmd.selection_parameters = params.object;
    }
    if (cmd.selection_mode == "auto") {
        if (!cmd.selection_candidate.empty() || !cmd.selection_layout.empty() ||
            !cmd.selection_parameters.empty() || v.has("parameters") ||
            v.has("candidate_id") || v.has("layout_id"))
            throw std::runtime_error("auto selection cannot contain overrides");
    } else if (cmd.selection_mode == "candidate") {
        if (cmd.selection_candidate.empty() || v.has("layout_id") ||
            v.has("parameters"))
            throw std::runtime_error("candidate selection requires only candidate_id");
    } else if (cmd.selection_mode == "layout") {
        if (cmd.selection_layout.empty() || v.has("candidate_id") ||
            v.has("parameters"))
            throw std::runtime_error("layout selection requires only layout_id");
    } else if (cmd.selection_mode == "override") {
        if (!v.has("parameters") || cmd.selection_parameters.empty() ||
            v.has("candidate_id") || v.has("layout_id"))
            throw std::runtime_error("override requires parameters and no candidate/layout ID");
    } else throw std::runtime_error("invalid selection mode");
    cmd.has_selection = true;
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
    JsonV3 obj = parse_json_v3(line);
    if (obj.kind != JsonV3::Object)
        throw std::runtime_error("command must be a JSON object");
    for (std::map<std::string, JsonV3>::const_iterator i = obj.object.begin();
         i != obj.object.end(); ++i)
        if (i->first != "command" && i->first != "request_id" &&
            i->first != "output" && i->first != "mapping" &&
            i->first != "size_bytes" && i->first != "core" &&
            i->first != "slot" &&
            i->first != "candidate_id" && i->first != "layout_id" &&
            i->first != "mapper" && i->first != "submapper" &&
            i->first != "prg_kib" && i->first != "chr_kib" &&
            i->first != "mirroring" && i->first != "battery" &&
            i->first != "selection")
            throw std::runtime_error("unknown command field: " + i->first);
    WebCommand cmd;
    cmd.action = field(obj, "command", true);
    cmd.core = field(obj, "core");
    cmd.slot = field(obj, "slot");
    if (!cmd.slot.empty() && (!safe_token(cmd.slot) || cmd.slot.size() > 48))
        throw std::runtime_error("invalid slot identifier");
    if (cmd.core.size() > 48) throw std::runtime_error("invalid core identifier");
    for (std::size_t k = 0; k < cmd.core.size(); ++k) {
        unsigned char c = static_cast<unsigned char>(cmd.core[k]);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            throw std::runtime_error("invalid core identifier");
    }
    cmd.request_id = field(obj, "request_id", true);
    if (cmd.request_id.empty() || cmd.request_id.size() > 64)
        throw std::runtime_error("invalid request_id length");
    for (std::size_t i = 0; i < cmd.request_id.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(cmd.request_id[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') || c == '_' || c == '-'))
            throw std::runtime_error("invalid request_id character");
    }
    cmd.candidate_id = field(obj, "candidate_id");
    cmd.layout_id = field(obj, "layout_id");
    if ((!cmd.candidate_id.empty() && !safe_token(cmd.candidate_id)) ||
        (!cmd.layout_id.empty() && !safe_token(cmd.layout_id)) ||
        cmd.candidate_id.size() > 31 || cmd.layout_id.size() > 31)
        throw std::runtime_error("invalid legacy candidate/layout identifier");
    const bool m = obj.has("mapper") || obj.has("submapper") ||
                   obj.has("prg_kib") || obj.has("chr_kib") ||
                   obj.has("mirroring") || obj.has("battery");
    if (m) {
        if (!obj.has("mapper") || !obj.has("submapper") ||
            !obj.has("prg_kib") || !obj.has("chr_kib") ||
            !obj.has("mirroring") || !obj.has("battery"))
            throw std::runtime_error("manual NES layout requires all fields");
        if (!cmd.candidate_id.empty() || !cmd.layout_id.empty())
            throw std::runtime_error("manual layout cannot mix candidate/layout IDs");
        cmd.mapper = integer_field(obj, "mapper", 4095);
        cmd.submapper = integer_field(obj, "submapper", 255);
        cmd.prg_kib = integer_field(obj, "prg_kib", 65536);
        cmd.chr_kib = integer_field(obj, "chr_kib", 65536);
        cmd.battery = integer_field(obj, "battery", 1);
        cmd.mirroring = field(obj, "mirroring", true);
        if (cmd.mirroring != "horizontal" && cmd.mirroring != "vertical" &&
            cmd.mirroring != "four_screen")
            throw std::runtime_error("invalid NES mirroring");
        cmd.has_manual = true;
    }
    if (obj.has("selection")) {
        if (m || obj.has("candidate_id") || obj.has("layout_id") ||
            obj.has("mapping") || obj.has("size_bytes"))
            throw std::runtime_error("selection cannot mix legacy read fields");
        check_selection(cmd, obj.at("selection"));
    }
    cmd.output = field(obj, "output");
    cmd.mapping = field(obj, "mapping");
    if (!cmd.mapping.empty() && cmd.mapping != "lo" && cmd.mapping != "hi")
        throw std::runtime_error("invalid mapping");
    if (cmd.output.size() > 1024 || cmd.output.find('\n') != std::string::npos ||
        cmd.output.find('\r') != std::string::npos)
        throw std::runtime_error("invalid output path");
    if (obj.has("size_bytes")) {
        std::uint64_t n = obj.at("size_bytes").number();
        if (n < 131072 || n > 4194304)
            throw std::runtime_error("size_bytes outside supported SNES range");
        cmd.size_bytes = static_cast<std::size_t>(n);
        cmd.has_size = true;
    }
    if (cmd.action != "probe" && cmd.action != "read_rom" &&
        cmd.action != "cancel" && cmd.action != "get_capabilities" &&
        cmd.action != "list_cores" && cmd.action != "probe_slot" &&
        cmd.action != "get_flash_capabilities" && cmd.action != "flash_probe")
        throw std::runtime_error("unsupported command");
    if (cmd.action == "list_cores" && (!cmd.core.empty() || !cmd.slot.empty()))
        throw std::runtime_error("list_cores takes no core identifier");
    if (cmd.action == "probe_slot") {
        if (cmd.slot.empty() || !cmd.core.empty())
            throw std::runtime_error("probe_slot requires slot and forbids core");
    } else if (!cmd.slot.empty())
        throw std::runtime_error("slot only allowed for probe_slot");
    if (cmd.action == "read_rom" && cmd.output.empty())
        throw std::runtime_error("read_rom requires output");
    if (cmd.action != "read_rom" &&
        (!cmd.output.empty() || !cmd.mapping.empty() || cmd.has_size ||
         !cmd.candidate_id.empty() || !cmd.layout_id.empty() ||
         cmd.has_manual || cmd.has_selection))
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
