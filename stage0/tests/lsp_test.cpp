#include "foundation/lsp.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures{};

void expect(bool condition, std::string_view message) {
    if (condition) {
        return;
    }
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

std::string frame(std::string_view body) {
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" +
           std::string(body);
}

std::string fileUri(const std::filesystem::path &path) {
    auto value = std::filesystem::absolute(path).lexically_normal().generic_string();
#ifdef _WIN32
    if (!value.empty() && value.front() != '/') {
        value.insert(value.begin(), '/');
    }
#endif
    return "file://" + value;
}

std::filesystem::path temporaryRoot() {
    const auto sequence = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("foundation-language-server-" + std::to_string(sequence));
}

std::vector<std::string> messages(std::string_view transcript) {
    std::vector<std::string> result;
    std::size_t offset{};
    while (offset < transcript.size()) {
        const auto headerEnd = transcript.find("\r\n\r\n", offset);
        if (headerEnd == std::string_view::npos) {
            break;
        }
        const auto lengthStart = transcript.find(':', offset);
        if (lengthStart == std::string_view::npos || lengthStart >= headerEnd) {
            break;
        }
        const auto length = static_cast<std::size_t>(
            std::stoull(std::string(transcript.substr(lengthStart + 1,
                                                      headerEnd - lengthStart - 1))));
        const auto bodyStart = headerEnd + 4;
        if (bodyStart + length > transcript.size()) {
            break;
        }
        result.emplace_back(transcript.substr(bodyStart, length));
        offset = bodyStart + length;
    }
    return result;
}

std::string responseFor(std::string_view transcript, int id) {
    const auto marker = "\"id\":" + std::to_string(id);
    for (const auto &message : messages(transcript)) {
        if (message.find(marker) != std::string::npos) {
            return message;
        }
    }
    return {};
}

void diagnosticsUseUnsavedCompilerInput() {
    const auto root = temporaryRoot();
    std::filesystem::create_directories(root);
    const auto source = root / "main.fdn";
    {
        std::ofstream file(source, std::ios::binary);
        file << "package sample\nfn main() i32 { 0 }\n";
    }
    const auto rootUri = fileUri(root);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        rootUri + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri +
        "\",\"version\":1,\"text\":\"package sample\\nfn main() i32 { \\\"wrong\\\" }\\n\"}}}";
    const auto change =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri +
        "\",\"version\":2},\"contentChanges\":[{\"text\":\"package sample\\nfn add(left i32, right i32) i32 { left + right }\\nfn main() i32 { add(1, 2) }\\n\"}]}}";
    const auto beforeChangeSymbols =
        "{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"textDocument/documentSymbol\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri + "\"}}}";
    const auto documentSymbols =
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"textDocument/documentSymbol\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri + "\"}}}";
    const auto workspaceSymbols =
        "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"workspace/symbol\","
        "\"params\":{\"query\":\"main\"}}";
    const auto hover =
        "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"textDocument/hover\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri + "\"},\"position\":{\"line\":2,\"character\":17}}}";
    const auto definition =
        "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"textDocument/definition\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri + "\"},\"position\":{\"line\":2,\"character\":17}}}";
    const auto completion =
        "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"textDocument/completion\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri + "\"},\"position\":{\"line\":2,\"character\":16}}}";
    const auto signature =
        "{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"textDocument/signatureHelp\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri + "\"},\"position\":{\"line\":2,\"character\":23}}}";
    const auto semanticTokens =
        "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"textDocument/semanticTokens/full\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri + "\"}}}";
    const auto inlayHints =
        "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"textDocument/inlayHint\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri +
        "\"},\"range\":{\"start\":{\"line\":0,\"character\":0},"
        "\"end\":{\"line\":3,\"character\":0}}}}";
    const auto renameFunction =
        "{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"textDocument/rename\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri +
        "\"},\"position\":{\"line\":2,\"character\":17},\"newName\":\"sum\"}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit =
        "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";

    std::istringstream input(frame(initialize) + frame(open) + frame(beforeChangeSymbols) +
                             frame(change) +
                             frame(documentSymbols) + frame(workspaceSymbols) + frame(hover) +
                             frame(definition) + frame(completion) + frame(signature) +
                             frame(semanticTokens) + frame(inlayHints) + frame(renameFunction) +
                             frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();

    expect(status == 0, "shutdown followed by exit succeeds");
    expect(errors.str().empty(), "valid protocol transcript writes no server errors");
    expect(transcript.find("foundation-ls") != std::string::npos,
           "initialize identifies the language server");
    expect(transcript.find("FDN2011") != std::string::npos,
           "unsaved source is checked by compiler semantics");
    expect(transcript.find("\"diagnostics\":[]") != std::string::npos,
           "a corrected unsaved document clears diagnostics");
    expect(transcript.find("\"positionEncoding\":\"utf-16\"") != std::string::npos,
           "the server declares UTF-16 positions");
    expect(transcript.find("\"documentSymbolProvider\":true") != std::string::npos,
           "initialize advertises document symbols");
    expect(responseFor(transcript, 12).find("\"name\":\"main\"") != std::string::npos &&
               responseFor(transcript, 12).find("\"name\":\"add\"") == std::string::npos,
           "cached analysis reflects the current document version");
    expect(responseFor(transcript, 3).find("\"name\":\"main\"") != std::string::npos &&
               responseFor(transcript, 3).find("\"name\":\"add\"") != std::string::npos,
           "document changes invalidate cached compiler symbols");
    expect(transcript.find("\"id\":4") != std::string::npos &&
               transcript.find("\"location\"") != std::string::npos,
           "workspace symbol search returns source locations");
    expect(transcript.find("\"id\":5") != std::string::npos &&
               transcript.find("fn add(left i32, right i32) i32") != std::string::npos,
           "hover returns the compiler declaration signature");
    expect(transcript.find("\"id\":6") != std::string::npos &&
               transcript.find(sourceUri) != std::string::npos,
           "definition returns the declaration location");
    expect(transcript.find("\"id\":7") != std::string::npos &&
               transcript.find("\"label\":\"add\"") != std::string::npos,
           "completion returns compiler declarations");
    expect(transcript.find("\"id\":8") != std::string::npos &&
               transcript.find("\"activeParameter\":1") != std::string::npos,
           "signature help tracks the active argument");
    expect(transcript.find("\"semanticTokensProvider\"") != std::string::npos &&
               transcript.find("\"inlayHintProvider\":true") != std::string::npos,
           "initialize advertises semantic tokens and inlay hints");
    const auto tokenResponse = responseFor(transcript, 9);
    expect(tokenResponse.find("\"data\":[") != std::string::npos &&
               tokenResponse.find("\"data\":[]") == std::string::npos,
           "semantic tokens encode compiler-resolved declarations and uses");
    const auto hintResponse = responseFor(transcript, 10);
    expect(hintResponse.find("\"label\":\"left:\"") != std::string::npos &&
               hintResponse.find("\"label\":\"right:\"") != std::string::npos,
           "inlay hints name non-obvious call arguments");
    const auto functionRenameResponse = responseFor(transcript, 11);
    expect(functionRenameResponse.find("\"character\":3") != std::string::npos &&
               functionRenameResponse.find("\"character\":6") != std::string::npos &&
               functionRenameResponse.find("\"character\":16") != std::string::npos &&
               functionRenameResponse.find("\"character\":19") != std::string::npos,
           "function rename edits only callee identifiers, not call expressions");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void malformedJsonGetsProtocolError() {
    const auto malformed = "{\"jsonrpc\":\"2.0\",}";
    std::istringstream input(frame(malformed));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);

    expect(status == 1, "end of stream without shutdown is not a clean exit");
    expect(output.str().find("-32700") != std::string::npos,
           "malformed JSON receives a parse error");
}

void lifecycleRejectsInvalidRequestOrder() {
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":30,\"method\":\"shutdown\",\"params\":null}";
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":31,\"method\":\"initialize\",\"params\":{}}";
    const auto initializeAgain =
        "{\"jsonrpc\":\"2.0\",\"id\":32,\"method\":\"initialize\",\"params\":{}}";
    const auto validShutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":33,\"method\":\"shutdown\",\"params\":null}";
    const auto afterShutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":34,\"method\":\"workspace/symbol\","
        "\"params\":{\"query\":\"\"}}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(frame(shutdown) + frame(initialize) + frame(initializeAgain) +
                             frame(validShutdown) + frame(afterShutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();

    expect(status == 0, "valid shutdown and exit complete after lifecycle errors");
    expect(responseFor(transcript, 30).find("-32002") != std::string::npos,
           "shutdown before initialize is rejected");
    expect(responseFor(transcript, 32).find("already initialized") != std::string::npos,
           "a second initialize request is rejected");
    expect(responseFor(transcript, 34).find("server is shutting down") != std::string::npos,
           "requests after shutdown are rejected");
}

void invalidJsonRpcEnvelopeIsRejected() {
    const auto wrongVersion =
        "{\"jsonrpc\":\"1.0\",\"id\":40,\"method\":\"initialize\",\"params\":{}}";
    const auto invalidId =
        "{\"jsonrpc\":\"2.0\",\"id\":true,\"method\":\"initialize\",\"params\":{}}";
    std::istringstream input(frame(wrongVersion) + frame(invalidId));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();

    expect(status == 1, "invalid envelopes do not produce a clean lifecycle exit");
    expect(responseFor(transcript, 40).find("-32600") != std::string::npos,
           "a non-2.0 JSON-RPC envelope is rejected");
    expect(transcript.find("\"id\":true") != std::string::npos &&
               transcript.find("\"code\":-32600") != std::string::npos,
           "an invalid request id type is rejected");
}

void unopenedLibraryDoesNotRequireMain() {
    const auto root = temporaryRoot();
    std::filesystem::create_directories(root);
    const auto source = root / "library.fdn";
    const auto rootUri = fileUri(root);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        rootUri + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri +
        "\",\"version\":1,\"text\":\"package sample\\nfn Value() i32 { 1 }\\n\"}}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit =
        "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";

    std::istringstream input(frame(initialize) + frame(open) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);

    expect(status == 0, "an overlay-only library document is accepted");
    expect(output.str().find("FDN2006") == std::string::npos,
           "editor analysis does not require an executable entry point");
    expect(output.str().find("\"diagnostics\":[]") != std::string::npos,
           "overlay-only library publishes an empty diagnostic set");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void semanticNavigationSeparatesHomonyms() {
    const auto root = temporaryRoot();
    std::filesystem::create_directories(root);
    const auto source = root / "types.fdn";
    const auto rootUri = fileUri(root);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        rootUri + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"package sample\\nstruct Left { value i32 }\\nstruct Right { value i32 }\\nfn leftValue(item Left) i32 { item.value }\\nfn rightValue(item Right) i32 { item.value }\\n\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, std::size_t line,
                                      std::size_t character, std::string_view extra = {}) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}" +
               std::string(extra) + "}}";
    };
    const auto leftDefinition = request(10, "textDocument/definition", 3, 38);
    const auto rightDefinition = request(11, "textDocument/definition", 4, 39);
    const auto leftReferences =
        request(12, "textDocument/references", 3, 38,
                ",\"context\":{\"includeDeclaration\":true}");
    const auto leftRename =
        request(13, "textDocument/rename", 3, 38, ",\"newName\":\"amount\"");
    const auto visibilityRename =
        request(14, "textDocument/rename", 3, 38, ",\"newName\":\"Value\"");
    const auto prepare = request(15, "textDocument/prepareRename", 3, 38);
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";

    std::istringstream input(frame(initialize) + frame(open) + frame(leftDefinition) +
                             frame(rightDefinition) + frame(leftReferences) + frame(leftRename) +
                             frame(visibilityRename) + frame(prepare) + frame(shutdown) +
                             frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto leftDefinitionResponse = responseFor(transcript, 10);
    const auto rightDefinitionResponse = responseFor(transcript, 11);
    const auto referencesResponse = responseFor(transcript, 12);
    const auto renameResponse = responseFor(transcript, 13);

    expect(status == 0, "semantic navigation transcript exits cleanly");
    expect(errors.str().empty(), "semantic navigation writes no server errors");
    expect(leftDefinitionResponse.find("\"line\":1") != std::string::npos &&
               leftDefinitionResponse.find("\"character\":14") != std::string::npos,
           "left field resolves to the Left declaration");
    expect(rightDefinitionResponse.find("\"line\":2") != std::string::npos &&
               rightDefinitionResponse.find("\"character\":15") != std::string::npos,
           "right field resolves to the Right declaration");
    expect(referencesResponse.find("\"line\":1") != std::string::npos &&
               referencesResponse.find("\"line\":3") != std::string::npos &&
               referencesResponse.find("\"line\":2") == std::string::npos &&
               referencesResponse.find("\"line\":4") == std::string::npos,
           "references exclude the homonymous Right field");
    expect(renameResponse.find("\"newText\":\"amount\"") != std::string::npos &&
               renameResponse.find("\"line\":1") != std::string::npos &&
               renameResponse.find("\"line\":3") != std::string::npos &&
               renameResponse.find("\"line\":2") == std::string::npos &&
               renameResponse.find("\"character\":19") != std::string::npos &&
               renameResponse.find("\"character\":40") != std::string::npos,
           "rename edits only the resolved field identity");
    expect(responseFor(transcript, 14).find("\"result\":null") != std::string::npos,
           "rename cannot change export visibility");
    expect(responseFor(transcript, 15).find("\"placeholder\":\"value\"") !=
               std::string::npos,
           "prepare rename returns the resolved source range");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void workspaceFoldersAreIndependentAndDynamic() {
    const auto root = temporaryRoot();
    const auto first = root / "first";
    const auto second = root / "second";
    std::filesystem::create_directories(first);
    std::filesystem::create_directories(second);
    {
        std::ofstream source(first / "alpha.fdn", std::ios::binary);
        source << "package alpha\nfn AlphaValue() i32 { 1 }\n";
    }
    {
        std::ofstream source(second / "beta.fdn", std::ios::binary);
        source << "package beta\nfn BetaValue() i32 { 2 }\n";
    }
    const auto firstUri = fileUri(first);
    const auto secondUri = fileUri(second);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{"
        "\"workspaceFolders\":[{\"uri\":\"" +
        firstUri + "\",\"name\":\"first\"},{\"uri\":\"" + secondUri +
        "\",\"name\":\"second\"}]}}";
    const auto before =
        "{\"jsonrpc\":\"2.0\",\"id\":20,\"method\":\"workspace/symbol\","
        "\"params\":{\"query\":\"Value\"}}";
    const auto remove =
        "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWorkspaceFolders\","
        "\"params\":{\"event\":{\"added\":[],\"removed\":[{\"uri\":\"" +
        firstUri + "\",\"name\":\"first\"}]}}}";
    const auto after =
        "{\"jsonrpc\":\"2.0\",\"id\":21,\"method\":\"workspace/symbol\","
        "\"params\":{\"query\":\"Value\"}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";

    std::istringstream input(frame(initialize) + frame(before) + frame(remove) + frame(after) +
                             frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto beforeResponse = responseFor(transcript, 20);
    const auto afterResponse = responseFor(transcript, 21);

    expect(status == 0, "multi-root workspace transcript exits cleanly");
    expect(errors.str().empty(), "multi-root workspace writes no server errors");
    expect(transcript.find("\"workspaceFolders\":{\"changeNotifications\":true,"
                           "\"supported\":true}") != std::string::npos,
           "initialize advertises dynamic workspace folders");
    expect(beforeResponse.find("AlphaValue") != std::string::npos &&
               beforeResponse.find("BetaValue") != std::string::npos,
           "workspace symbols include every configured root");
    expect(afterResponse.find("AlphaValue") == std::string::npos &&
               afterResponse.find("BetaValue") != std::string::npos,
           "removed workspace roots stop contributing symbols");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void diagnosticsStayScopedToTheirWorkspace() {
    const auto root = temporaryRoot();
    const auto first = root / "first";
    const auto second = root / "second";
    std::filesystem::create_directories(first);
    std::filesystem::create_directories(second);
    const auto firstSource = fileUri(first / "alpha.fdn");
    const auto secondSource = fileUri(second / "beta.fdn");
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{"
        "\"workspaceFolders\":[{\"uri\":\"" +
        fileUri(first) + "\",\"name\":\"first\"},{\"uri\":\"" + fileUri(second) +
        "\",\"name\":\"second\"}]}}";
    const auto open = [](std::string_view uri, std::string_view packageName, int version) {
        return "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
               "\"textDocument\":{\"uri\":\"" +
               std::string(uri) + "\",\"version\":" + std::to_string(version) +
               ",\"text\":\"package " + std::string(packageName) +
               "\\nfn Value() i32 { \\\"wrong\\\" }\\n\"}}}";
    };
    const auto staleChange =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        firstSource +
        "\",\"version\":0},\"contentChanges\":[{\"text\":\"package alpha\\n"
        "fn Value() i32 { 1 }\\n\"}]}}";
    const auto watchedChange =
        "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWatchedFiles\","
        "\"params\":{\"changes\":[{\"uri\":\"" +
        firstSource + "\",\"type\":2}]}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";

    std::istringstream input(frame(initialize) + frame(open(firstSource, "alpha", 1)) +
                             frame(open(secondSource, "beta", 1)) + frame(staleChange) +
                             frame(watchedChange) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    auto firstCleared = false;
    auto firstDiagnosticCount = 0;
    for (const auto &message : messages(transcript)) {
        if (message.find(firstSource) == std::string::npos) {
            continue;
        }
        if (message.find("\"diagnostics\":[]") != std::string::npos) {
            firstCleared = true;
        }
        if (message.find("FDN2011") != std::string::npos) {
            ++firstDiagnosticCount;
        }
    }

    expect(status == 0, "workspace diagnostic transcript exits cleanly");
    expect(!firstCleared, "another workspace cannot clear active diagnostics");
    expect(firstDiagnosticCount == 2,
           "stale versions are ignored and watched source changes refresh diagnostics");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

} // namespace

int main() {
    diagnosticsUseUnsavedCompilerInput();
    malformedJsonGetsProtocolError();
    lifecycleRejectsInvalidRequestOrder();
    invalidJsonRpcEnvelopeIsRejected();
    unopenedLibraryDoesNotRequireMain();
    semanticNavigationSeparatesHomonyms();
    workspaceFoldersAreIndependentAndDynamic();
    diagnosticsStayScopedToTheirWorkspace();

    if (failures != 0) {
        std::cerr << failures << " language server assertions failed\n";
        return 1;
    }
    std::cout << "language server tests passed\n";
    return 0;
}
