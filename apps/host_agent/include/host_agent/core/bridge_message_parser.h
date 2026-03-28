#ifndef HOST_AGENT_CORE_BRIDGE_MESSAGE_PARSER_H
#define HOST_AGENT_CORE_BRIDGE_MESSAGE_PARSER_H

#include "host_agent/model/bridge_messages.h"

#include <string>
#include <string_view>

namespace host_agent::core {

bool ParseBridgeMessage(std::string_view json_line,
                        model::BridgeMessage* out_message,
                        std::string* out_error);

}  // namespace host_agent::core

#endif  // HOST_AGENT_CORE_BRIDGE_MESSAGE_PARSER_H

