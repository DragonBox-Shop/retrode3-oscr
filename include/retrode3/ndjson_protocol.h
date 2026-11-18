#pragma once
#include <cstddef>
#include <string>

namespace retrode3 {
struct WebCommand {
    std::string action;
    std::string core;
    std::string request_id;
    std::string output;
    std::string mapping;
    std::size_t size_bytes;
    bool has_size;
    WebCommand() : size_bytes(0), has_size(false) {}
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
