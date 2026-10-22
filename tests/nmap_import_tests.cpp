#include "nmap_import.h"
#include "host_knowledge.h"
#include "nmap_queue.h"
#include "passive_queue.h"
#include "asset_view.h"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace reconclave;
int main() {
  const std::string xml = R"(<?xml version="1.0"?><!DOCTYPE nmaprun>
<?xml-stylesheet href="file:///usr/share/nmap/nmap.xsl" type="text/xsl"?>
<nmaprun start="1700000000"><scaninfo protocol="tcp" numservices="2" services="22,443"/>
<host><status state="up"/><address addrtype="ipv4" addr="192.0.2.10"/>
<hostnames><hostname name="lab&amp;test"/></hostnames><ports>
<port protocol="tcp" portid="22"><state state="open"/><service name="ssh" product="OpenSSH" version="9.0" method="probed"/></port>
<port protocol="tcp" portid="443"><state state="filtered"/></port>
<port protocol="udp" portid="22"><state state="open"/><service name="unknown" method="table"/></port>
<port protocol="udp" portid="161"><state state="open|filtered"/></port>
</ports></host><runstats><finished exit="success" time="1700000010"/></runstats></nmaprun>)";
  NmapResult r; std::string error;
  assert(parseNmapXml(xml,0,r,error)); r.source="nmap-fixture.xml";
  assert(r.completion=="complete" && r.hosts.size()==1);
  r.hosts[0].mac = "AA:BB:CC:DD:EE:FF";
  assert(r.hosts[0].hostname=="lab&test");
  assert(r.coverage.find("22,443")!=std::string::npos);
  const auto dir = std::filesystem::temp_directory_path()/ ("sightline-import-"+std::to_string(getpid()));
  std::filesystem::create_directory(dir);
  HostKnowledge knowledge((dir/"hosts.json").string());
  assert(knowledge.observeNmap(r)==1);
  assert(knowledge.observeNmap(r)==0);
  knowledge.observeArp({{"192.0.2.10", "aa:bb:cc:dd:ee:ff", "eth0", true}}, r.observed_ms);
  assert(knowledge.snapshot()[0].changes == 0);
  auto hosts=knowledge.snapshot();
  assert(hosts[0].services.size()==2 && hosts[0].open_ports==std::vector<std::uint16_t>{22});
  assert(assetPorts(hosts[0])==std::vector<std::uint16_t>{22});
  assert(knowledge.attentionCount()==0);
  auto quick = r; quick.source = "nmap-quick.xml"; quick.observed_ms += 1000;
  quick.hosts[0].ports[0].method = "table"; quick.hosts[0].ports[0].product.clear(); quick.hosts[0].ports[0].version.clear();
  knowledge.observeNmap(quick);
  assert(knowledge.snapshot()[0].services[0].banner == "OpenSSH 9.0");
  auto older = r; older.source = "nmap-old.xml"; older.observed_ms -= 1000;
  older.hosts[0].ports[0].product = "Obsolete";
  knowledge.observeNmap(older);
  assert(knowledge.snapshot()[0].services[0].banner == "OpenSSH 9.0");
  assert(knowledge.save(error));
  HostKnowledge reload((dir/"hosts.json").string()); assert(reload.load(error));
  assert(reload.snapshot()[0].services[1].protocol=="udp");
  assert(reload.snapshot()[0].scan_source==quick.source);
  assert(parseNmapXml(xml,130,r,error) && r.completion=="cancelled");
  assert(parseNmapXml(xml,124,r,error) && r.completion=="timed-out");
  const auto before=r.hosts.size();
  assert(!parseNmapXml(xml.substr(0,xml.size()-12),0,r,error));
  assert(r.hosts.size()==before); // no partial mutation on parser failure
  assert(!parseNmapXml("<nmaprun/><nmaprun/>",0,r,error));
  assert(!parseNmapXml(std::string(8*1024*1024+1,'x'),0,r,error));
  auto invalid=xml; invalid.replace(invalid.find("192.0.2.10"),10,"999.0.2.10");
  assert(!parseNmapXml(invalid,0,r,error));
  invalid=xml; invalid.replace(invalid.find("portid=\"22\""),11,"portid=\"99999\"");
  assert(!parseNmapXml(invalid,0,r,error));
  // External DTDs are not loaded by tinyxml2; no entity expansion/network I/O.
  assert(parseNmapXml("<!DOCTYPE nmaprun SYSTEM 'file:///does-not-exist'><nmaprun start='1'/>",0,r,error));
  const auto path=(dir/"nmap-queued.xml").string();
  {std::ofstream f(path);f<<xml;}
  {std::ofstream f(path+".ready");f<<0;}
  processNmapImport(reload,dir.string());
  assert(std::filesystem::exists(path+".import.json"));
  std::ifstream receipt(path+".import.json");std::string line;std::getline(receipt,line);
  assert(line.find("\"status\":\"imported\"")!=std::string::npos);
  const auto observations=reload.snapshot()[0].observations;
  processNmapImport(reload,dir.string()); assert(reload.snapshot()[0].observations==observations);
  std::filesystem::create_symlink(path,dir/"symlink.xml");
  assert(!readNmapFile((dir/"symlink.xml").string(),0,r,error));
  {std::ofstream f(dir/"nmap-bad.xml");f<<"<nmaprun>";}
  {std::ofstream f(dir/"nmap-bad.xml.ready");f<<130;}
  processNmapImport(reload,dir.string());
  assert(std::filesystem::exists(dir/"nmap-bad.xml.import.json"));
  assert(reload.snapshot()[0].observations==observations);
  const auto passive=(dir/"passive-fixture-good.json").string();
  {std::ofstream f(passive);f<<R"({"schema":"sightline-passive-hosts/v1","scope":"192.0.2.0/24","interface":"eth0","observed_ms":1700000020000,"hosts":[{"address":"192.0.2.11","mac":"02:aa:bb:cc:dd:ee","signals":["arp"],"packets":2}]})";}
  {std::ofstream f(passive+".ready");f<<0;}
  processPassiveImport(reload,dir.string());
  assert(std::filesystem::exists(passive+".import.json"));
  auto passive_hosts=reload.snapshot();
  assert(passive_hosts.size()==2);
  assert(passive_hosts[0].open_ports==std::vector<std::uint16_t>{22});
  assert(passive_hosts[1].address=="192.0.2.11" && passive_hosts[1].open_ports.empty() && passive_hosts[1].services.empty());
  assert(passive_hosts[1].passive_source=="passive-fixture-good.json");
  assert(passive_hosts[1].passive_observed_ms==1700000020000ULL);
  const auto earlier_changes=passive_hosts[0].changes;
  reload.observePassive({{"192.0.2.10","02:00:00:00:00:01","eth0",true}},1700000030000ULL,"passive-conflict.json");
  assert(reload.snapshot()[0].mac=="aa:bb:cc:dd:ee:ff" && reload.snapshot()[0].changes==earlier_changes);
  const auto bad_passive=(dir/"passive-fixture-bad.json").string();
  {std::ofstream f(bad_passive);f<<R"({"schema":"sightline-passive-hosts/v1","scope":"192.0.2.0/24","interface":"eth0","observed_ms":1700000020000,"hosts":[{"address":"198.51.100.1","mac":"02:aa:bb:cc:dd:ee"}]})";}
  {std::ofstream f(bad_passive+".ready");f<<0;}
  processPassiveImport(reload,dir.string());
  assert(std::filesystem::exists(bad_passive+".import.json"));
  assert(reload.snapshot().size()==2);
  std::filesystem::remove_all(dir);
  std::cout<<"Nmap parse, provenance, persistence and queue checks passed\n";
}
