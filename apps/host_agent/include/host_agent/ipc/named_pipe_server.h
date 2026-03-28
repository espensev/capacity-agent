#ifndef HOST_AGENT_IPC_NAMED_PIPE_SERVER_H
#define HOST_AGENT_IPC_NAMED_PIPE_SERVER_H

#include <functional>
#include <string>
#include <string_view>

namespace host_agent::ipc {

using MessageHandler = std::function<void(std::string_view)>;

class NamedPipeServer {
public:
    explicit NamedPipeServer(std::wstring pipe_name);

    bool Run(const MessageHandler& handler);
    void Stop();

private:
    std::wstring full_pipe_name_;
    bool running_ = false;
};

}  // namespace host_agent::ipc

#endif  // HOST_AGENT_IPC_NAMED_PIPE_SERVER_H

