#pragma once
#include <cstddef>
#include <string>
#include <map>
#include "retrode3/json_v3.h"

namespace retrode3 {
struct WebCommand {
    std::string action;
    std::string core;
    std::string slot;
    std::string request_id;
    std::string output;
    std::string source;
    std::string mapping;
    std::string candidate_id;
    std::string layout_id;
    std::string mirroring;
    unsigned mapper, submapper, prg_kib, chr_kib, battery;
    bool has_manual;
    bool has_selection;
    std::string selection_schema;
    std::string selection_mode;
    std::string selection_candidate;
    std::string selection_layout;
    std::map<std::string, JsonV3> selection_parameters;
    std::size_t size_bytes;
    bool has_size;
    WebCommand() : mapper(0), submapper(0), prg_kib(0), chr_kib(0), battery(0), has_manual(false), has_selection(false), size_bytes(0), has_size(false) {}
};

class NdjsonProtocol {
public:
    explicit NdjsonProtocol(bool interactive);
    bool next(WebCommand &command);
    void poll_cancel(std::string const &request_id);
    bool cancelled() const { return cancelled_; }
    void reset_cancel() { cancelled_ = false; }
    void emit(std::string const &json);
    bool interactive() const { return interactive_; }
    static std::string quote(std::string const &s);
    static std::string id(std::string const &request_id);
private:
    bool interactive_;
    bool cancelled_;
    bool eof_;
    std::string input_;
    bool read_line(std::string &line, bool block);
    static WebCommand parse(std::string const &line);
};
}
