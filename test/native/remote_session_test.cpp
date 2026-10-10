#include "duckdb.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/common/file_opener.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/exception/http_exception.hpp"
#include "duckomo/remote_file.hpp"
#include "duckomo/om_reader.hpp"
#include "duckomo_extension.hpp"
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace duckdb {
void ExtensionHelper::LoadAllExtensions(DuckDB &db) { db.LoadStaticExtension<DuckomoExtension>(); }
}
namespace {
using namespace duckdb;
using namespace duckdb::duckomo;
void Require(bool value, const char *why) { if (!value) throw std::runtime_error(why); }
struct State {
    int opens=0; int closed=0; bool fail=false; bool changed=false; bool no_tag=false;
    std::function<void()> open_error;
    std::function<void()> read_error;
};
struct Handle final : FileHandle {
    Handle(FileSystem &fs, const std::string &path, FileOpenFlags flags, State &state)
        : FileHandle(fs,path,flags),state(state) {}
    ~Handle() override { Close(); }
    void Close() override { if (!closed) { ++state.closed;closed=true; } }
    State &state; bool closed=false;
};
class FakeFileSystem final : public FileSystem {
public:
    explicit FakeFileSystem(State &state) : state(state) {}
    bool CanHandleFile(const string &path) override { return path.find("https://duckomo-test/")==0; }
    string GetName() const override { return "duckomo-standard-file-test"; }
    unique_ptr<FileHandle> OpenFile(const string &path, FileOpenFlags flags, optional_ptr<FileOpener> opener) override {
        Require(bool(opener),"standard opener required");
        Require(flags.DirectIO(),"standard direct I/O prevents read-ahead");
        Require(bool(opener->TryGetClientContext()),"current client context must reach filesystem");
        Value value;
        Require(bool(opener->TryGetCurrentSetting("force_download",value)) && !value.GetValue<bool>(),"local forced-download override");
        Require(bool(opener->TryGetCurrentSetting("force_download_threshold",value)) && value.GetValue<uint64_t>()==0,"local threshold override");
        Require(bool(opener->TryGetCurrentSetting("auto_fallback_to_full_download",value)) && !value.GetValue<bool>(),"no whole-file fallback");
        Require(bool(opener->TryGetCurrentSetting("unsafe_disable_etag_checks",value)) && !value.GetValue<bool>(),"ETag check required");
        Require(bool(opener->TryGetCurrentSetting("s3_version_id_pinning",value)) && value.GetValue<bool>(),"S3 pinning required");
        ++state.opens;
        if (state.open_error) state.open_error();
        if (state.fail) throw IOException("private-token=secret");
        return make_uniq<Handle>(*this,path,flags,state);
    }
    int64_t GetFileSize(FileHandle &) override { return state.changed?9:8; }
    string GetVersionTag(FileHandle &) override { return state.no_tag ? "" : state.changed?"v2":"v1"; }
    void Read(FileHandle &,void *dest,int64_t size,idx_t offset) override {
        if (state.read_error) state.read_error();
        if (state.fail) throw IOException("short read, private-token=secret");
        if (offset+size>8) throw IOException("short read");
        std::memcpy(dest,"abcdefgh"+offset,static_cast<size_t>(size));
    }
    State &state;
};
template<class F> void Fails(F action,const char *why) {
    bool failed=false;
    try { action(); } catch(const std::exception &e) { failed=true;Require(std::string(e.what()).find("secret")==std::string::npos,"error must redact transport secrets"); }
    Require(failed,why);
}
void TestDiagnosticFailures() {
    State state;
    DuckDB db(nullptr); Connection conn(db);
    FileSystem::GetFileSystem(*conn.context).RegisterSubSystem(make_uniq<FakeFileSystem>(state));
    auto metrics=std::make_shared<ScanMetrics>();
    auto session=RemoteReadSession::Create(*conn.context,"https://duckomo-test/object.om?token=sensitive-marker",metrics);
    struct Case { std::function<void()> fail; const char *category; };
    std::vector<Case> cases={
        {[]{throw MissingExtensionException("sensitive-marker");},"extension_missing"},
        {[]{throw Exception(ExceptionType::AUTOLOAD,"sensitive-marker");},"extension_load"},
        {[]{throw PermissionException("sensitive-marker");},"access_denied"},
        {[]{throw InvalidInputException("proxy credential=sensitive-marker");},"configuration"},
        {[]{throw Exception(ExceptionType::INVALID_CONFIGURATION,"sensitive-marker");},"configuration"},
        {[]{throw Exception(ExceptionType::SETTINGS,"sensitive-marker");},"configuration"},
        {[]{throw IOException("HTTP 404 sensitive-marker");},"transport"},
        {[]{throw std::runtime_error("HTTP 403 sensitive-marker");},"transport"},
        {[]{throw std::runtime_error("{token=sensitive-marker");},"transport"},
        {[]{throw HTTPException("HTTP 404 sensitive-marker");},"transport"},
        {[]{throw Exception(unordered_map<string,string>{{"status_code","404 sensitive-marker"}},
                           ExceptionType::HTTP,"sensitive-marker");},"transport"}
    };
    for (const auto &status : std::vector<std::pair<int,const char *>>{
             {401,"access_denied"},{403,"access_denied"},{404,"object_not_found"},
             {408,"timeout"},{504,"timeout"},{412,"object_changed"},{416,"range_request"},
             {429,"rate_limited"},{500,"http"}}) {
        cases.push_back({[code=status.first]{
            const std::unordered_map<std::string,std::string> headers={{"Authorization","sensitive-marker"}};
            throw HTTPException(code,"sensitive-marker body",headers,"sensitive-marker reason",
                                "sensitive-marker signed URL");
        },status.second});
    }
    const auto expect=[&](const std::function<void()> &action, const char *category, ReaderErrorCode code) {
        try { action(); }
        catch (const ReaderError &error) {
            const std::string message=error.what();
            Require(error.Code()==code,"diagnostics preserve reader error phase");
            Require(message.find(std::string("[")+category+"]")!=std::string::npos,"actionable error category");
            Require(message.find("?<redacted>")!=std::string::npos,"object query parameters are redacted");
            Require(message.find("sensitive-marker")==std::string::npos,"raw message/body/header/reason must never escape");
            Require(std::count(message.begin(),message.end(),'\n')==0,"diagnostics contain no injected lines");
            return;
        }
        throw std::runtime_error("remote diagnostic did not fail");
    };
    for (const auto &test : cases) {
        state.open_error=test.fail;
        expect([&]{session->Open(*conn.context,ScanMetadataStage::Bind);},test.category,ReaderErrorCode::FileIo);
        state.open_error={};
        auto file=session->Open(*conn.context,ScanMetadataStage::Scan);
        state.read_error=test.fail;
        const auto before=metrics->Snapshot().physical_read_bytes;
        expect([&]{file->ReadRange(0,1,ScanReadPhase::Index,"value");},test.category,ReaderErrorCode::IndexRead);
        expect([&]{file->ReadRange(0,1,ScanReadPhase::Data,"value");},test.category,ReaderErrorCode::DataRead);
        Require(metrics->Snapshot().physical_read_bytes==before,"failed reads are not accounted as success");
        state.read_error={};
        Require(file->ReadRange(0,1,ScanReadPhase::Data,"value")[0]=='a',"same-session recovery after classified failure");
    }
    Require(state.closed==static_cast<int>(cases.size()),"all successful diagnostic handles close");
    state.open_error=[]{throw Exception(ExceptionType::INTERRUPT,"sensitive-marker");};
    bool cancelled=false;
    try { session->Open(*conn.context,ScanMetadataStage::Bind); } catch(const InterruptException &) {cancelled=true;}
    Require(cancelled,"serialized interruption preserves cancellation rather than transport failure");
    state.open_error={};
    auto file=session->Open(*conn.context,ScanMetadataStage::Scan);
    state.read_error=[]{throw Exception(ExceptionType::INTERRUPT,"sensitive-marker");};
    for (const auto phase : {ScanReadPhase::Index, ScanReadPhase::Data}) {
        cancelled=false;
        try { file->ReadRange(0,1,phase,"value"); } catch(const InterruptException &) {cancelled=true;}
        Require(cancelled,"serialized read interruption preserves cancellation rather than transport failure");
    }
    state.read_error={};
    Require(file->ReadRange(0,1,ScanReadPhase::Data,"value")[0]=='a',"read recovery after serialized interruption");
    file.reset();
    Require(state.closed==static_cast<int>(cases.size())+1,"cancellation diagnostic handle closes");
    Require(RedactRemoteUri("https://user-sensitive-marker:pass-sensitive-marker@host/file.om?token=sensitive-marker") ==
            "https://host/file.om?<redacted>","userinfo and signed parameters are both redacted");
}

class IdentityFileSystem final : public FileSystem {
public:
    IdentityFileSystem() {
        std::ifstream input("test/data/raw.om",std::ios::binary);
        Require(input.good(),"identity fixture available");
        bytes.assign(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
    }
    bool CanHandleFile(const string &path) override { return path.find("https://duckomo-identity/")==0; }
    string GetName() const override { return "duckomo-query-object-identity-test"; }
    unique_ptr<FileHandle> OpenFile(const string &path,FileOpenFlags flags,optional_ptr<FileOpener>) override {
        return make_uniq<Handle>(*this,path,flags,state);
    }
    int64_t GetFileSize(FileHandle &) override { return bytes.size(); }
    string GetVersionTag(FileHandle &) override { return ""; }
    void Read(FileHandle &,void *destination,int64_t count,idx_t offset) override {
        Require(offset<=bytes.size() && count>=0 && static_cast<uint64_t>(count)<=bytes.size()-offset,
                "identity fixture read is bounded");
        std::memcpy(destination,bytes.data()+offset,static_cast<size_t>(count));
    }
private:
    State state;
    std::vector<char> bytes;
};
void TestQueryObjectIdentity(Connection &conn) {
    const auto *core_override=std::getenv("DUCKOMO_CORE_FUNCTIONS_EXTENSION");
    const std::string core_path=core_override ? core_override : "build/release/extension/core_functions/core_functions.duckdb_extension";
    auto core=conn.Query("LOAD '"+core_path+"'");
    Require(core && !core->HasError(),"core functions available for identity queries");
    FileSystem::GetFileSystem(*conn.context).RegisterSubSystem(make_uniq<IdentityFileSystem>());
    const auto scalar=[&](const std::string &sql) {
        auto result=conn.Query(sql);
        Require(result && !result->HasError() && result->RowCount()==1,"identity query succeeds");
        return result->GetValue(0,0).ToString();
    };
    const std::string arguments=
        ", dimensions := map(['value'], [['row','column']]), "
        "grid := {'nx':3,'ny':2,'lat0':0.0,'lon0':0.0,'dlat':1.0,'dlon':1.0,'order':'separate'}, "
        "spatial_axes := ['row','column']";
    std::string previous;
    for (const auto *object : {"one","two"}) {
        const auto uri=std::string("'https://duckomo-identity/download.om?id=")+object+"&token=secret'";
        const auto scan="SELECT om_source.object_id FROM read_om("+uri+arguments+", include_source := true) LIMIT 1";
        const auto id=scalar(scan);
        Require(id==scalar(scan),"one remote URI has stable object identity");
        Require(id==scalar("SELECT object_id FROM om_grid_info("+uri+arguments+")"),
                "grid descriptor and source rows agree on remote object identity");
        Require(id.rfind("sha256:",0)==0 && id!=previous,"query-selected objects have distinct opaque identities");
        previous=id;
        const auto metrics=scalar("SELECT metrics FROM duckomo_last_scan_metrics()");
        Require(metrics.find("secret")==std::string::npos && metrics.find("id="+std::string(object))==std::string::npos,
                "object identity preserves display-path redaction");
    }
}
}
int main() {
    try {
        State state;
        DBConfig query_config;query_config.SetOptionByName("allow_unsigned_extensions",Value::BOOLEAN(true));
        DuckDB db(nullptr,&query_config); Connection conn(db);
        auto &fs=FileSystem::GetFileSystem(*conn.context);
        fs.RegisterSubSystem(make_uniq<FakeFileSystem>(state));
        auto metrics=std::make_shared<ScanMetrics>();
        auto session=RemoteReadSession::Create(*conn.context,"https://duckomo-test/object.om?token=secret",metrics);
        Require(session->Path()=="https://duckomo-test/object.om?<redacted>","URI must be redacted");
        auto first=session->Open(*conn.context,ScanMetadataStage::Bind);
        auto second=session->Open(*conn.context,ScanMetadataStage::Scan);
        Require(state.opens==2,"each worker owns an independent handle");
        auto bytes=first->ReadRange(1,3,ScanReadPhase::Data,"value");
        Require(std::string(bytes.begin(),bytes.end())=="bcd","exact positional read");
        Require(metrics->Snapshot().physical_read_bytes==3,"successful application read accounting");
        Require(!metrics->Snapshot().response_body_bytes && metrics->Snapshot().transport_count_complete == false,"remote transport unknown");
        Require(metrics->ToMetricsV3Json().find("\"response_body_bytes\":null")!=std::string::npos,"legacy remote NULL");
        Fails([&]{ first->ReadRange(7,2,ScanReadPhase::Data,"value"); },"bounds must fail");
        Fails([&]{ first->ReadRange(0,1,nullptr,ScanReadPhase::Data,"value"); },"null destination must fail");
        state.fail=true;
        Fails([&]{ first->ReadRange(0,4,ScanReadPhase::Data,"value"); },"failed/short reads must fail");
        Require(metrics->Snapshot().physical_read_bytes==3,"failed read cannot count as success");
        state.fail=false;
        Require(second->ReadRange(0,1,ScanReadPhase::Data,"value")[0]=='a',"recovery after read failure");
        state.changed=true;
        Fails([&]{session->Open(*conn.context,ScanMetadataStage::Scan);},"observable size/version conflict must fail");
        state.changed=false;
        conn.Interrupt();
        bool interrupted=false;
        try { first->ReadRange(0,1,ScanReadPhase::Data,"value"); } catch(const InterruptException &) { interrupted=true; }
        Require(interrupted,"cancellation keeps InterruptException");
        Require(!conn.Query("SELECT 1")->HasError(),"next query resets cancellation");
        Require(first->ReadRange(0,1,ScanReadPhase::Data,"value")[0]=='a',"read recovery after cancellation");
        first.reset();second.reset();Require(state.closed==state.opens,"all handles closed after conflict/recovery");
        state.no_tag=true;
        auto weak=RemoteReadSession::Create(*conn.context,"https://duckomo-test/weak.om",metrics);
        weak->Open(*conn.context,ScanMetadataStage::Bind);
        weak->Open(*conn.context,ScanMetadataStage::Scan); // Empty tag is allowed, never content_verified.
        DBConfig config; config.SetOptionByName("enable_external_access",Value::BOOLEAN(false));
        DuckDB restricted(nullptr,&config);Connection denied(restricted);
        Fails([&]{ RemoteReadSession::Create(*denied.context,"https://duckomo-test/object.om?token=secret",metrics); },"external access restriction");
        Require(!IsSupportedRemoteOmUri("https://host/*.om"),"reject globs");
        Require(!IsSupportedRemoteOmUri("https://host/file.om#frag"),"reject fragments");
        TestDiagnosticFailures();
        TestQueryObjectIdentity(conn);
        std::cout<<"remote_session_test: standard interface, handles, errors, cancellation, permissions passed\n";
        return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
