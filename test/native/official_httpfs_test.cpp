#include "duckdb.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckomo_extension.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &db) { db.LoadStaticExtension<DuckomoExtension>(); }
}
namespace {
using namespace duckdb;
void Require(bool yes,const char *message) {
 if(!yes){std::cerr<<"official_httpfs_test: "<<message<<'\n';throw std::runtime_error(message);}
}
std::string Lit(const std::string &s) {std::string r="'";for(char c:s){r+=c;if(c=='\'')r+=c;}return r+"'";}
std::string Metrics(Connection &c) {
 auto result=c.Query("SELECT metrics FROM duckomo_last_scan_metrics()");
 Require(result && !result->HasError() && result->RowCount()==1,"one query metrics record");
 return result->GetValue(0,0).ToString();
}
}
int main() {
 using namespace duckdb;
 const auto *httpfs=std::getenv("DUCKOMO_HTTPFS");const auto *base=std::getenv("DUCKOMO_HTTP_BASE");
 if(!httpfs || !base){std::cout<<"official_httpfs_test: not-run (DUCKOMO_HTTPFS/DUCKOMO_HTTP_BASE required)\n";return 2;}
 // Match the CLI harness: controlled loopback traffic must reach the fixture service directly.
 for(const auto *key:{"HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","http_proxy","https_proxy","all_proxy"})unsetenv(key);
 try {
  DBConfig config;config.SetOptionByName("allow_unsigned_extensions",Value::BOOLEAN(true));
  DuckDB db(nullptr,&config);Connection conn(db),second(db);
  auto load=conn.Query("LOAD "+Lit(httpfs));Require(load && !load->HasError(),"matching official HTTPFS must load");
  Require(!conn.Query("SET http_retries=0")->HasError(),"disable retry for controlled cancel latency");
  for (const auto &fault : {std::pair<const char *,const char *>{"403","access_denied"},
                           std::pair<const char *,const char *>{"403-data","access_denied"},
                           std::pair<const char *,const char *>{"404","object_not_found"}}) {
   Require(!conn.Query("SET VARIABLE diagnostic_uri="+Lit(std::string(base)+"/raw.om?fault="+fault.first+"&token=sensitive-marker"))->HasError(),
           "store test URI outside the erroring SQL text");
   const auto failed=conn.Query("SELECT * FROM read_om(getvariable('diagnostic_uri'))");
   Require(failed && failed->HasError(),"HTTP access/object errors fail");
   const auto message=failed->GetError();
   // Some official HTTPFS builds lose status on HEAD-denied range retries.
   // Preserve an explicit unknown-status diagnostic, never guess from text.
   const bool unknown_head=std::string(fault.first)=="403" && message.find("[transport]")!=std::string::npos &&
                           message.find("no structured HTTP status")!=std::string::npos;
   Require(message.find(std::string("[")+fault.second+"]")!=std::string::npos || unknown_head,
           "structured HTTP diagnostic category or honest unknown-status fallback");
   Require(message.find("sensitive-marker")==std::string::npos,"HTTP diagnostics redact signed parameters");
   Require(Metrics(conn).find("\"status\":\"failure\"")!=std::string::npos,"classified failure publishes QueryEnd");
   auto recovery=conn.Query("SELECT count(*) FROM read_om("+Lit(std::string(base)+"/raw.om")+")");
   Require(recovery && !recovery->HasError() && recovery->GetValue(0,0).GetValue<int64_t>()==6,"same-connection HTTP error recovery");
  }
  Require(!conn.Query("SET http_proxy='http://sensitive-marker:invalid-port/'")->HasError(),"set malformed test proxy");
  Require(!conn.Query("SET VARIABLE diagnostic_uri="+Lit(std::string(base)+"/raw.om?token=sensitive-marker"))->HasError(),"set proxy-test URI");
  auto proxy_error=conn.Query("SELECT * FROM read_om(getvariable('diagnostic_uri'))");
  Require(proxy_error && proxy_error->HasError() && proxy_error->GetError().find("[configuration]")!=std::string::npos,
          "proxy misconfiguration is not a missing-extension error");
  Require(proxy_error->GetError().find("sensitive-marker")==std::string::npos,"proxy credentials are not exposed");
  Require(!conn.Query("SET http_proxy=''")->HasError(),"reset test proxy");
  {
   DuckDB missing_db(nullptr,&config);Connection missing(missing_db);
   Require(!missing.Query("SET autoload_known_extensions=false")->HasError(),"disable extension autoload for missing-extension test");
   Require(!missing.Query("SET VARIABLE diagnostic_uri='https://example.invalid/file.om?token=sensitive-marker'")->HasError(),"set missing-extension URI");
   auto failure=missing.Query("SELECT * FROM read_om(getvariable('diagnostic_uri'))");
   Require(failure && failure->HasError() && failure->GetError().find("[extension_missing]")!=std::string::npos,
           "missing official HTTPFS has its own diagnostic");
   Require(failure->GetError().find("sensitive-marker")==std::string::npos,"missing-extension errors redact parameters");
  }
  auto future=std::async(std::launch::async,[&]{return conn.Query("SELECT * FROM read_om("+Lit(std::string(base)+"/raw.om?fault=timeout&seconds=2")+")");});
  std::this_thread::sleep_for(std::chrono::milliseconds(250));conn.Interrupt();
  auto result=future.get();Require(result && result->HasError(),"cancelled scan must fail");
  auto metrics=Metrics(conn);Require(metrics.find("\"status\":\"cancelled\"")!=std::string::npos,"QueryEnd publishes cancelled status");
  Require(metrics.find("\"response_body_bytes\":null")!=std::string::npos,"cancelled remote transport remains unknown");
  auto recovered=conn.Query("SELECT * FROM read_om("+Lit(std::string(base)+"/raw.om")+")");
  Require(recovered && !recovered->HasError() && recovered->RowCount()==6,"same-connection recovery");
  Require(Metrics(conn).find("\"status\":\"success\"")!=std::string::npos,"recovery terminal status");
  auto second_result=second.Query("SELECT count(*) FROM duckomo_last_scan_metrics()");
  Require(second_result && !second_result->HasError() && second_result->GetValue(0,0).GetValue<int64_t>()==0,"connection metrics isolated");
  // Concurrent independent connections in one process/database.
  auto one=std::async(std::launch::async,[&]{return conn.Query("SELECT * FROM read_om("+Lit(std::string(base)+"/raw.om")+") ORDER BY ALL");});
  auto two=second.Query("SELECT * FROM read_om("+Lit(std::string(base)+"/raw.om")+") ORDER BY ALL");
  auto first=one.get();Require(first && two && !first->HasError() && !two->HasError() && first->ToString()==two->ToString(),"concurrent connection results");
  Require(Metrics(conn)!=Metrics(second),"connection query identities differ");
  std::cout<<"official_httpfs_test: actual HTTPFS diagnostics/redaction, cancellation/recovery and connection isolation passed\n";return 0;
 } catch(const std::exception &){std::cerr<<"official HTTPFS test failed (transport diagnostics withheld)\n";return 1;}
}
