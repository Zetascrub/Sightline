#include "evidence_browser.h"
#include "ui_jobs.h"
#include <cstdio>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string root = argv[1];
  int failures = 0;
  auto check = [&](bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
  };
  std::string error, preview, path;
  std::vector<reconclave::EvidenceFile> files;
  check(reconclave::listEvidenceFiles(root + "/missing", files, error) && files.empty(), "missing store is empty");
  check(!reconclave::exportEvidenceBundle(root, {}, {}, path, error), "empty export rejected");
  std::ofstream(root + "/scan.csv") << "host,port\n192.0.2.1,443\n";
  std::ofstream(root + "/notes.txt") << std::string(9000, 'a');
  std::ofstream(root + "/.hidden") << "hidden";
  std::ofstream(root + "/partial.tmp") << "incomplete";
  std::ofstream(root + "/screenshot.tmp.png") << "incomplete";
  std::ofstream(root + "/capture.pcap", std::ios::binary).write("abc\0xyz", 7);
  symlink("scan.csv", (root + "/linked.csv").c_str());
  mkfifo((root + "/pipe").c_str(), 0600);
  check(reconclave::listEvidenceFiles(root, files, error) && files.size() == 3, "inventory excludes links, partials, pipes and hidden files");
  check(reconclave::previewEvidenceFile(root, "notes.txt", preview, error) && preview.find("limited") != std::string::npos && preview.size() < 8300, "large preview bounded");
  check(reconclave::previewEvidenceFile(root, "capture.pcap", preview, error) && preview.find("Binary") != std::string::npos, "binary preview identified");
  check(!reconclave::previewEvidenceFile(root, "../outside", preview, error), "traversal refused");
  check(!reconclave::previewEvidenceFile(root, "linked.csv", preview, error), "symlink preview refused");
  check(!reconclave::previewEvidenceFile(root, "pipe", preview, error), "pipe preview does not block");
  check(!reconclave::exportEvidenceBundle(root, {}, [](auto, auto) { return false; }, path, error) && path.empty(), "cancelled export not published");
  check(!reconclave::exportEvidenceBundle(root, {}, [&](auto done, auto total) { return done != total; }, path, error), "late cancellation not published");
  std::ofstream(root + "/too-large").seekp(17 * 1024 * 1024).put('x');
  check(!reconclave::exportEvidenceBundle(root, {}, {}, path, error), "oversized artifact rejected");
  unlink((root + "/too-large").c_str());
  check(!reconclave::exportEvidenceBundle(root, {}, [&](auto done, auto) {
    if (done == 0) std::ofstream(root + "/scan.csv", std::ios::app) << "changed";
    return true;
  }, path, error) && path.empty(), "changing file prevents publishing a partial snapshot");
  std::ofstream(root + "/scan.csv") << "host,port\n192.0.2.1,443\n";
  check(reconclave::exportEvidenceBundle(root, {"project", "engagement", "operator"}, {}, path, error), "archive exported");
  const auto first = path;
  check(reconclave::exportEvidenceBundle(root, {}, {}, path, error) && path != first, "repeated export does not replace previous bundle");

  const reconclave::EvidenceContext first_session{"one", "site", "tester"};
  const reconclave::EvidenceContext other_session{"two", "site", "tester"};
  check(reconclave::attributeEvidenceFile(root, "scan.csv", first_session, error), "completed file gets context");
  check(reconclave::attributeEvidenceFile(root, "notes.txt", other_session, error), "another session gets distinct context");
  check(!reconclave::attributeEvidenceFile(root, "linked.csv", first_session, error), "links cannot be attributed");
  check(reconclave::listEvidenceFiles(root, files, error), "attributed inventory loads");
  unsigned attributed = 0;
  for (const auto& file : files) if (file.attributed) ++attributed;
  check(attributed == 2, "legacy binary remains unattributed");
  check(reconclave::exportEvidenceBundle(root, first_session, {}, path, error, true), "session-specific bundle exports");
  check(!reconclave::exportEvidenceBundle(root, {"missing", "site", "tester"}, {}, path, error, true), "empty session never falls back to whole store");
  std::ofstream(root + "/scan.csv", std::ios::app) << "new";
  check(reconclave::listEvidenceFiles(root, files, error), "changed inventory loads");
  for (const auto& file : files) if (file.name == "scan.csv") check(!file.attributed, "changed file invalidates attribution");
  std::ofstream(root + "/scan.csv") << "host,port\n192.0.2.1,443\n";
  reconclave::UiJobs jobs;
  using reconclave::UiJob;
  using reconclave::UiJobState;
  check(jobs.activeCount() == 0, "jobs start idle");
  check(jobs.begin(UiJob::Capture, "capture") && !jobs.begin(UiJob::Capture, "duplicate"), "duplicate job rejected");
  check(jobs.begin(UiJob::Evidence, "export") && jobs.activeCount() == 2, "independent jobs tracked");
  jobs.cancelling(UiJob::Capture);
  check(jobs.get(UiJob::Capture).active(), "stopping remains active until exit");
  jobs.finish(UiJob::Capture, false, "stopped", true);
  check(jobs.get(UiJob::Capture).state == UiJobState::Cancelled && jobs.activeCount() == 1, "cancelled state distinct from failure");
  jobs.finish(UiJob::Evidence, false, "disk full");
  check(jobs.get(UiJob::Evidence).state == UiJobState::Failed, "failure retained");
  jobs.finish(UiJob::Evidence, true, "late completion");
  check(jobs.get(UiJob::Evidence).state == UiJobState::Failed, "late completion cannot overwrite a result");
  check(jobs.begin(UiJob::Evidence, "retry"), "retry can start after failure");
  jobs.finish(UiJob::Evidence, true, "saved");
  check(jobs.get(UiJob::Evidence).state == UiJobState::Succeeded && jobs.activeCount() == 0, "retry completed");
  return failures ? 1 : 0;
}
