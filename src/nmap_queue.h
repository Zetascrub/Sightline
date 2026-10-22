#pragma once
#include "host_knowledge.h"
namespace reconclave {
// Process at most one ready artifact per tick; no traffic and no UI/database races.
void processNmapImport(HostKnowledge& knowledge, const std::string& directory);
}
