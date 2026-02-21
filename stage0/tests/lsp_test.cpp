#include "foundation/lsp.hpp"
#include "foundation/package.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

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

std::string jsonEscape(std::string_view value) {
    std::string result;
    for (const auto character : value) {
        if (character == '\\' || character == '"') {
            result += '\\';
            result += character;
        } else if (character == '\n') {
            result += "\\n";
        } else if (character == '\r') {
            result += "\\r";
        } else if (character == '\t') {
            result += "\\t";
        } else {
            result += character;
        }
    }
    return result;
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

void writeFile(const std::filesystem::path &path, std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << contents;
}

class PackageCacheOverride {
  public:
    explicit PackageCacheOverride(const std::filesystem::path &path) {
#ifdef _WIN32
        const auto required = GetEnvironmentVariableW(L"FOUNDATION_PACKAGE_CACHE", nullptr, 0);
        if (required != 0) {
            std::wstring value(required, L'\0');
            const auto copied = GetEnvironmentVariableW(L"FOUNDATION_PACKAGE_CACHE",
                                                        value.data(), required);
            if (copied != 0 && copied < required) {
                value.resize(copied);
                previous_ = std::move(value);
            }
        }
        valid_ = SetEnvironmentVariableW(L"FOUNDATION_PACKAGE_CACHE", path.c_str()) != 0;
#else
        if (const auto *value = std::getenv("FOUNDATION_PACKAGE_CACHE"); value != nullptr) {
            previous_ = value;
        }
        valid_ = setenv("FOUNDATION_PACKAGE_CACHE", path.c_str(), 1) == 0;
#endif
    }

    PackageCacheOverride(const PackageCacheOverride &) = delete;
    PackageCacheOverride &operator=(const PackageCacheOverride &) = delete;

    ~PackageCacheOverride() {
#ifdef _WIN32
        SetEnvironmentVariableW(L"FOUNDATION_PACKAGE_CACHE",
                                previous_.has_value() ? previous_->c_str() : nullptr);
#else
        if (previous_.has_value()) {
            static_cast<void>(setenv("FOUNDATION_PACKAGE_CACHE", previous_->c_str(), 1));
        } else {
            static_cast<void>(unsetenv("FOUNDATION_PACKAGE_CACHE"));
        }
#endif
    }

    [[nodiscard]] bool valid() const { return valid_; }

  private:
#ifdef _WIN32
    std::optional<std::wstring> previous_;
#else
    std::optional<std::string> previous_;
#endif
    bool valid_{};
};

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
    const auto codeLenses =
        "{\"jsonrpc\":\"2.0\",\"id\":13,\"method\":\"textDocument/codeLens\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri + "\"}}}";
    const auto prepareCalls =
        "{\"jsonrpc\":\"2.0\",\"id\":14,"
        "\"method\":\"textDocument/prepareCallHierarchy\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\"},\"position\":{\"line\":1,\"character\":4}}}";
    const auto callRequest = [&sourceUri](int id, std::string_view method,
                                          std::string_view name) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"callHierarchy/" + std::string(method) +
               "\",\"params\":{\"item\":{\"data\":{\"kind\":\"function\","
               "\"name\":\"" + std::string(name) +
               "\",\"scope\":\"function:sample\",\"uri\":\"" + sourceUri +
               "\"}}}}";
    };
    const auto incomingCalls = callRequest(15, "incomingCalls", "add");
    const auto outgoingCalls = callRequest(16, "outgoingCalls", "main");
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit =
        "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";

    std::istringstream input(frame(initialize) + frame(open) + frame(beforeChangeSymbols) +
                             frame(change) +
                             frame(documentSymbols) + frame(workspaceSymbols) + frame(hover) +
                             frame(definition) + frame(completion) + frame(signature) +
                             frame(semanticTokens) + frame(inlayHints) + frame(renameFunction) +
                             frame(codeLenses) + frame(prepareCalls) + frame(incomingCalls) +
                             frame(outgoingCalls) + frame(shutdown) + frame(exit));
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
    const auto codeLensResponse = responseFor(transcript, 13);
    expect(codeLensResponse.find("\"title\":\"1 reference\"") != std::string::npos &&
               codeLensResponse.find("\"command\":\"editor.action.showReferences\"") !=
                   std::string::npos &&
               codeLensResponse.find(sourceUri) != std::string::npos,
           "code lenses expose clickable compiler-resolved reference counts");
    expect(responseFor(transcript, 14).find("\"name\":\"add\"") !=
               std::string::npos,
           "call hierarchy preparation resolves callable symbols");
    const auto incomingResponse = responseFor(transcript, 15);
    expect(incomingResponse.find("\"from\":{") != std::string::npos &&
               incomingResponse.find("\"name\":\"main\"") != std::string::npos &&
               incomingResponse.find("\"character\":16") != std::string::npos,
           "incoming calls identify the compiler-resolved caller and call range");
    const auto outgoingResponse = responseFor(transcript, 16);
    expect(outgoingResponse.find("\"to\":{") != std::string::npos &&
               outgoingResponse.find("\"name\":\"add\"") != std::string::npos &&
               outgoingResponse.find("\"character\":16") != std::string::npos,
           "outgoing calls identify the compiler-resolved callee and call range");

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

void importedPackagesExposeHoverAndAllDefinitions() {
    const auto root = temporaryRoot();
    const auto app = root / "app";
    const auto library = root / "library";
    std::filesystem::create_directories(app);
    std::filesystem::create_directories(library);
    const auto appSource = app / "main.fdn";
    const auto firstSource = library / "first.fdn";
    const auto secondSource = library / "second.fdn";
    {
        std::ofstream file(appSource, std::ios::binary);
        file << "package sample.app\n"
                "import sample.library as lib\n"
                "fn Value() i32 { lib.FirstValue() }\n";
    }
    {
        std::ofstream file(firstSource, std::ios::binary);
        file << "package sample.library\nfn FirstValue() i32 { 1 }\n";
    }
    {
        std::ofstream file(secondSource, std::ios::binary);
        file << "package sample.library\nfn SecondValue() i32 { 2 }\n";
    }
    const auto sourceUri = fileUri(appSource);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri +
        "\",\"version\":1,\"text\":\"package sample.app\\nimport sample.library as lib\\n"
        "fn Value() i32 { lib.FirstValue() }\\n\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":1,\"character\":12}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";

    std::istringstream input(frame(initialize) + frame(open) + frame(request(70, "hover")) +
                             frame(request(71, "definition")) +
                             frame(request(72, "declaration")) + frame(shutdown) +
                             frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto hover = responseFor(transcript, 70);
    const auto definitions = responseFor(transcript, 71);
    const auto declarations = responseFor(transcript, 72);

    expect(status == 0, "package navigation transcript exits cleanly");
    expect(errors.str().empty(), "package navigation writes no server errors");
    expect(hover.find("package sample.library as lib") != std::string::npos &&
               hover.find("2 source files") != std::string::npos &&
               hover.find("\"character\":7,\"line\":1") != std::string::npos,
           "import hover identifies the package, alias, source count, and full range");
    expect(definitions.find(fileUri(firstSource)) != std::string::npos &&
               definitions.find(fileUri(secondSource)) != std::string::npos &&
               definitions.find("\"character\":8,\"line\":0") != std::string::npos,
           "import definition returns every source in the package");
    expect(declarations.find(fileUri(firstSource)) != std::string::npos &&
               declarations.find(fileUri(secondSource)) != std::string::npos,
           "import declaration uses the same package source locations");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void completionsRespectScopesAndMemberAccess() {
    const auto root = temporaryRoot();
    const auto widgets = root / "widgets";
    std::filesystem::create_directories(widgets);
    const auto library = widgets / "widgets.fdn";
    const auto source = root / "main.fdn";
    const auto libraryContents =
        "package widgets\n"
        "contract Reader {\n"
        "    fn Read(view) i32\n"
        "}\n"
        "struct Public implements Reader {\n"
        "    Visible i32\n"
        "    hidden i32\n"
        "    fn Read(view) i32 { self.Visible }\n"
        "    fn internal(view) i32 { self.hidden }\n"
        "}\n"
        "struct internalType {}\n"
        "fn Make(value i32) Public {\n"
        "    Public {\n"
        "        Visible = value\n"
        "        hidden = 0\n"
        "    }\n"
        "}\n"
        "fn hiddenFn() i32 { 0 }\n";
    const auto contents =
        "package sample\n"
        "import widgets as w\n"
        "fn inspect(person view w.Public) i32 {\n"
        "    let outer = 1\n"
        "    if true {\n"
        "        let inner = 2\n"
        "        return person.Visible + inner + outer\n"
        "    }\n"
        "    let later = 3\n"
        "    return later\n"
        "}\n"
        "fn usePackage() w.Public {\n"
        "    w.Make(4)\n"
        "}\n"
        "fn choose() Option<i32> {\n"
        "    Option.Some(1)\n"
        "}\n"
        "fn finalBinding() void {\n"
        "    let finalLocal = 1\n"
        "    \n"
        "}\n"
        "task channelMembers(sender Sender<String>, receiver Receiver<String>) void {\n"
        "    discard sender.send(\"hello\")\n"
        "    discard receiver.receive()\n"
        "}\n";
    const auto incomplete =
        "package sample\n"
        "import widgets as w\n"
        "fn inspect(person view w.Public) i32 {\n"
        "    return person.\n"
        "}\n";
    {
        std::ofstream file(library, std::ios::binary);
        file << libraryContents;
    }
    {
        std::ofstream file(source, std::ios::binary);
        file << contents;
    }
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto completion = [&sourceUri](int id, int line, int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/completion\",\"params\":{"
               "\"textDocument\":{\"uri\":\"" +
               sourceUri + "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto scoped = completion(75, 6, 44);
    const auto packageMembers = completion(76, 12, 6);
    const auto valueMembers = completion(77, 6, 22);
    const auto enumMembers = completion(79, 15, 11);
    const auto finalLocal = completion(80, 19, 4);
    const auto senderMembers = completion(81, 22, 19);
    const auto receiverMembers = completion(82, 23, 21);
    const auto change =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":2},\"contentChanges\":[{\"text\":\"" +
        jsonEscape(incomplete) + "\"}]}}";
    const auto incompleteMembers = completion(78, 3, 18);
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";

    std::istringstream input(frame(initialize) + frame(open) + frame(scoped) +
                             frame(packageMembers) + frame(valueMembers) + frame(enumMembers) +
                             frame(finalLocal) + frame(senderMembers) +
                             frame(receiverMembers) + frame(change) +
                             frame(incompleteMembers) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto scopedResponse = responseFor(transcript, 75);
    const auto packageResponse = responseFor(transcript, 76);
    const auto valueResponse = responseFor(transcript, 77);
    const auto enumResponse = responseFor(transcript, 79);
    const auto finalLocalResponse = responseFor(transcript, 80);
    const auto senderResponse = responseFor(transcript, 81);
    const auto receiverResponse = responseFor(transcript, 82);
    const auto incompleteResponse = responseFor(transcript, 78);

    expect(status == 0, "completion transcript exits cleanly");
    expect(errors.str().empty(), "completion requests write no server errors");
    expect(scopedResponse.find("\"label\":\"person\"") != std::string::npos &&
               scopedResponse.find("\"label\":\"outer\"") != std::string::npos &&
               scopedResponse.find("\"label\":\"inner\"") != std::string::npos &&
               scopedResponse.find("\"label\":\"later\"") == std::string::npos,
           "local completions follow lexical scope and declaration order");
    expect(packageResponse.find("\"label\":\"Make\"") != std::string::npos &&
               packageResponse.find("\"label\":\"Public\"") != std::string::npos &&
               packageResponse.find("\"label\":\"Reader\"") != std::string::npos &&
               packageResponse.find("hiddenFn") == std::string::npos &&
               packageResponse.find("internalType") == std::string::npos,
           "package member completions expose only exported declarations");
    expect(valueResponse.find("\"label\":\"Visible\"") != std::string::npos &&
               valueResponse.find("\"label\":\"Read\"") != std::string::npos &&
               valueResponse.find("hidden") == std::string::npos &&
               valueResponse.find("internal") == std::string::npos &&
               valueResponse.find("\"label\":\"Make\"") == std::string::npos,
           "value member completions use the compiler receiver type and access rules");
    expect(enumResponse.find("\"label\":\"None\"") != std::string::npos &&
               enumResponse.find("\"label\":\"Some\"") != std::string::npos &&
               enumResponse.find("\"label\":\"Ok\"") == std::string::npos,
           "enum member completions stay on the selected enum type");
    expect(finalLocalResponse.find("\"label\":\"finalLocal\"") != std::string::npos,
           "the last local declaration in a block becomes visible after its initializer");
    expect(senderResponse.find("\"label\":\"send\"") != std::string::npos &&
               senderResponse.find("Result<void, ChannelError>") != std::string::npos,
           "Sender completion exposes the typed suspending send operation");
    expect(receiverResponse.find("\"label\":\"receive\"") != std::string::npos &&
               receiverResponse.find("Result<String, ChannelError>") != std::string::npos,
           "Receiver completion exposes the typed suspending receive operation");
    expect(incompleteResponse.find("\"label\":\"Visible\"") != std::string::npos &&
               incompleteResponse.find("\"label\":\"Read\"") != std::string::npos,
           "member completions survive an unfinished dot expression");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void foldingAndSelectionRangesFollowCompilerTokens() {
    const auto root = temporaryRoot();
    std::filesystem::create_directories(root);
    const auto source = root / "main.fdn";
    const auto contents =
        "package sample\n"
        "import std.text\n"
        "import std.path\n"
        "// hidden {\n"
        "// }\n"
        "fn Value(input i32) i32 {\n"
        "    if true {\n"
        "        return input\n"
        "    }\n"
        "    return 0\n"
        "}\n";
    {
        std::ofstream file(source, std::ios::binary);
        file << contents;
    }
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto folding =
        "{\"jsonrpc\":\"2.0\",\"id\":73,\"method\":\"textDocument/foldingRange\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri + "\"}}}";
    const auto selection =
        "{\"jsonrpc\":\"2.0\",\"id\":74,\"method\":\"textDocument/selectionRange\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri + "\"},\"positions\":[{\"line\":7,\"character\":17},"
                    "{\"line\":3,\"character\":5}]}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";

    std::istringstream input(frame(initialize) + frame(open) + frame(folding) +
                             frame(selection) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto initialized = responseFor(transcript, 1);
    const auto folds = responseFor(transcript, 73);
    const auto selections = responseFor(transcript, 74);

    expect(status == 0, "structural range transcript exits cleanly");
    expect(errors.str().empty(), "structural range requests write no server errors");
    expect(initialized.find("\"foldingRangeProvider\":true") != std::string::npos &&
               initialized.find("\"selectionRangeProvider\":true") != std::string::npos,
           "server advertises folding and selection ranges");
    expect(folds.find("\"endLine\":2,\"kind\":\"imports\",\"startLine\":1") !=
                   std::string::npos &&
               folds.find("\"endLine\":9,\"startLine\":5") != std::string::npos &&
               folds.find("\"endLine\":7,\"startLine\":6") != std::string::npos,
           "folding ranges include imports and nested executable blocks");
    expect(folds.find("\"startLine\":3") == std::string::npos,
           "braces inside comments do not create folding ranges");
    expect(selections.find("\"character\":20,\"line\":7") != std::string::npos &&
               selections.find("\"character\":15,\"line\":7") != std::string::npos &&
               selections.find("\"character\":12,\"line\":6") != std::string::npos &&
               selections.find("\"character\":0,\"line\":0") != std::string::npos &&
               selections.find("\"character\":0,\"line\":11") != std::string::npos,
           "selection ranges expand from the identifier through nested blocks to the document");
    expect(selections.find("\"line\":3") == std::string::npos,
           "words inside comments are not compiler token selections");

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
    const auto highlights = request(16, "textDocument/documentHighlight", 3, 38);
    const auto typeDefinition = request(17, "textDocument/typeDefinition", 3, 14);
    const auto declaration = request(18, "textDocument/declaration", 3, 38);
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";

    std::istringstream input(frame(initialize) + frame(open) + frame(leftDefinition) +
                             frame(rightDefinition) + frame(leftReferences) + frame(leftRename) +
                             frame(visibilityRename) + frame(prepare) + frame(highlights) +
                             frame(typeDefinition) + frame(declaration) + frame(shutdown) +
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
    const auto highlightResponse = responseFor(transcript, 16);
    expect(highlightResponse.find("\"line\":1") != std::string::npos &&
               highlightResponse.find("\"line\":3") != std::string::npos &&
               highlightResponse.find("\"line\":2") == std::string::npos &&
               highlightResponse.find("\"line\":4") == std::string::npos,
           "document highlights include only the resolved symbol in the active file");
    const auto typeDefinitionResponse = responseFor(transcript, 17);
    expect(typeDefinitionResponse.find("\"line\":1") != std::string::npos &&
               typeDefinitionResponse.find("\"character\":7") != std::string::npos,
           "type definition follows a parameter to its nominal struct declaration");
    const auto declarationResponse = responseFor(transcript, 18);
    expect(declarationResponse.find("\"line\":1") != std::string::npos &&
               declarationResponse.find("\"character\":14") != std::string::npos,
           "declaration navigation uses the resolved field identity");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void formatsUnsavedDocumentsAndRanges() {
    const auto root = temporaryRoot();
    std::filesystem::create_directories(root);
    const auto source = root / "format.fdn";
    const auto rootUri = fileUri(root);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        rootUri + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri +
        "\",\"version\":1,\"text\":\"package   sample\\nstruct Value<T>{\\nitem   T\\n}\\n"
        "fn main()i32{\\nlet value=Value<i32>{item=2}\\nprint(  \\\"h\\u00e9\\\"  )\\n"
        "return value.item\\n}\\n\"}}}";
    const auto formatting =
        "{\"jsonrpc\":\"2.0\",\"id\":27,\"method\":\"textDocument/formatting\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri + "\"},\"options\":{\"tabSize\":4,\"insertSpaces\":true}}}";
    const auto rangeFormatting =
        "{\"jsonrpc\":\"2.0\",\"id\":28,\"method\":\"textDocument/rangeFormatting\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri +
        "\"},\"range\":{\"start\":{\"line\":2,\"character\":0},"
        "\"end\":{\"line\":3,\"character\":0}},"
        "\"options\":{\"tabSize\":4,\"insertSpaces\":true}}}";
    const auto unicodeRangeFormatting =
        "{\"jsonrpc\":\"2.0\",\"id\":29,\"method\":\"textDocument/rangeFormatting\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri +
        "\"},\"range\":{\"start\":{\"line\":6,\"character\":0},"
        "\"end\":{\"line\":7,\"character\":0}},"
        "\"options\":{\"tabSize\":4,\"insertSpaces\":true}}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";

    std::istringstream input(frame(initialize) + frame(open) + frame(formatting) +
                             frame(rangeFormatting) + frame(unicodeRangeFormatting) +
                             frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto initialized = responseFor(transcript, 1);
    const auto documentEdit = responseFor(transcript, 27);
    const auto rangeEdit = responseFor(transcript, 28);
    const auto unicodeRangeEdit = responseFor(transcript, 29);

    expect(status == 0, "formatting transcript exits cleanly");
    expect(errors.str().empty(), "formatting writes no server errors");
    expect(initialized.find("\"documentFormattingProvider\":true") != std::string::npos &&
               initialized.find("\"documentRangeFormattingProvider\":true") !=
                   std::string::npos,
           "server advertises document and range formatting");
    expect(documentEdit.find("package sample\\nstruct Value<T> {\\n    item T") !=
               std::string::npos &&
               documentEdit.find("let value = Value<i32> { item = 2 }") != std::string::npos,
           "document formatting uses the shared formatter on an unsaved overlay");
    expect(rangeEdit.find("\"newText\":\"    item T\"") != std::string::npos &&
               rangeEdit.find("package sample") == std::string::npos,
           "range formatting edits only selected lines with full-document indentation context");
    expect(unicodeRangeEdit.find("\"end\":{\"character\":15,\"line\":6}") !=
               std::string::npos,
           "range formatting reports UTF-16 line endings");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void offersCompilerBackedDiscardQuickFixes() {
    const auto root = temporaryRoot();
    std::filesystem::create_directories(root);
    const auto source = root / "main.fdn";
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri +
        "\",\"version\":1,\"text\":\"package sample\\nfn makeResult() Result<i32, bool> { "
        "Result<i32, bool>.Ok(1) }\\nfn makeText() String { \\\"owned\\\" }\\n\\n"
        "fn main() i32 {\\n    makeResult()\\n    makeText()\\n    0\\n}\\n\"}}}";
    const auto codeAction =
        "{\"jsonrpc\":\"2.0\",\"id\":30,\"method\":\"textDocument/codeAction\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri +
        "\"},\"range\":{\"start\":{\"line\":5,\"character\":8},"
        "\"end\":{\"line\":5,\"character\":8}},\"context\":{\"diagnostics\":[]}}}";
    const auto ownedCodeAction =
        "{\"jsonrpc\":\"2.0\",\"id\":32,\"method\":\"textDocument/codeAction\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri +
        "\"},\"range\":{\"start\":{\"line\":6,\"character\":8},"
        "\"end\":{\"line\":6,\"character\":8}},\"context\":{\"diagnostics\":[]}}}";
    const auto unrelated =
        "{\"jsonrpc\":\"2.0\",\"id\":31,\"method\":\"textDocument/codeAction\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri +
        "\"},\"range\":{\"start\":{\"line\":4,\"character\":0},"
        "\"end\":{\"line\":4,\"character\":0}},\"context\":{\"diagnostics\":[]}}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";

    std::istringstream input(frame(initialize) + frame(open) + frame(codeAction) +
                             frame(ownedCodeAction) + frame(unrelated) + frame(shutdown) +
                             frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto initialized = responseFor(transcript, 1);
    const auto action = responseFor(transcript, 30);
    const auto ownedAction = responseFor(transcript, 32);
    const auto empty = responseFor(transcript, 31);

    expect(status == 0, "code action transcript exits cleanly");
    expect(errors.str().empty(), "code action writes no server errors");
    expect(initialized.find("\"codeActionKinds\":[\"quickfix\"]") != std::string::npos,
           "server advertises quick fixes");
    expect(action.find("Handle explicitly with discard") != std::string::npos &&
               action.find("\"newText\":\"discard \"") != std::string::npos &&
               action.find("\"character\":4,\"line\":5") != std::string::npos,
           "must-use diagnostic offers an insertion at the statement start");
    expect(ownedAction.find("Handle explicitly with discard") != std::string::npos &&
               ownedAction.find("\"character\":4,\"line\":6") != std::string::npos,
           "owned-value diagnostic offers the same explicit handling fix");
    expect(empty.find("\"result\":[]") != std::string::npos,
           "unrelated selections do not receive quick fixes");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void contractImplementationsIncludeInheritedAndDelegatedTypes() {
    const auto root = temporaryRoot();
    std::filesystem::create_directories(root);
    const auto source = root / "contracts.fdn";
    const auto rootUri = fileUri(root);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        rootUri + "\"}}";
    const auto contents =
        "package sample\n"
        "contract Named {\n"
        "    fn name(view) String\n"
        "}\n"
        "contract Tagged extends Named {}\n"
        "struct Identity implements Named {\n"
        "    fn name(view) String { \"identity\" }\n"
        "}\n"
        "struct Admin implements Tagged {\n"
        "    fn name(view) String { \"admin\" }\n"
        "}\n"
        "struct Wrapper implements Named by identity {\n"
        "    identity Identity\n"
        "}\n"
        "fn make() Identity { Identity {} }\n"
        "fn use() Identity {\n"
        "    let value = make()\n"
        "    value\n"
        "}\n";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::size_t line, std::size_t character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/implementation\",\"params\":{"
               "\"textDocument\":{\"uri\":\"" +
               sourceUri + "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto contractImplementations = request(17, 1, 10);
    const auto methodImplementations = request(18, 2, 8);
    const auto prepareHierarchy =
        "{\"jsonrpc\":\"2.0\",\"id\":19,"
        "\"method\":\"textDocument/prepareTypeHierarchy\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\"},\"position\":{\"line\":1,\"character\":10}}}";
    const auto hierarchyRequest = [&sourceUri](int id, std::string_view method,
                                                std::string_view name) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"typeHierarchy/" + std::string(method) +
               "\",\"params\":{\"item\":{\"data\":{\"kind\":\"contract\","
               "\"name\":\"" + std::string(name) +
               "\",\"scope\":\"type:sample\",\"uri\":\"" + sourceUri + "\"}}}}";
    };
    const auto hierarchySubtypes = hierarchyRequest(20, "subtypes", "Named");
    const auto hierarchySupertypes = hierarchyRequest(21, "supertypes", "Tagged");
    const auto typeDefinitionRequest = [&sourceUri](int id, std::size_t line,
                                                     std::size_t character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/typeDefinition\",\"params\":{"
               "\"textDocument\":{\"uri\":\"" +
               sourceUri + "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto fieldTypeDefinition = typeDefinitionRequest(24, 12, 6);
    const auto callableTypeDefinition = typeDefinitionRequest(25, 14, 4);
    const auto localTypeDefinition = typeDefinitionRequest(26, 17, 6);
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";

    std::istringstream input(frame(initialize) + frame(open) +
                             frame(contractImplementations) + frame(methodImplementations) +
                             frame(prepareHierarchy) + frame(hierarchySubtypes) +
                             frame(hierarchySupertypes) + frame(fieldTypeDefinition) +
                             frame(callableTypeDefinition) + frame(localTypeDefinition) +
                             frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto contracts = responseFor(transcript, 17);
    const auto methods = responseFor(transcript, 18);
    const auto preparedHierarchy = responseFor(transcript, 19);
    const auto subtypes = responseFor(transcript, 20);
    const auto supertypes = responseFor(transcript, 21);
    const auto fieldType = responseFor(transcript, 24);
    const auto callableType = responseFor(transcript, 25);
    const auto localType = responseFor(transcript, 26);

    expect(status == 0, "implementation navigation transcript exits cleanly");
    expect(errors.str().empty(), "implementation navigation writes no server errors");
    expect(contracts.find("\"line\":5") != std::string::npos &&
               contracts.find("\"line\":8") != std::string::npos &&
               contracts.find("\"line\":11") != std::string::npos,
           "contract implementations include direct, inherited, and delegated types");
    expect(methods.find("\"line\":6") != std::string::npos &&
               methods.find("\"line\":9") != std::string::npos &&
               methods.find("\"line\":11") != std::string::npos,
           "contract method implementations include methods and delegated owners");
    expect(preparedHierarchy.find("\"name\":\"Named\"") != std::string::npos &&
               preparedHierarchy.find("\"kind\":11") != std::string::npos &&
               preparedHierarchy.find("\"scope\":\"type:sample\"") != std::string::npos,
           "type hierarchy preparation preserves a stable semantic identity");
    expect(subtypes.find("\"name\":\"Tagged\"") != std::string::npos &&
               subtypes.find("\"name\":\"Identity\"") != std::string::npos &&
               subtypes.find("\"name\":\"Wrapper\"") != std::string::npos &&
               subtypes.find("\"name\":\"Admin\"") == std::string::npos,
           "type hierarchy returns direct child contracts and implementing structs");
    expect(supertypes.find("\"name\":\"Named\"") != std::string::npos,
           "type hierarchy returns direct parent contracts");
    expect(fieldType.find("\"line\":5") != std::string::npos &&
               callableType.find("\"line\":5") != std::string::npos &&
               localType.find("\"line\":5") != std::string::npos,
           "type definition resolves fields, callable returns, and inferred locals");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void callHierarchySeparatesHomonymousMethods() {
    const auto root = temporaryRoot();
    std::filesystem::create_directories(root);
    const auto source = root / "calls.fdn";
    const auto rootUri = fileUri(root);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        rootUri + "\"}}";
    const auto contents =
        "package sample\n"
        "struct Left {\n"
        "    fn value(view) i32 { 1 }\n"
        "}\n"
        "struct Right {\n"
        "    fn value(view) i32 { 2 }\n"
        "}\n"
        "fn readLeft(item Left) i32 { item.value() }\n"
        "fn readRight(item Right) i32 { item.value() }\n";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto prepare =
        "{\"jsonrpc\":\"2.0\",\"id\":22,"
        "\"method\":\"textDocument/prepareCallHierarchy\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\"},\"position\":{\"line\":2,\"character\":8}}}";
    const auto incoming =
        "{\"jsonrpc\":\"2.0\",\"id\":23,"
        "\"method\":\"callHierarchy/incomingCalls\",\"params\":{\"item\":{\"data\":{"
        "\"kind\":\"method\",\"name\":\"value\","
        "\"scope\":\"method:sample.Left\",\"uri\":\"" +
        sourceUri + "\"}}}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";

    std::istringstream input(frame(initialize) + frame(open) + frame(prepare) +
                             frame(incoming) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto prepared = responseFor(transcript, 22);
    const auto callers = responseFor(transcript, 23);

    expect(status == 0, "homonymous call hierarchy transcript exits cleanly");
    expect(errors.str().empty(), "homonymous call hierarchy writes no server errors");
    expect(prepared.find("\"scope\":\"method:sample.Left\"") != std::string::npos,
           "call hierarchy preparation preserves the concrete method owner");
    expect(callers.find("\"name\":\"readLeft\"") != std::string::npos &&
               callers.find("\"line\":7") != std::string::npos &&
               callers.find("\"name\":\"readRight\"") == std::string::npos &&
               callers.find("\"line\":8") == std::string::npos,
           "incoming calls exclude a homonymous method on another struct");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void lockedPackageWorkspaceLoadsCachedDependencies() {
    const auto root = temporaryRoot();
    const auto app = root / "apps" / "sample";
    const auto dependencyRoot = root / "registry" / "example.greeting" / "1.0.0";
    const auto cache = root / "cache";
    const auto appSource = app / "src" / "main.fdn";
    const auto dependencySource = dependencyRoot / "src" / "greeting.fdn";
    const auto appContents = std::string{
        "package example.app\n\n"
        "import example.greeting\n\n"
        "fn main() i32 {\n"
        "    print(greeting.Message())\n"
        "    0\n"
        "}\n"};
    writeFile(dependencyRoot / "foundation.package",
              "format foundation.package/v1\n"
              "name example.greeting\n"
              "version 1.0.0\n"
              "sdk ^0.1.0\n"
              "source src\n");
    writeFile(dependencySource,
              "package example.greeting\n\n"
              "fn Message() String {\n"
              "    \"hello\"\n"
              "}\n");

    foundation::PackageManifest appManifest;
    appManifest.name = "example.app";
    appManifest.version = *foundation::parsePackageVersion("1.0.0");
    appManifest.sdk = *foundation::parsePackageRequirement("^0.1.0");
    appManifest.source = "src";
    appManifest.dependencies.push_back(
        {"example.greeting", *foundation::parsePackageRequirement("^1.0.0"),
         foundation::PackageLocationKind::Registry, "default", std::nullopt});
    writeFile(app / "foundation.package", foundation::renderPackageManifest(appManifest));
    writeFile(appSource, appContents);

    const auto dependencyManifest =
        foundation::readPackageManifest(dependencyRoot / "foundation.package");
    if (!dependencyManifest.value.has_value()) {
        expect(false, "cached dependency manifest parses for language server test");
        std::error_code error;
        std::filesystem::remove_all(root, error);
        return;
    }
    const auto snapshot =
        foundation::inspectPackageSource(dependencyRoot, *dependencyManifest.value);
    if (!snapshot.value.has_value()) {
        expect(false, "cached dependency snapshot exists for language server test");
        std::error_code error;
        std::filesystem::remove_all(root, error);
        return;
    }
    foundation::PackageCandidate candidate;
    candidate.manifest = *dependencyManifest.value;
    candidate.digest = snapshot.value->digest;
    candidate.kind = foundation::PackageLocationKind::Registry;
    candidate.location = "default";
    candidate.root = dependencyRoot;
    const auto installed = foundation::installPackageInCache(cache, candidate);
    if (!installed.value.has_value()) {
        expect(false, "cached dependency installs for language server test");
        std::error_code error;
        std::filesystem::remove_all(root, error);
        return;
    }

    foundation::PackageLock lock;
    lock.rootName = appManifest.name;
    lock.rootVersion = appManifest.version;
    lock.target = foundation::hostTargetPlatform();
    lock.packages.push_back({candidate.manifest.name, candidate.manifest.version,
                             candidate.digest, candidate.kind, candidate.location});
    lock.edges.push_back({appManifest.name, candidate.manifest.name});
    const auto written =
        foundation::writePackageLockAtomically(app / "foundation.lock", lock);
    if (!written.errors.empty()) {
        expect(false, "package lock writes for language server test");
        std::error_code error;
        std::filesystem::remove_all(root, error);
        return;
    }

    PackageCacheOverride environment(cache);
    expect(environment.valid(), "language server test sets the package cache environment");
    const auto rootUri = fileUri(root);
    const auto sourceUri = fileUri(appSource);
    const auto cachedSourceUri = fileUri(*installed.value / "src" / "greeting.fdn");
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{"
        "\"rootUri\":\"" +
        rootUri + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(appContents) + "\"}}}";
    const auto hover =
        "{\"jsonrpc\":\"2.0\",\"id\":40,\"method\":\"textDocument/hover\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri + "\"},\"position\":{\"line\":5,\"character\":22}}}";
    const auto definition =
        "{\"jsonrpc\":\"2.0\",\"id\":41,\"method\":\"textDocument/definition\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri + "\"},\"position\":{\"line\":5,\"character\":22}}}";
    const auto symbols =
        "{\"jsonrpc\":\"2.0\",\"id\":42,\"method\":\"workspace/symbol\","
        "\"params\":{\"query\":\"Message\"}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";

    std::istringstream input(frame(initialize) + frame(open) + frame(hover) +
                             frame(definition) + frame(symbols) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();

    expect(status == 0, "locked package language server transcript exits cleanly");
    expect(errors.str().empty(), "locked package language server writes no server errors");
    expect(transcript.find("\"diagnostics\":[]") != std::string::npos,
           "locked package workspace has no diagnostics");
    expect(responseFor(transcript, 40).find("fn Message() String") != std::string::npos,
           "hover resolves a function from locked cached content");
    expect(responseFor(transcript, 41).find(cachedSourceUri) != std::string::npos,
           "definition opens the verified cached source");
    const auto workspaceSymbols = responseFor(transcript, 42);
    expect(workspaceSymbols.find("Message") != std::string::npos &&
               workspaceSymbols.find(cachedSourceUri) != std::string::npos,
           "workspace symbols include locked cached packages from a nested project");

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

void distributedMethodsExposeDocumentationAndParameters() {
    const auto root = temporaryRoot();
    const auto app = root / "app" / "main.fdn";
    const auto user = root / "profile" / "user.fdn";
    const auto rename = root / "profile" / "rename.fdn";
    writeFile(user,
              "package sample.profile\n"
              "/// A profile edited across source files.\n"
              "struct User {\n"
              "    /// The name shown to people.\n"
              "    Name String\n"
              "}\n");
    writeFile(rename,
              "package sample.profile\n"
              "methods User {\n"
              "    /**\n"
              "     * Replaces the displayed profile name.\n"
              "     */\n"
              "    fn Rename(\n"
              "        edit,\n"
              "        /// The new user-facing name.\n"
              "        name String\n"
              "    ) void { self.Name = name }\n"
              "}\n");
    const std::string appContents =
        "package sample.app\n"
        "import sample.profile\n"
        "fn main() i32 {\n"
        "    var user = profile.User { Name = \"Ada\" }\n"
        "    user.Rename(\"Grace\")\n"
        "    0\n"
        "}\n";
    writeFile(app, appContents);

    const auto rootUri = fileUri(root);
    const auto appUri = fileUri(app);
    const auto renameUri = fileUri(rename);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        rootUri + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" + appUri +
        "\",\"languageId\":\"foundation\",\"version\":1,\"text\":\"" +
        jsonEscape(appContents) + "\"}}}";
    const std::string completionContents =
        "package sample.app\n"
        "import sample.profile\n"
        "fn main() i32 {\n"
        "    var user = profile.User { Name = \"Ada\" }\n"
        "    user.\n"
        "    0\n"
        "}\n";
    const auto changeForCompletion =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" + appUri +
        "\",\"version\":2},\"contentChanges\":[{\"text\":\"" +
        jsonEscape(completionContents) + "\"}]}}";
    const auto request = [&appUri](int id, std::string_view method, int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + appUri +
               "\"},\"position\":{\"line\":4,\"character\":" +
               std::to_string(character) + "}}}";
    };
    const auto typeHover =
        "{\"jsonrpc\":\"2.0\",\"id\":95,\"method\":\"textDocument/hover\","
        "\"params\":{\"textDocument\":{\"uri\":\"" + appUri +
        "\"},\"position\":{\"line\":3,\"character\":24}}}";
    const auto composite =
        "{\"jsonrpc\":\"2.0\",\"id\":94,\"method\":\"foundation/compositeType\","
        "\"params\":{\"textDocument\":{\"uri\":\"" + appUri +
        "\"},\"position\":{\"line\":3,\"character\":24}}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(frame(initialize) + frame(open) +
                             frame(request(90, "hover", 10)) +
                             frame(typeHover) + frame(composite) +
                             frame(request(92, "signatureHelp", 22)) +
                             frame(request(93, "definition", 10)) +
                             frame(changeForCompletion) +
                             frame(request(91, "completion", 9)) + frame(shutdown) +
                             frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto hover = responseFor(transcript, 90);
    const auto completion = responseFor(transcript, 91);
    const auto signature = responseFor(transcript, 92);
    const auto definition = responseFor(transcript, 93);
    const auto compositeType = responseFor(transcript, 94);
    const auto userHover = responseFor(transcript, 95);

    expect(status == 0, "distributed-method language server transcript exits cleanly");
    expect(errors.str().empty(), "distributed-method requests write no server errors");
    expect(hover.find("fn Rename(edit, name String) void") != std::string::npos &&
               hover.find("Replaces the displayed profile name.") != std::string::npos &&
               hover.find("**Parameters**") != std::string::npos &&
               hover.find("The new user-facing name.") != std::string::npos,
           "method hover combines its signature, documentation, and parameter documentation");
    expect(completion.find("\"label\":\"Rename\"") != std::string::npos &&
               completion.find("Replaces the displayed profile name.") != std::string::npos &&
               completion.find("Rename(${1:name})$0") != std::string::npos &&
               completion.find("editor.action.triggerParameterHints") != std::string::npos,
           "member completion carries documentation, parameter placeholders, and signature trigger");
    expect(signature.find("\"label\":\"name String\"") != std::string::npos &&
               signature.find("The new user-facing name.") != std::string::npos &&
               signature.find("Replaces the displayed profile name.") != std::string::npos,
           "signature help carries callable and parameter documentation");
    expect(definition.find(renameUri) != std::string::npos,
           "method definition navigates to its distributed source file");
    expect(compositeType.find("\"typeName\":\"User\"") != std::string::npos &&
               compositeType.find("\"methodCount\":1") != std::string::npos &&
               compositeType.find("\"fileCount\":2") != std::string::npos &&
               compositeType.find("\"kind\":\"method\"") != std::string::npos &&
               compositeType.find("/**") != std::string::npos &&
               compositeType.find(renameUri) != std::string::npos,
           "composite type request preserves documented editable fragments across files");
    expect(userHover.find("1 field, 1 method across 2 files") != std::string::npos &&
               userHover.find("**Fields**") != std::string::npos &&
               userHover.find("The name shown to people.") != std::string::npos &&
               userHover.find("foundationComposite") != std::string::npos,
           "struct hover documents fields, summarizes its shape, and exposes the composite action");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void channelOperationsExposeEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "task work(sender Sender<String>) void {\n"
        "    discard sender.send(\"hello\")\n"
        "}\n"
        "fn main() i32 { 0 }\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":2,\"character\":" +
               std::to_string(character) + "}}}";
    };
    const auto inlay =
        "{\"jsonrpc\":\"2.0\",\"id\":103,\"method\":\"textDocument/inlayHint\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri +
        "\"},\"range\":{\"start\":{\"line\":0,\"character\":0},"
        "\"end\":{\"line\":4,\"character\":20}}}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(frame(initialize) + frame(open) +
                             frame(request(101, "hover", 20)) +
                             frame(request(102, "signatureHelp", 25)) + frame(inlay) +
                             frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto hover = responseFor(transcript, 101);
    const auto signature = responseFor(transcript, 102);
    const auto hints = responseFor(transcript, 103);

    expect(status == 0, "channel operation language server transcript exits cleanly");
    expect(errors.str().empty(), "channel operation requests write no server errors");
    expect(hover.find("fn send(value String) Result<void, ChannelError>") !=
                   std::string::npos &&
               hover.find("ownership transfers") != std::string::npos,
           "channel operation hover explains its typed ownership contract");
    expect(signature.find("fn send(value String) Result<void, ChannelError>") !=
                   std::string::npos &&
               signature.find("Value ownership transfers only") != std::string::npos,
           "channel operation signature help exposes its parameter contract");
    expect(hints.find("value:") != std::string::npos,
           "channel send contributes a parameter inlay hint");

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
    importedPackagesExposeHoverAndAllDefinitions();
    completionsRespectScopesAndMemberAccess();
    foldingAndSelectionRangesFollowCompilerTokens();
    semanticNavigationSeparatesHomonyms();
    formatsUnsavedDocumentsAndRanges();
    offersCompilerBackedDiscardQuickFixes();
    contractImplementationsIncludeInheritedAndDelegatedTypes();
    callHierarchySeparatesHomonymousMethods();
    lockedPackageWorkspaceLoadsCachedDependencies();
    workspaceFoldersAreIndependentAndDynamic();
    diagnosticsStayScopedToTheirWorkspace();
    distributedMethodsExposeDocumentationAndParameters();
    channelOperationsExposeEditorDetails();

    if (failures != 0) {
        std::cerr << failures << " language server assertions failed\n";
        return 1;
    }
    std::cout << "language server tests passed\n";
    return 0;
}
