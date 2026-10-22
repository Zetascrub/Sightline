#include "nmap_queue.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <cstdio>
namespace reconclave {
void processNmapImport(HostKnowledge& knowledge, const std::string& directory) {
  DIR* dir = opendir(directory.c_str());
  if (!dir) return;
  std::string file;
  while (const auto* entry = readdir(dir)) {
    const std::string name = entry->d_name;
    if (name.rfind("nmap-",0) != 0 || name.size() < 10 || name.substr(name.size()-10) != ".xml.ready") continue;
    const auto path = directory + "/" + name;
    struct stat st{};
    if (lstat(path.c_str(),&st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0022)) continue;
    const auto xml = path.substr(0,path.size()-6);
    if (access((xml+".import.json").c_str(),F_OK) == 0) continue;
    file = xml; break;
  }
  closedir(dir);
  if (file.empty()) return;
  int status = -1;
  std::ifstream marker(file+".ready");
  const bool valid_marker = static_cast<bool>(marker >> status) && status >= 0 && status <= 255;
  NmapResult result; std::string error;
  bool ok = valid_marker;
  if (ok) ok = readNmapFile(file,status,result,error);
  else error = "Invalid import marker";
  unsigned count = 0;
  if (ok) {
    const auto previous = knowledge;
    count = knowledge.observeNmap(result);
    if (!knowledge.save(error)) {
      knowledge = previous;
      return; // Retry a transient storage failure without marking evidence imported.
    }
  }
  auto receipt = json::Value::makeObject();
  receipt.set("schema",json::Value::makeString("sightline-nmap-import/v1"));
  receipt.set("source",json::Value::makeString(file.substr(file.find_last_of('/')+1)));
  receipt.set("status",json::Value::makeString(ok ? "imported" : "rejected"));
  receipt.set("scan_completion",json::Value::makeString(result.completion));
  receipt.set("hosts_imported",json::Value::makeNumber(count));
  receipt.set("error",json::Value::makeString(error));
  const auto dest = file+".import.json";
  std::ofstream output(dest+".tmp"); output << json::stringify(receipt) << '\n'; output.close();
  if (output.good()) std::rename((dest+".tmp").c_str(),dest.c_str());
}
}
