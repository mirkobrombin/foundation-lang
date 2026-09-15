#include "foundation/package.hpp"
#include "foundation/sha256.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures{};

void expect(bool condition, const std::string &message) {
    if (condition) {
        return;
    }
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

std::string errorCodes(const std::vector<foundation::PackageError> &errors) {
    std::string result;
    for (const auto &error : errors) {
        result += ' ' + error.code + ' ' + error.message;
    }
    return result;
}

bool hasCode(const std::vector<foundation::PackageError> &errors, std::string_view code) {
    return std::any_of(errors.begin(), errors.end(), [&](const auto &error) {
        return error.code == code;
    });
}

constexpr std::string_view qualifiedManifest = R"(format foundation.package/v1
name sample.firmware
version 1.0.0
language 1
source src
native_library c
native_name sample_firmware
native_source native/board.c target freestanding
native_source native/host.c target hosted
native_source native/shared.c
native_link m target hosted
native_link rt target linux
native_link soft target freestanding
foreign c firmware 1.0.0 path native abi c/v1
dependency sample.board 1.0.0 registry default target freestanding
dependency sample.host 1.0.0 registry default target hosted
dependency sample.shared 1.0.0 registry default
)";

constexpr std::string_view dependencyManifest = R"(format foundation.package/v1
name sample.firmware
version 1.0.0
language 1
source src
dependency sample.board 1.0.0 registry default target freestanding
dependency sample.host 1.0.0 registry default target hosted
dependency sample.shared 1.0.0 registry default
)";

foundation::PackageManifest manifest(std::string name) {
    foundation::PackageManifest result;
    result.name = std::move(name);
    result.version = *foundation::parsePackageVersion("1.0.0");
    result.sdk = *foundation::parsePackageRequirement("^0.1.0");
    result.source = "src";
    return result;
}

foundation::PackageCandidate candidate(std::string name) {
    return {manifest(name), "sha256:" + foundation::sha256Hex(name),
            foundation::PackageLocationKind::Registry, "default", {}};
}

bool resolvesPackage(const foundation::PackageLock &lock, std::string_view name) {
    return std::any_of(lock.packages.begin(), lock.packages.end(), [&](const auto &package) {
        return package.name == name;
    });
}

void selectorsParseAndRenderCanonically() {
    const auto parsed = foundation::parsePackageManifest("foundation.package", qualifiedManifest);
    expect(parsed.value.has_value() && parsed.errors.empty(),
           "freestanding and hosted manifest qualifiers parse");
    if (!parsed.value.has_value()) {
        return;
    }
    expect(foundation::renderPackageManifest(*parsed.value) == qualifiedManifest,
           "manifest qualifiers render canonically");
    expect(parsed.value->nativeSources.front().target == foundation::TargetSelector::Freestanding &&
               parsed.value->dependencies[1].target == foundation::TargetSelector::Hosted,
           "manifest qualifiers keep their selectors");
}

void overlappingSelectorsAreDuplicates() {
    constexpr std::string_view overlapping = R"(format foundation.package/v1
name sample.overlap
version 1.0.0
language 1
source src
native_library c
native_name sample_overlap
native_link m target hosted
native_link m target linux
)";
    const auto overlappingResult =
        foundation::parsePackageManifest("foundation.package", overlapping);
    expect(std::any_of(overlappingResult.errors.begin(), overlappingResult.errors.end(),
                       [](const auto &error) {
                           return error.code == "FDN4015" &&
                                  error.message.find("unique library") != std::string::npos;
                       }),
           "a hosted link overlaps the same link for one hosted platform:" +
               errorCodes(overlappingResult.errors));
    constexpr std::string_view disjoint = R"(format foundation.package/v1
name sample.disjoint
version 1.0.0
language 1
source src
native_library c
native_name sample_disjoint
native_link m target freestanding
native_link m target hosted
)";
    const auto disjointResult = foundation::parsePackageManifest("foundation.package", disjoint);
    expect(disjointResult.errors.empty(),
           "freestanding and hosted links do not overlap:" + errorCodes(disjointResult.errors));
}

void selectorsActivateMatchingEntries() {
    const auto parsed = foundation::parsePackageManifest("foundation.package", qualifiedManifest);
    if (!parsed.value.has_value()) {
        expect(false, "selection fixture parses");
        return;
    }
    const auto dependencies =
        foundation::parsePackageManifest("foundation.package", dependencyManifest);
    if (!dependencies.value.has_value()) {
        expect(false, "dependency fixture parses:" + errorCodes(dependencies.errors));
        return;
    }
    const auto sdk = *foundation::parsePackageVersion("0.1.0");
    std::vector<foundation::PackageCandidate> catalog{
        candidate("sample.board"), candidate("sample.host"), candidate("sample.shared")};
    const auto freestanding = foundation::resolvePackageGraph(
        "foundation.package", *dependencies.value, sdk,
        foundation::TargetPlatform::Freestanding, catalog);
    const auto linux = foundation::resolvePackageGraph(
        "foundation.package", *dependencies.value, sdk, foundation::TargetPlatform::Linux,
        catalog);
    expect(freestanding.value.has_value() && linux.value.has_value(),
           "qualified dependencies resolve for both targets:" + errorCodes(freestanding.errors) +
               errorCodes(linux.errors));
    if (!freestanding.value.has_value() || !linux.value.has_value()) {
        return;
    }
    expect(resolvesPackage(freestanding.value->lock, "sample.board") &&
               !resolvesPackage(freestanding.value->lock, "sample.host") &&
               resolvesPackage(freestanding.value->lock, "sample.shared"),
           "freestanding activates freestanding and unqualified dependencies");
    expect(!resolvesPackage(linux.value->lock, "sample.board") &&
               resolvesPackage(linux.value->lock, "sample.host") &&
               resolvesPackage(linux.value->lock, "sample.shared"),
           "a hosted platform activates hosted and unqualified dependencies");

    std::vector<std::string> freestandingSources;
    std::vector<std::string> macosSources;
    for (const auto &source : parsed.value->nativeSources) {
        if (!source.target.has_value() ||
            foundation::targetSelected(*source.target, foundation::TargetPlatform::Freestanding)) {
            freestandingSources.push_back(source.path.generic_string());
        }
        if (!source.target.has_value() ||
            foundation::targetSelected(*source.target, foundation::TargetPlatform::MacOS)) {
            macosSources.push_back(source.path.generic_string());
        }
    }
    expect(freestandingSources ==
               std::vector<std::string>{"native/board.c", "native/shared.c"},
           "freestanding selects freestanding and unqualified native sources");
    expect(macosSources == std::vector<std::string>{"native/host.c", "native/shared.c"},
           "macOS selects hosted and unqualified native sources");

    std::vector<std::string> freestandingLinks;
    std::vector<std::string> windowsLinks;
    for (const auto &link : parsed.value->nativeLinks) {
        if (!link.target.has_value() ||
            foundation::targetSelected(*link.target, foundation::TargetPlatform::Freestanding)) {
            freestandingLinks.push_back(link.library);
        }
        if (!link.target.has_value() ||
            foundation::targetSelected(*link.target, foundation::TargetPlatform::Windows)) {
            windowsLinks.push_back(link.library);
        }
    }
    expect(freestandingLinks == std::vector<std::string>{"soft"},
           "freestanding selects only freestanding links");
    expect(windowsLinks == std::vector<std::string>{"m"},
           "Windows selects hosted links and skips other platforms");
}

void freestandingLocksRoundTrip() {
    foundation::PackageLock lock;
    lock.rootName = "sample.firmware";
    lock.rootVersion = *foundation::parsePackageVersion("1.0.0");
    lock.target = foundation::TargetPlatform::Freestanding;
    const auto rendered = foundation::renderPackageLock(lock);
    expect(rendered ==
               "format foundation.lock/v1\nroot sample.firmware 1.0.0\ntarget freestanding\n",
           "freestanding lock renders its target");
    const auto parsed = foundation::parsePackageLock("foundation.lock", rendered);
    expect(parsed.value.has_value() &&
               parsed.value->target == foundation::TargetPlatform::Freestanding &&
               foundation::renderPackageLock(*parsed.value) == rendered,
           "freestanding lock round-trips");
    const auto hosted = foundation::parsePackageLock(
        "foundation.lock",
        "format foundation.lock/v1\nroot sample.firmware 1.0.0\ntarget hosted\n");
    expect(!hosted.value.has_value() && hasCode(hosted.errors, "FDN4022"),
           "a lock cannot select the hosted selector");
}

void platformQualifiersKeepTheirBytes() {
    constexpr std::string_view existing = R"(format foundation.package/v1
name sample.native
version 1.0.0
sdk ^0.1.0
source src
native_library c
native_name sample_native
native_soversion 2
native_source native/libfuse/increment.c
native_link m target linux
foreign c libfuse 2.9.9 path native/libfuse abi c/v1
dependency sample.mac 1.0.0 registry default target macos
dependency sample.win 1.0.0 registry default target windows
)";
    const auto parsed = foundation::parsePackageManifest("foundation.package", existing);
    expect(parsed.value.has_value() &&
               foundation::renderPackageManifest(*parsed.value) == existing,
           "platform-qualified manifests keep their canonical bytes");
}

} // namespace

int main() {
    selectorsParseAndRenderCanonically();
    overlappingSelectorsAreDuplicates();
    selectorsActivateMatchingEntries();
    freestandingLocksRoundTrip();
    platformQualifiersKeepTheirBytes();
    if (failures != 0) {
        std::cerr << failures << " package target test failure(s)\n";
        return 1;
    }
    return 0;
}
