#pragma once
#include "host_knowledge.h"
#include <string>

namespace reconclave {
// Import one receive-only observation file per tick. Passive evidence can add
// host identity, but must never create an open-port or service observation.
void processPassiveImport(HostKnowledge& knowledge, const std::string& directory);
}
