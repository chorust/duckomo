#include "duckdb.hpp"
#include "duckdb_test_compat.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/extension_helper.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/common/file_opener.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/common/exception/http_exception.hpp"
#include "duckomo/remote_file.hpp"
#include "duckomo/om_reader.hpp"
#include "duckomo/reader.hpp"
#include "duckomo_extension.hpp"
extern "C" {
#include "om_variable.h"
#include "om_encoder.h"
#include "om_file.h"
}
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
DUCKOMO_TEST_LOAD_ALL_EXTENSIONS(db) { db.LoadStaticExtension<DuckomoExtension>(); }
}
namespace {
using namespace duckdb;
using namespace duckdb::duckomo;
void Require(bool value, const char *why) { if (!value) throw std::runtime_error(why); }
void Require(bool value, const std::string &why) { if (!value) throw std::runtime_error(why); }
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
    DuckDB db(nullptr);
	DUCKOMO_TEST_LOAD_EXTENSIONS(db); Connection conn(db);
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
        auto result=duckdb::duckomo_test::Rows(conn.Query(sql));
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

class MergeTestFileSystem final : public FileSystem {
public:
    explicit MergeTestFileSystem(const std::string &fixture_path, State &state) : state_(state) {
        std::ifstream input(fixture_path, std::ios::binary);
        Require(input.good(), "merge fixture available: " + fixture_path);
        bytes_.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    bool CanHandleFile(const string &path) override { return path.find("https://duckomo-merge/") == 0; }
    string GetName() const override { return "duckomo-range-merge-test"; }
    unique_ptr<FileHandle> OpenFile(const string &path, FileOpenFlags flags, optional_ptr<FileOpener>) override {
        ++state_.opens;
        return make_uniq<Handle>(*this, path, flags, state_);
    }
    int64_t GetFileSize(FileHandle &) override { return static_cast<int64_t>(bytes_.size()); }
    string GetVersionTag(FileHandle &) override { return "v1"; }
    void Read(FileHandle &, void *destination, int64_t count, idx_t offset) override {
        Require(offset <= bytes_.size() && count >= 0 && static_cast<uint64_t>(count) <= bytes_.size() - offset,
                "merge fixture read is bounded");
        if (state_.read_error) state_.read_error();
        ++reads_;
        bytes_read_ += static_cast<std::uint64_t>(count);
        std::memcpy(destination, bytes_.data() + offset, static_cast<size_t>(count));
    }
    void Reset() {
        reads_ = 0;
        bytes_read_ = 0;
    }
    std::uint64_t Reads() const { return reads_; }
    std::uint64_t BytesRead() const { return bytes_read_; }

private:
    State &state_;
    std::vector<char> bytes_;
    std::uint64_t reads_ = 0;
    std::uint64_t bytes_read_ = 0;
};

class SimpleMemoryFile final : public ReadAtFile {
public:
    explicit SimpleMemoryFile(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}
    std::uint64_t Size() const noexcept override { return bytes_.size(); }
    const std::string &Path() const noexcept override { static const std::string p = "https://duckomo-test/memory.om"; return p; }
    const std::shared_ptr<ScanMetrics> &Metrics() const noexcept override { static const std::shared_ptr<ScanMetrics> m; return m; }
    void ReadRange(std::uint64_t offset, std::uint64_t size, void *destination, ScanReadPhase,
                   const std::string &) const override {
        Require(offset <= bytes_.size() && size <= bytes_.size() - offset, "bounded read in SimpleMemoryFile");
        if (destination && size > 0) {
            std::memcpy(destination, bytes_.data() + offset, static_cast<size_t>(size));
        }
    }
    std::vector<std::uint8_t> ReadRange(std::uint64_t offset, std::uint64_t size, ScanReadPhase phase,
                                       const std::string &var) const override {
        std::vector<std::uint8_t> res(static_cast<size_t>(size));
        ReadRange(offset, size, res.data(), phase, var);
        return res;
    }
private:
    std::vector<std::uint8_t> bytes_;
};

std::uint64_t ExtractRemoteIoUint(const std::string &json, const std::string &key) {
    auto remote_pos = json.find("\"remote_io\":");
    if (remote_pos == std::string::npos) throw std::runtime_error("missing remote_io in metrics JSON");
    const std::string target = "\"" + key + "\":";
    auto pos = json.find(target, remote_pos);
    if (pos == std::string::npos) throw std::runtime_error("missing key in remote_io: " + key);
    pos += target.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (pos >= json.size() || (json[pos] < '0' || json[pos] > '9')) {
        throw std::runtime_error("non-numeric value in remote_io for: " + key);
    }
    char *end = nullptr;
    auto val = std::strtoull(json.c_str() + pos, &end, 10);
    return val;
}

std::vector<std::uint8_t> CreateOmWithLargeLutChunk(uint64_t target_lut_bytes) {
    const uint64_t dimensions[] = {2, 2};
    std::vector<float> values = {1.0f, 2.0f, 3.0f, 4.0f};
    OmEncoder_t encoder{};
    Require(om_encoder_init(&encoder, 1, 0, COMPRESSION_FPX_XOR2D, DATA_TYPE_FLOAT_ARRAY,
                            dimensions, dimensions, 2) == ERROR_OK, "encoder init");
    std::vector<std::uint8_t> compressed(65536);
    std::vector<std::uint8_t> scratch(om_encoder_chunk_buffer_size(&encoder));
    const uint64_t offset[] = {0, 0};
    const auto bytes = om_encoder_compress_chunk(&encoder, values.data(), dimensions, offset, dimensions,
                                                 0, 0, compressed.data(), scratch.data());
    std::vector<std::uint8_t> object(om_header_write_size());
    om_header_write(object.data());
    const uint64_t lut[] = {object.size(), object.size() + bytes};
    object.insert(object.end(), compressed.begin(), compressed.begin() + bytes);
    const auto lut_offset = object.size();
    std::vector<std::uint8_t> lut_data(target_lut_bytes, 0);
    om_encoder_compress_lut(lut, 2, lut_data.data(), lut_data.size());
    object.insert(object.end(), lut_data.begin(), lut_data.end());
    while (object.size() % 8) object.push_back(0);
    const auto root_offset = object.size();
    std::vector<std::uint8_t> root(om_variable_write_numeric_array_size(0, 0, 2));
    om_variable_write_numeric_array(root.data(), 0, 0, nullptr, nullptr, "", DATA_TYPE_FLOAT_ARRAY,
                                    COMPRESSION_FPX_XOR2D, 1, 0, 2, dimensions, dimensions, target_lut_bytes, lut_offset);
    object.insert(object.end(), root.begin(), root.end());
    while (object.size() % 8) object.push_back(0);
    const auto trailer_offset = object.size();
    object.resize(trailer_offset + om_trailer_size());
    om_trailer_write(object.data() + trailer_offset, root_offset, root.size());
    return object;
}

void TestRemoteRangeMerge(Connection &conn, State &merge_state) {
    auto fs_instance = make_uniq<MergeTestFileSystem>("test/data/projection.om", merge_state);
    auto *merge_fs = fs_instance.get();
    FileSystem::GetFileSystem(*conn.context).RegisterSubSystem(std::move(fs_instance));

    const auto scalar = [&](const std::string &sql) {
        auto result = duckdb::duckomo_test::Rows(conn.Query(sql));
        Require(result && !result->HasError() && result->RowCount() == 1, "scalar query succeeds: " + sql);
        return result->GetValue(0, 0).ToString();
    };

    const std::string uri = "'https://duckomo-merge/projection.om'";
    const std::string query =
        "SELECT count(*), avg(humidity), avg(pressure), avg(temperature) FROM read_om(" + uri +
        ", dimensions := map(['humidity','pressure','temperature'], "
        "[['row','column'],['row','column'],['row','column']]))";

    // Enabling async is accepted on 1.5.x but must keep the sync path.
#if DUCKOMO_DUCKDB_API_GENERATION < 2
    Require(!conn.Query("SET duckomo_async_io = true")->HasError(), "1.5 accepts async option");
#endif
    // 1. Run with duckomo_remote_merge = false
    Require(!conn.Query("SET duckomo_remote_merge = false")->HasError(), "disable merge succeeds");
    merge_fs->Reset();
    auto res_off = duckdb::duckomo_test::Rows(conn.Query(query));
    Require(res_off && !res_off->HasError() && res_off->RowCount() == 1, "query succeeds with merge off");
    const auto off_reads = merge_fs->Reads();
    const auto off_bytes = merge_fs->BytesRead();
    const auto off_result = res_off->GetValue(0, 0).ToString() + "," +
                            res_off->GetValue(1, 0).ToString() + "," +
                            res_off->GetValue(2, 0).ToString() + "," +
                            res_off->GetValue(3, 0).ToString();
    const auto metrics_off = scalar("SELECT metrics FROM duckomo_last_scan_metrics()");
#if DUCKOMO_DUCKDB_API_GENERATION < 2
    Require(metrics_off.find("\"async_applied\":false") != std::string::npos,
            "1.5 ignores async=true for remote scans");
#endif
    const auto off_useful = ExtractRemoteIoUint(metrics_off, "useful_bytes");
    const auto off_requests = ExtractRemoteIoUint(metrics_off, "requests");

    // 2. Run with duckomo_remote_merge = true (defaults: gap=4096, max=65536)
    Require(!conn.Query("SET duckomo_remote_merge = true")->HasError(), "enable merge succeeds");
    Require(!conn.Query("SET duckomo_remote_merge_gap = 4096")->HasError(), "set gap succeeds");
    Require(!conn.Query("SET duckomo_remote_max_request = 65536")->HasError(), "set max request succeeds");
    merge_fs->Reset();
    auto res_on = duckdb::duckomo_test::Rows(conn.Query(query));
    Require(res_on && !res_on->HasError() && res_on->RowCount() == 1, "query succeeds with merge on");
    const auto on_reads = merge_fs->Reads();
    const auto on_bytes = merge_fs->BytesRead();
    const auto on_result = res_on->GetValue(0, 0).ToString() + "," +
                           res_on->GetValue(1, 0).ToString() + "," +
                           res_on->GetValue(2, 0).ToString() + "," +
                           res_on->GetValue(3, 0).ToString();
    Require(off_result == on_result, "merge on produces identical results to merge off");

    const auto metrics_on = scalar("SELECT metrics FROM duckomo_last_scan_metrics()");
    const auto on_useful = ExtractRemoteIoUint(metrics_on, "useful_bytes");
    const auto on_gap = ExtractRemoteIoUint(metrics_on, "gap_bytes");
    const auto on_physical = ExtractRemoteIoUint(metrics_on, "physical_bytes");
    const auto on_requests = ExtractRemoteIoUint(metrics_on, "requests");

    Require(on_reads < off_reads, "filesystem reads decreased with remote merge");
    Require(on_requests < off_requests, "metrics remote_io.requests decreased with remote merge");
    Require(on_useful > 0 && off_useful > 0, "useful bytes must be non-zero");
    // Useful bytes count bytes consumed from completed physical reads; cache
    // reuse avoids counting them twice, so the two modes can differ.
    Require(on_useful <= off_useful, "merge reuse avoids counting cached bytes twice");
    Require(on_physical >= on_useful, "physical bytes >= useful bytes");
    Require(on_gap == on_physical - on_useful, "gap bytes equals physical minus useful bytes");
    Require(on_gap > 0, "read-ahead and unselected merged chunks are reported as gap bytes");

    // 3. Test LIMIT 1 reads are bounded (do not scan full file, extra bytes <= 1 max_request)
    merge_fs->Reset();
    auto res_limit = duckdb::duckomo_test::Rows(conn.Query(
        "SELECT humidity, pressure, temperature FROM read_om(" + uri +
        ", dimensions := map(['humidity','pressure','temperature'], "
        "[['row','column'],['row','column'],['row','column']])) LIMIT 1"));
    Require(res_limit && !res_limit->HasError() && res_limit->RowCount() == 1, "LIMIT 1 query succeeds");
    const auto limit_bytes = merge_fs->BytesRead();
    Require(limit_bytes < off_bytes, "LIMIT 1 reads fewer bytes than full scan");
    Require(limit_bytes <= 65536 + 16384, "LIMIT 1 extra read does not exceed one max_request budget plus metadata");

    // 4. Test configuration error when duckomo_remote_max_request < lut_chunk_length
    {
        auto large_lut_bytes = CreateOmWithLargeLutChunk(1024);
        OmV3Reader om_reader(make_uniq<SimpleMemoryFile>(std::move(large_lut_bytes)));
        OmDecoderState decoder(om_reader.ReadRootVariable());
        RemoteReadBudget bad_budget;
        bad_budget.enabled = true;
        bad_budget.max_request = 512;
        bool caught = false;
        try {
            float dummy = 0.0f;
            om_reader.DecodeSelection(decoder, "value", {0, 0}, {1, 1}, {0, 0}, {1, 1}, &dummy, sizeof(dummy), bad_budget);
        } catch (const ReaderError &error) {
            caught = true;
            Require(std::string(error.what()).find("[configuration] duckomo_remote_max_request is smaller than an official LUT block") != std::string::npos,
                    "max_request below LUT chunk length raises [configuration] error");
        }
        Require(caught, "max_request below LUT chunk length must throw [configuration]");
    }

    // 5. Test cancellation path during range merge
    {
        merge_state.read_error = [&] {
            if (merge_fs->Reads() >= 2) throw Exception(ExceptionType::INTERRUPT, "range merge cancellation");
        };
        auto res = conn.Query(query);
        Require(res && res->HasError(), "query fails on interruption during range merge");
        Require(res->GetError().find("cancellation") != std::string::npos ||
                res->GetError().find("interrupt") != std::string::npos ||
                res->GetError().find("INTERRUPT") != std::string::npos,
                "error preserves interrupt classification");
        const auto cancel_bytes = merge_fs->BytesRead();
        Require(cancel_bytes < off_bytes, "interrupted query does not scan full file");
        Require(cancel_bytes <= 65536 + 16384, "interrupted query reads at most one merge request beyond metadata");
        merge_state.read_error = {};
        Require(!conn.Query("SELECT 1")->HasError(), "next query resets cancellation state after range merge interrupt");
    }
}

}
int main() {
    try {
        State state;
        DBConfig query_config;query_config.SetOptionByName("allow_unsigned_extensions",Value::BOOLEAN(true));
        DuckDB db(nullptr,&query_config);
	DUCKOMO_TEST_LOAD_EXTENSIONS(db); Connection conn(db);
        auto &fs=FileSystem::GetFileSystem(*conn.context);
        fs.RegisterSubSystem(make_uniq<FakeFileSystem>(state));
        auto metrics=std::make_shared<ScanMetrics>();
        auto session=RemoteReadSession::Create(*conn.context,"https://duckomo-test/object.om?token=secret",metrics);
        Require(session->Path()=="https://duckomo-test/object.om?<redacted>","URI must be redacted");
        auto first=session->Open(*conn.context,ScanMetadataStage::Bind);
        auto second=session->Open(*conn.context,ScanMetadataStage::Scan);
        Require(state.opens==2,"each worker owns an independent handle");
        metrics->SetRemoteIo(false,512,64*1024,false);
        {
            auto bytes=first->ReadRange(1,3,ScanReadPhase::Data,"value");
            Require(std::string(bytes.begin(),bytes.end())=="bcd","exact positional read");
            const auto snapshot=metrics->Snapshot();
            Require(snapshot.remote_requests && *snapshot.remote_requests>=1,"remote request counted");
            Require(snapshot.remote_useful_bytes && snapshot.remote_gap_bytes &&
                    *snapshot.remote_physical_bytes>=*snapshot.remote_useful_bytes,"gap is non-negative");
            Require(snapshot.remote_inflight_peak==1 && !snapshot.remote_inflight_budget_bytes,"sync inflight is one");
            Require(metrics->ToMetricsV4Json().find("remote_io")!=std::string::npos,"remote_io object present");
        }
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
        DuckDB restricted(nullptr,&config);
	DUCKOMO_TEST_LOAD_EXTENSIONS(restricted);Connection denied(restricted);
        Fails([&]{ RemoteReadSession::Create(*denied.context,"https://duckomo-test/object.om?token=secret",metrics); },"external access restriction");
        Require(!IsSupportedRemoteOmUri("https://host/*.om"),"reject globs");
        Require(!IsSupportedRemoteOmUri("https://host/file.om#frag"),"reject fragments");
        TestDiagnosticFailures();
        TestQueryObjectIdentity(conn);
        State merge_state;
        TestRemoteRangeMerge(conn, merge_state);
        std::cout<<"remote_session_test: standard interface, handles, errors, cancellation, permissions, range merge passed\n";
        return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
