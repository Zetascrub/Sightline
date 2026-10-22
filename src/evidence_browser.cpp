#include "evidence_browser.h"
#include "trust_crypto.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace reconclave {
namespace {
struct Fd {
  int value;
  explicit Fd(int fd) : value(fd) {}
  ~Fd() { if (value >= 0) close(value); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
};
bool allowedName(const std::string& name) {
  return !name.empty() && name[0] != '.' && name.find('/') == std::string::npos &&
      name.find('\\') == std::string::npos &&
      name.find_first_of("\r\n\t") == std::string::npos &&
      name.find('\0') == std::string::npos && name.find(".tmp.") == std::string::npos &&
      !(name.size() >= 8 && name.substr(name.size() - 8) == ".partial") &&
      !(name.size() >= 4 && name.substr(name.size() - 4) == ".tmp");
}
bool writeAll(int fd, const char* data, std::size_t size) {
  while (size > 0) {
    const auto n = write(fd, data, size);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    data += n;
    size -= static_cast<std::size_t>(n);
  }
  return true;
}
bool readFile(int directory, const EvidenceFile& file, std::string& bytes, std::string& error) {
  Fd fd(openat(directory, file.name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  struct stat before {}, after {};
  if (fd.value < 0 || fstat(fd.value, &before) != 0 || !S_ISREG(before.st_mode) ||
      before.st_size < 0 || static_cast<std::uint64_t>(before.st_size) != file.size ||
      before.st_mtime != file.modified) {
    error = "File changed or cannot be opened: " + file.name;
    return false;
  }
  bytes.resize(file.size);
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto n = read(fd.value, &bytes[offset], bytes.size() - offset);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { error = "Unable to read: " + file.name; return false; }
    offset += static_cast<std::size_t>(n);
  }
  if (fstat(fd.value, &after) != 0 || before.st_size != after.st_size ||
      before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
      before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec) {
    error = "File is still changing. Stop active captures and retry: " + file.name;
    return false;
  }
  return true;
}
bool tarEntry(int fd, const std::string& name, const std::string& bytes, std::int64_t modified) {
  if (name.size() > 99) return false;
  char header[512]{};
  std::memcpy(header, name.data(), name.size());
  std::snprintf(header + 100, 8, "%07o", 0600);
  std::snprintf(header + 108, 8, "%07o", 0);
  std::snprintf(header + 116, 8, "%07o", 0);
  std::snprintf(header + 124, 12, "%011llo", static_cast<unsigned long long>(bytes.size()));
  std::snprintf(header + 136, 12, "%011llo", static_cast<unsigned long long>(std::max<std::int64_t>(0, modified)));
  std::memset(header + 148, ' ', 8);
  header[156] = '0';
  std::memcpy(header + 257, "ustar", 5);
  std::memcpy(header + 263, "00", 2);
  unsigned sum = 0;
  for (unsigned char c : header) sum += c;
  std::snprintf(header + 148, 7, "%06o", sum);
  header[155] = ' ';
  const char zeros[512]{};
  return writeAll(fd, header, sizeof(header)) && writeAll(fd, bytes.data(), bytes.size()) &&
      writeAll(fd, zeros, (512 - bytes.size() % 512) % 512);
}
std::string fileVersion(const struct stat& info) {
  return std::to_string(info.st_size) + ":" + std::to_string(info.st_mtim.tv_sec) + ":" +
         std::to_string(info.st_mtim.tv_nsec) + ":" + std::to_string(info.st_ino);
}
json::Value contextJson(const EvidenceContext& context) {
  auto value = json::Value::makeObject();
  value.set("project_id", json::Value::makeString(context.project_id));
  value.set("engagement_id", json::Value::makeString(context.engagement_id));
  value.set("operator_id", json::Value::makeString(context.operator_id));
  return value;
}
void readAttribution(int dir, EvidenceFile& file, const struct stat& info) {
  Fd metadata(openat(dir, ".metadata", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  Fd fd(openat(metadata.value, (file.name + ".json").c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  struct stat meta_info {};
  if (fd.value < 0 || fstat(fd.value, &meta_info) != 0 || !S_ISREG(meta_info.st_mode) || meta_info.st_size > 4096) return;
  std::string text;
  char buffer[4096];
  while (text.size() < sizeof(buffer)) {
    const auto n = read(fd.value, buffer, sizeof(buffer) - text.size());
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) return;
    if (!n) break;
    text.append(buffer, static_cast<std::size_t>(n));
  }
  json::Value record;
  std::string error;
  if (!json::parse(text, record, error)) return;
  const auto* version = record.find("file_version");
  const auto* context = record.find("context");
  if (!version || version->asString() != fileVersion(info) || !context) return;
  const auto* project = context->find("project_id");
  const auto* engagement = context->find("engagement_id");
  const auto* operator_id = context->find("operator_id");
  if (!project || !project->isString() || !engagement || !engagement->isString() ||
      !operator_id || !operator_id->isString()) return;
  file.context = {project->asString(), engagement->asString(), operator_id->asString()};
  file.attributed = true;
}
}  // namespace

bool sameEvidenceContext(const EvidenceContext& a, const EvidenceContext& b) {
  return a.project_id == b.project_id && a.engagement_id == b.engagement_id && a.operator_id == b.operator_id;
}

bool attributeEvidenceFile(const std::string& directory, const std::string& name,
                           const EvidenceContext& context, std::string& error) {
  error.clear();
  if (!allowedName(name)) { error = "Invalid artifact name"; return false; }
  Fd dir(open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  struct stat info {};
  if (dir.value < 0 || fstatat(dir.value, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(info.st_mode)) {
    error = "Artifact unavailable for session attribution"; return false;
  }
  if (mkdirat(dir.value, ".metadata", 0700) != 0 && errno != EEXIST) { error = "Cannot create session metadata directory"; return false; }
  Fd meta(openat(dir.value, ".metadata", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  const std::string temporary = name + ".json.tmp";
  Fd output(openat(meta.value, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
  if (output.value < 0) { error = "Cannot create session metadata"; return false; }
  auto record = json::Value::makeObject();
  record.set("file_version", json::Value::makeString(fileVersion(info)));
  record.set("context", contextJson(context));
  const std::string text = json::canonicalStringify(record);
  bool ok = writeAll(output.value, text.data(), text.size()) && fsync(output.value) == 0;
  if (ok) ok = renameat(meta.value, temporary.c_str(), meta.value, (name + ".json").c_str()) == 0;
  unlinkat(meta.value, temporary.c_str(), 0);
  if (!ok) error = "Cannot save session metadata";
  return ok;
}

bool listEvidenceFiles(const std::string& directory, std::vector<EvidenceFile>& files, std::string& error) {
  files.clear(); error.clear();
  DIR* dir = opendir(directory.c_str());
  if (!dir) {
    if (errno == ENOENT) return true;
    error = std::strerror(errno); return false;
  }
  bool ok = true;
  while (true) {
    errno = 0;
    auto* entry = readdir(dir);
    if (!entry) {
      if (errno != 0) { error = std::strerror(errno); ok = false; }
      break;
    }
    if (!allowedName(entry->d_name)) continue;
    struct stat info {};
    if (fstatat(dirfd(dir), entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(info.st_mode)) continue;
    if (files.size() == 256) { error = "More than 256 files. Archive older evidence before browsing."; ok = false; break; }
    EvidenceFile file;
    file.name = entry->d_name;
    file.size = static_cast<std::uint64_t>(info.st_size);
    file.modified = info.st_mtime;
    readAttribution(dirfd(dir), file, info);
    files.push_back(file);
  }
  closedir(dir);
  if (!ok) { files.clear(); return false; }
  std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
    return a.modified != b.modified ? a.modified > b.modified : a.name < b.name;
  });
  return true;
}

bool previewEvidenceFile(const std::string& directory, const std::string& name,
                         std::string& preview, std::string& error) {
  preview.clear(); error.clear();
  if (!allowedName(name)) { error = "Invalid file name"; return false; }
  Fd dir(open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  Fd fd(openat(dir.value, name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  struct stat info {};
  if (fd.value < 0 || fstat(fd.value, &info) != 0 || !S_ISREG(info.st_mode)) {
    error = "File is no longer available. Refresh the evidence list."; return false;
  }
  char buffer[8193];
  std::size_t count = 0;
  while (count < sizeof(buffer)) {
    const auto n = read(fd.value, buffer + count, sizeof(buffer) - count);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) { error = "Unable to read file"; return false; }
    if (n == 0) break;
    count += static_cast<std::size_t>(n);
  }
  for (std::size_t i = 0; i < count; ++i) {
    const auto c = static_cast<unsigned char>(buffer[i]);
    if (c < 32 && c != '\n' && c != '\r' && c != '\t') {
      preview = "Binary artifact. Export the store to inspect this file on your computer.";
      return true;
    }
  }
  preview.assign(buffer, std::min<std::size_t>(count, 8192));
  if (count > 8192) preview += "\n\n[Preview limited to the first 8 KiB]";
  if (preview.empty()) preview = "This file is empty.";
  return true;
}

bool exportEvidenceBundle(const std::string& directory, const EvidenceContext& context,
                          const std::function<bool(std::size_t, std::size_t)>& progress,
                          std::string& path, std::string& error, bool session_only) {
  path.clear(); error.clear();
  std::vector<EvidenceFile> files;
  if (!listEvidenceFiles(directory, files, error)) return false;
  if (session_only) {
    files.erase(std::remove_if(files.begin(), files.end(), [&](const auto& file) {
      return !file.attributed || !sameEvidenceContext(file.context, context);
    }), files.end());
  }
  if (files.empty()) { error = "No matching evidence to export. Save an observation first."; return false; }
  std::uint64_t total = 0;
  for (const auto& file : files) {
    if (file.size > 16 * 1024 * 1024 || file.name.size() > 89) {
      error = "File exceeds export limits (16 MiB or 89 name characters): " + file.name; return false;
    }
    total += file.size;
  }
  if (total > 128 * 1024 * 1024) { error = "Store exceeds the 128 MiB export limit."; return false; }
  Fd dir(open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  if (dir.value < 0 || (mkdirat(dir.value, "exports", 0700) != 0 && errno != EEXIST)) {
    error = "Cannot create export folder"; return false;
  }
  Fd exports(openat(dir.value, "exports", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (exports.value < 0) { error = "Cannot open export folder"; return false; }
  // O_EXCL prevents overwriting existing exports, even across processes.
  std::string name;
  int output = -1;
  for (unsigned i = 0; i < 1000 && output < 0; ++i) {
    name = "evidence-" + std::to_string(static_cast<long long>(time(nullptr))) + "-" + std::to_string(i) + ".tar";
    struct stat existing {};
    if (fstatat(exports.value, name.c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0) continue;
    output = openat(exports.value, (name + ".tmp").c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (output < 0 && errno != EEXIST) break;
  }
  if (output < 0) { error = "Cannot create export. Check free storage."; return false; }
  Fd archive(output);
  struct PartialArchive {
    int directory;
    std::string name;
    ~PartialArchive() { unlinkat(directory, name.c_str(), 0); }
  } partial{exports.value, name + ".tmp"};
  auto manifest = json::Value::makeObject();
  manifest.set("schema", json::Value::makeString("sightline-store-bundle/v1"));
  manifest.set("scope", json::Value::makeString(session_only ? "files attributed to the exact export context" : "all top-level evidence files; may contain multiple sessions"));
  manifest.set("export_project", json::Value::makeString(context.project_id));
  manifest.set("export_engagement", json::Value::makeString(context.engagement_id));
  manifest.set("export_operator", json::Value::makeString(context.operator_id));
  auto entries = json::Value::makeArray();
  bool ok = true;
  for (std::size_t i = 0; i < files.size(); ++i) {
    if (progress && !progress(i, files.size())) { error = "Export cancelled. No archive was published."; ok = false; break; }
    std::string bytes;
    if (!readFile(dir.value, files[i], bytes, error)) { ok = false; break; }
    if (session_only) {
      struct stat current {};
      EvidenceFile check = files[i];
      check.attributed = false;
      if (fstatat(dir.value, files[i].name.c_str(), &current, AT_SYMLINK_NOFOLLOW) == 0)
        readAttribution(dir.value, check, current);
      if (!check.attributed || !sameEvidenceContext(check.context, context)) {
        error = "Session attribution changed during export. Retry after writers stop."; ok = false; break;
      }
    }
    const std::string member = "artifacts/" + files[i].name;
    if (!tarEntry(output, member, bytes, files[i].modified)) { ok = false; error = "Cannot write archive. Check free storage."; break; }
    auto entry = json::Value::makeObject();
    entry.set("path", json::Value::makeString(member));
    entry.set("size_bytes", json::Value::makeNumber(bytes.size()));
    entry.set("sha256", json::Value::makeString(sha256Hex(bytes)));
    if (files[i].attributed) entry.set("context", contextJson(files[i].context));
    entries.push_back(entry);
  }
  manifest.set("files", entries);
  const char zeros[1024]{};
  if (ok && progress && !progress(files.size(), files.size())) { ok = false; error = "Export cancelled. No archive was published."; }
  if (ok) ok = tarEntry(output, "bundle-manifest.json", json::canonicalStringify(manifest) + "\n", time(nullptr)) &&
      writeAll(output, zeros, sizeof(zeros)) && fsync(output) == 0;
  // Publish with a hard link so an existing archive can never be replaced.
  if (ok) ok = linkat(exports.value, (name + ".tmp").c_str(), exports.value, name.c_str(), 0) == 0;
  unlinkat(exports.value, (name + ".tmp").c_str(), 0);
  if (!ok) { if (error.empty()) error = "Could not publish export. Check storage and retry."; return false; }
  path = directory + "/exports/" + name;
  return true;
}
}  // namespace reconclave
