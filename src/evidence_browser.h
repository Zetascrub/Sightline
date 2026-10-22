#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "evidence_store.h"

namespace reconclave {
struct EvidenceFile {
  std::string name;
  std::uint64_t size = 0;
  std::int64_t modified = 0;
  bool attributed = false;
  EvidenceContext context;
};

// Attribute a completed file to the context captured when its operation started.
// Metadata is separate from the artifact and invalidated if the artifact changes.
bool attributeEvidenceFile(const std::string& directory, const std::string& name,
                           const EvidenceContext& context, std::string& error);
bool sameEvidenceContext(const EvidenceContext& a, const EvidenceContext& b);

// Flat, newest-first inventory. Excludes links, hidden files and temporary files.
bool listEvidenceFiles(const std::string& directory, std::vector<EvidenceFile>& files,
                       std::string& error);
bool previewEvidenceFile(const std::string& directory, const std::string& name,
                         std::string& preview, std::string& error);

// A bounded snapshot of the whole store, not a session-filtered export. The
// manifest hashes the exact archived bytes. Exports go in a separate subfolder.
// Return false from progress to cancel; incomplete archives are removed.
bool exportEvidenceBundle(const std::string& directory, const EvidenceContext& context,
                          const std::function<bool(std::size_t, std::size_t)>& progress,
                          std::string& path, std::string& error, bool session_only = false);
}  // namespace reconclave
