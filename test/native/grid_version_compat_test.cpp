#include "duckomo/build_identity.hpp"
#include "duckomo/compat/duckdb_api.hpp"
#include <iostream>
#include <string>
int main() {
    const auto &identity=duckdb::duckomo::BUILD_IDENTITY;
    if (std::string(identity.duckdb_commit).size()!=40 || std::string(identity.build_id).size()!=64) {
        // Ordinary make builds do not supply matrix identities.
        if (identity.pair_id[0]!='\0') return 1;
    }
    std::cout<<"grid_version_compat_test: engine build identity; official dynamic loading is verified by the runtime harness\n";
    return 0;
}
