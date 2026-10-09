// Network audit is test-only. The Python harness drives actual official CLI
// and HTTPFS binaries; DuckOMO does not contain a protocol observer.
#include <unistd.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
int main(int argc, char **argv) {
    std::filesystem::path root=std::filesystem::current_path();
    for(int i=1;i+1<argc;++i) if(std::string(argv[i])=="--root") root=argv[i+1];
    auto script=root/"scripts/validate-official-httpfs.py";
    std::vector<std::string> args={"python3",script.string()};
    for(int i=1;i<argc;++i) args.emplace_back(argv[i]);
    std::vector<char *> raw;for(auto &arg:args) raw.push_back(arg.data());raw.push_back(nullptr);
    execvp(raw[0],raw.data());
    std::cerr<<"cannot execute official HTTPFS validation harness\n";return 127;
}
