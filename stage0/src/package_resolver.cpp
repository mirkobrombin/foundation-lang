#include "foundation/package.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace foundation {

namespace {

constexpr std::size_t maxResolvedPackages = 4096;
constexpr std::size_t maxCatalogCandidates = 65536;
constexpr std::size_t maxDependencyDepth = 256;

bool digestHex(std::string_view digest) {
    if (digest.size() != 71 || !digest.starts_with("sha256:")) {
        return false;
    }
    return std::all_of(digest.begin() + 7, digest.end(), [](const auto byte) {
        return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
    });
}

struct Constraint {
    PackageDependency dependency;
    std::vector<std::string> path;
};

struct SolverState {
    std::map<std::string, std::vector<Constraint>> constraints;
    std::map<std::string, std::size_t> selected;
    std::set<std::pair<std::string, std::string>> edges;
};

struct Failure {
    std::string code;
    std::string message;
};

bool active(const PackageDependency &dependency, TargetPlatform target) {
    return !dependency.target.has_value() || *dependency.target == target;
}

std::string locationName(PackageLocationKind kind) {
    return kind == PackageLocationKind::Path ? "path" : "registry";
}

std::string pathText(const std::vector<std::string> &path) {
    std::string result;
    for (const auto &part : path) {
        if (!result.empty()) {
            result += " -> ";
        }
        result += part;
    }
    return result;
}

bool matches(const PackageCandidate &candidate, const std::vector<Constraint> &constraints,
             const PackageVersion &sdk) {
    if (!candidate.manifest.sdk.accepts(sdk)) {
        return false;
    }
    return std::all_of(constraints.begin(), constraints.end(), [&](const auto &constraint) {
        const auto &dependency = constraint.dependency;
        return dependency.requirement.accepts(candidate.manifest.version) &&
               dependency.kind == candidate.kind && dependency.location == candidate.location;
    });
}

void rememberFailure(std::optional<Failure> &failure, std::string code, std::string message) {
    if (!failure.has_value()) {
        failure = Failure{std::move(code), std::move(message)};
    }
}

std::string conflictMessage(std::string_view name,
                            const std::vector<Constraint> &constraints) {
    std::ostringstream output;
    output << "cannot resolve package " << name;
    for (const auto &constraint : constraints) {
        output << "; " << pathText(constraint.path) << " requires "
               << constraint.dependency.requirement.string() << " from "
               << locationName(constraint.dependency.kind) << ' '
               << constraint.dependency.location;
    }
    return output.str();
}

std::optional<std::vector<std::string>> dependencyCycle(
    const std::set<std::pair<std::string, std::string>> &edges) {
    std::map<std::string, std::vector<std::string>> graph;
    for (const auto &[parent, dependency] : edges) {
        graph[parent].push_back(dependency);
        graph.try_emplace(dependency);
    }
    std::map<std::string, unsigned char> state;
    std::vector<std::string> stack;
    std::optional<std::vector<std::string>> result;
    std::function<bool(const std::string &)> visit = [&](const auto &name) {
        state[name] = 1;
        stack.push_back(name);
        for (const auto &dependency : graph[name]) {
            if (state[dependency] == 0 && visit(dependency)) {
                return true;
            }
            if (state[dependency] == 1) {
                const auto start = std::find(stack.begin(), stack.end(), dependency);
                result = std::vector<std::string>(start, stack.end());
                result->push_back(dependency);
                return true;
            }
        }
        stack.pop_back();
        state[name] = 2;
        return false;
    };
    for (const auto &[name, dependencies] : graph) {
        static_cast<void>(dependencies);
        if (state[name] == 0 && visit(name)) {
            break;
        }
    }
    return result;
}

bool solve(const std::vector<PackageCandidate> &catalog, const PackageVersion &sdk,
           TargetPlatform target, SolverState &state, std::optional<Failure> &failure) {
    auto unresolved = state.constraints.end();
    for (auto candidate = state.constraints.begin(); candidate != state.constraints.end();
         ++candidate) {
        if (!state.selected.contains(candidate->first)) {
            unresolved = candidate;
            break;
        }
    }
    if (unresolved == state.constraints.end()) {
        const auto cycle = dependencyCycle(state.edges);
        if (cycle.has_value()) {
            rememberFailure(failure, "FDN4054",
                            "package dependency cycle: " + pathText(*cycle));
            return false;
        }
        return true;
    }
    if (state.selected.size() == maxResolvedPackages) {
        rememberFailure(failure, "FDN4055", "package graph exceeds 4096 packages");
        return false;
    }

    std::vector<std::size_t> choices;
    for (std::size_t index = 0; index < catalog.size(); ++index) {
        if (catalog[index].manifest.name == unresolved->first &&
            matches(catalog[index], unresolved->second, sdk)) {
            choices.push_back(index);
        }
    }
    std::sort(choices.begin(), choices.end(), [&](const auto left, const auto right) {
        const auto &leftCandidate = catalog[left];
        const auto &rightCandidate = catalog[right];
        if (leftCandidate.manifest.version != rightCandidate.manifest.version) {
            return leftCandidate.manifest.version > rightCandidate.manifest.version;
        }
        return std::tie(leftCandidate.location, leftCandidate.digest) <
               std::tie(rightCandidate.location, rightCandidate.digest);
    });
    if (choices.empty()) {
        rememberFailure(failure, "FDN4052",
                        conflictMessage(unresolved->first, unresolved->second));
        return false;
    }

    const auto origin = std::min_element(
        unresolved->second.begin(), unresolved->second.end(), [](const auto &left,
                                                                 const auto &right) {
            return left.path < right.path;
        });
    for (const auto choice : choices) {
        auto next = state;
        next.selected.emplace(unresolved->first, choice);
        auto valid = true;
        for (const auto &dependency : catalog[choice].manifest.dependencies) {
            if (!active(dependency, target)) {
                continue;
            }
            auto path = origin->path;
            if (path.size() == maxDependencyDepth) {
                rememberFailure(failure, "FDN4055",
                                "package graph exceeds dependency depth 256");
                valid = false;
                break;
            }
            if (std::find(path.begin(), path.end(), dependency.name) != path.end()) {
                path.push_back(dependency.name);
                rememberFailure(failure, "FDN4054",
                                "package dependency cycle: " + pathText(path));
                valid = false;
                break;
            }
            path.push_back(dependency.name);
            next.constraints[dependency.name].push_back({dependency, std::move(path)});
            next.edges.emplace(unresolved->first, dependency.name);
        }
        if (!valid) {
            continue;
        }
        for (const auto &[name, selected] : next.selected) {
            if (!matches(catalog[selected], next.constraints[name], sdk)) {
                valid = false;
                break;
            }
        }
        if (valid && solve(catalog, sdk, target, next, failure)) {
            state = std::move(next);
            return true;
        }
    }
    return false;
}

} // namespace

PackageParseResult<PackageResolution>
resolvePackageGraph(const std::filesystem::path &rootManifestPath,
                    const PackageManifest &root, const PackageVersion &sdk,
                    TargetPlatform target, std::span<const PackageCandidate> inputCatalog) {
    PackageParseResult<PackageResolution> result;
    if (!root.sdk.accepts(sdk)) {
        result.errors.push_back({rootManifestPath, 1, 1, "FDN4050",
                                 "SDK " + sdk.string() + " does not satisfy root requirement " +
                                     root.sdk.string()});
        return result;
    }
    if (inputCatalog.size() > maxCatalogCandidates) {
        result.errors.push_back({rootManifestPath, 1, 1, "FDN4055",
                                 "package catalog exceeds 65536 candidates"});
        return result;
    }

    std::vector<PackageCandidate> catalog(inputCatalog.begin(), inputCatalog.end());
    const auto invalid = std::find_if(catalog.begin(), catalog.end(), [](const auto &candidate) {
        return candidate.manifest.name.empty() || !digestHex(candidate.digest) ||
               candidate.location.empty();
    });
    if (invalid != catalog.end()) {
        result.errors.push_back({rootManifestPath, 1, 1, "FDN4051",
                                 "invalid package candidate " + invalid->manifest.name});
        return result;
    }
    std::sort(catalog.begin(), catalog.end(), [](const auto &left, const auto &right) {
        return std::tie(left.manifest.name, left.manifest.version, left.kind, left.location,
                        left.digest) <
               std::tie(right.manifest.name, right.manifest.version, right.kind,
                        right.location, right.digest);
    });
    const auto duplicate = std::adjacent_find(catalog.begin(), catalog.end(),
                                              [](const auto &left, const auto &right) {
        return left.manifest.name == right.manifest.name &&
               left.manifest.version == right.manifest.version &&
               left.kind == right.kind && left.location == right.location;
    });
    if (duplicate != catalog.end()) {
        result.errors.push_back(
            {rootManifestPath, 1, 1, "FDN4053",
             "duplicate package candidate " + duplicate->manifest.name + ' ' +
                 duplicate->manifest.version.string() + " from " +
                 locationName(duplicate->kind) + ' ' + duplicate->location});
        return result;
    }

    SolverState state;
    for (const auto &dependency : root.dependencies) {
        if (!active(dependency, target)) {
            continue;
        }
        state.constraints[dependency.name].push_back(
            {dependency, {root.name, dependency.name}});
        state.edges.emplace(root.name, dependency.name);
    }
    std::optional<Failure> failure;
    if (!solve(catalog, sdk, target, state, failure)) {
        const auto &resolvedFailure = *failure;
        result.errors.push_back(
            {rootManifestPath, 1, 1, resolvedFailure.code, resolvedFailure.message});
        return result;
    }

    PackageResolution resolution;
    resolution.lock.rootName = root.name;
    resolution.lock.rootVersion = root.version;
    resolution.lock.target = target;
    for (const auto &[name, index] : state.selected) {
        const auto &candidate = catalog[index];
        resolution.lock.packages.push_back({name, candidate.manifest.version, candidate.digest,
                                            candidate.kind, candidate.location});
        resolution.packages.push_back(candidate);
    }
    for (const auto &[parent, dependency] : state.edges) {
        resolution.lock.edges.push_back({parent, dependency});
    }
    result.value = std::move(resolution);
    return result;
}

} // namespace foundation
