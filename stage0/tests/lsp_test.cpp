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

void namedArgumentsSelectDeclaredSignatureParameter() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "fn compose(first i32, second i32, third i32) i32 {\n"
        "    first * 100 + second * 10 + third\n"
        "}\n"
        "fn main() i32 {\n"
        "    compose(third = 3, first = 1, second = 2)\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/signatureHelp\",\"params\":{"
               "\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":5,\"character\":" +
               std::to_string(character) + "}}}";
    };
    const auto positionRequest = [&sourceUri](int id, std::string_view method,
                                               int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":5,\"character\":" +
               std::to_string(character) + "}}}";
    };
    const auto rename =
        "{\"jsonrpc\":\"2.0\",\"id\":205,\"method\":\"textDocument/rename\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri +
        "\"},\"position\":{\"line\":5,\"character\":24},\"newName\":\"head\"}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(201, 21)) +
        frame(request(202, 32)) + frame(positionRequest(203, "definition", 14)) +
        frame(positionRequest(204, "hover", 14)) + frame(rename) + frame(shutdown) +
        frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();

    expect(status == 0, "named argument signature transcript exits cleanly");
    expect(errors.str().empty(), "named argument signature requests write no server errors");
    expect(responseFor(transcript, 201).find("\"activeParameter\":2") != std::string::npos,
           "third named argument selects the third declared parameter");
    expect(responseFor(transcript, 202).find("\"activeParameter\":0") != std::string::npos,
           "first named argument selects the first declared parameter");
    const auto definitionResponse = responseFor(transcript, 203);
    expect(definitionResponse.find("\"character\":34,\"line\":1") !=
               std::string::npos,
           "named argument definition points to its declared parameter");
    expect(responseFor(transcript, 204).find("third i32") != std::string::npos,
           "named argument hover exposes its declared parameter");
    const auto renameResponse = responseFor(transcript, 205);
    expect(renameResponse.find("\"character\":11") != std::string::npos &&
               renameResponse.find("\"character\":23") != std::string::npos,
           "renaming a named argument updates the declaration and call label");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void namedEnumPayloadsExposeEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "enum Command {\n"
        "    Start\n"
        "    Stop(code i32)\n"
        "}\n"
        "fn main() i32 {\n"
        "    const command = Command.Stop(code = 1)\n"
        "    discard command\n"
        "    0\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character, std::string_view extra = {}) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}" +
               std::string(extra) + "}}";
    };
    const auto rename =
        "{\"jsonrpc\":\"2.0\",\"id\":215,\"method\":\"textDocument/rename\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri +
        "\"},\"position\":{\"line\":6,\"character\":34},\"newName\":\"status\"}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(211, "hover", 3, 10)) +
        frame(request(212, "completion", 6, 28)) +
        frame(request(213, "signatureHelp", 6, 40)) +
        frame(request(214, "definition", 6, 34)) + frame(rename) +
        frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();

    expect(status == 0, "named enum payload transcript exits cleanly");
    expect(errors.str().empty(), "named enum payload requests write no server errors");
    expect(responseFor(transcript, 211).find("code i32") != std::string::npos,
           "enum payload hover exposes its name and type");
    const auto completion = responseFor(transcript, 212);
    expect(completion.find("Stop(code i32)") != std::string::npos &&
               completion.find("Stop(${1:code})$0") != std::string::npos,
           "enum completion uses the declared payload name");
    const auto signature = responseFor(transcript, 213);
    expect(signature.find("Stop(code i32)") != std::string::npos &&
               signature.find("\"label\":\"code i32\"") != std::string::npos,
           "enum constructor signature exposes the declared payload");
    expect(responseFor(transcript, 214).find("\"character\":9,\"line\":3") !=
               std::string::npos,
           "named enum constructor label navigates to its payload declaration");
    const auto renameResponse = responseFor(transcript, 215);
    expect(renameResponse.find("\"newText\":\"status\"") != std::string::npos &&
               renameResponse.find("\"character\":9,\"line\":3") !=
                   std::string::npos &&
               renameResponse.find("\"character\":33,\"line\":6") !=
                   std::string::npos,
           "renaming an enum payload updates its declaration and named constructor label");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void testDeclarationsExposeEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "test \"addition works\" {\n"
        "    expect(20 + 22 == 42)\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" + sourceUri +
        "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto symbols =
        "{\"jsonrpc\":\"2.0\",\"id\":221,\"method\":\"textDocument/documentSymbol\","
        "\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri + "\"}}}";
    const auto completion =
        "{\"jsonrpc\":\"2.0\",\"id\":222,\"method\":\"textDocument/completion\","
        "\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
        "\"},\"position\":{\"line\":3,\"character\":1}}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(frame(initialize) + frame(open) + frame(symbols) +
                             frame(completion) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto symbolResponse = responseFor(transcript, 221);
    const auto completionResponse = responseFor(transcript, 222);

    expect(status == 0, "test declaration transcript exits cleanly");
    expect(errors.str().empty(), "test declaration requests write no server errors");
    expect(symbolResponse.find("\"name\":\"addition works\"") != std::string::npos &&
               symbolResponse.find("$test") == std::string::npos,
           "test declaration appears in Outline by its human name");
    expect(completionResponse.find("$test") == std::string::npos,
           "generated test functions stay out of completion");
    expect(completionResponse.find("\"label\":\"test\"") != std::string::npos &&
               completionResponse.find("\"label\":\"expect\"") != std::string::npos &&
               completionResponse.find("\"label\":\"fail\"") != std::string::npos &&
               completionResponse.find("\"label\":\"pass\"") != std::string::npos,
           "test declarations and prelude assertions receive completion");

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
        "    fn Read(self) i32\n"
        "}\n"
        "struct Public implements Reader {\n"
        "    Visible i32\n"
        "    hidden i32\n"
        "    fn Read(self) i32 { self.Visible }\n"
        "    fn internal(self) i32 { self.hidden }\n"
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
        "fn inspect(person w.Public) i32 {\n"
        "    const outer = 1\n"
        "    if true {\n"
        "        const inner = 2\n"
        "        return person.Visible + inner + outer\n"
        "    }\n"
        "    const later = 3\n"
        "    return later\n"
        "}\n"
        "fn usePackage() w.Public {\n"
        "    w.Make(4)\n"
        "}\n"
        "fn choose() Option<i32> {\n"
        "    Option.Some(1)\n"
        "}\n"
        "fn finalBinding() void {\n"
        "    const finalLocal = 1\n"
        "    \n"
        "}\n"
        "task channelMembers($sender Sender<String>, $receiver Receiver<String>) void {\n"
        "    discard sender.send(\"hello\")\n"
        "    discard receiver.receive()\n"
        "}\n";
    const auto incomplete =
        "package sample\n"
        "import widgets as w\n"
        "fn inspect(person w.Public) i32 {\n"
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
               senderResponse.find("\"label\":\"clone\"") != std::string::npos &&
               senderResponse.find("fn clone() Sender<String>") != std::string::npos &&
               senderResponse.find("Result<void, ChannelError>") != std::string::npos,
           "Sender completion exposes send and explicit handle cloning");
    expect(receiverResponse.find("\"label\":\"receive\"") != std::string::npos &&
               receiverResponse.find("Result<String, ChannelError>") != std::string::npos,
           "Receiver completion exposes the typed suspending receive operation");
    expect(incompleteResponse.find("\"label\":\"Visible\"") != std::string::npos &&
               incompleteResponse.find("\"label\":\"Read\"") != std::string::npos,
           "member completions survive an unfinished dot expression");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void numericConversionsExposeEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "fn main() i32 {\n"
        "    const source i64 = 7\n"
        "    const checked = i32.From(source)\n"
        "    discard checked\n"
        "    0\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(83, "completion", 3, 24)) +
        frame(request(84, "hover", 3, 26)) +
        frame(request(85, "signatureHelp", 3, 29)) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto completion = responseFor(transcript, 83);
    const auto hover = responseFor(transcript, 84);
    const auto signature = responseFor(transcript, 85);

    expect(status == 0, "numeric conversion language server transcript exits cleanly");
    expect(errors.str().empty(), "numeric conversion requests write no server errors");
    expect(completion.find("\"label\":\"From\"") != std::string::npos &&
               completion.find("Result<i32, NumberError>") != std::string::npos,
           "numeric types complete the checked From conversion");
    expect(hover.find("fn From(value i64) Result<i32, NumberError>") !=
                   std::string::npos &&
               hover.find("explicit numeric conversion") != std::string::npos,
           "numeric conversion hover exposes its compiler-resolved signature");
    expect(signature.find("fn From(value i64) Result<i32, NumberError>") !=
                   std::string::npos &&
               signature.find("\"label\":\"value i64\"") != std::string::npos,
           "numeric conversion signature help exposes source and result types");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void fieldDefaultsExposeEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "struct Settings {\n"
        "    label String = makeLabel()\n"
        "}\n"
        "fn makeLabel() String { \"ready\" }\n"
        "fn main() i32 {\n"
        "    const settings = Settings {}\n"
        "    print(settings.label)\n"
        "    0\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto hover =
        "{\"jsonrpc\":\"2.0\",\"id\":86,\"method\":\"textDocument/hover\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri + "\"},\"position\":{\"line\":2,\"character\":7}}}";
    const auto symbols =
        "{\"jsonrpc\":\"2.0\",\"id\":87,\"method\":\"textDocument/documentSymbol\","
        "\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri + "\"}}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(frame(initialize) + frame(open) + frame(hover) +
                             frame(symbols) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto hoverResponse = responseFor(transcript, 86);
    const auto symbolResponse = responseFor(transcript, 87);

    expect(status == 0, "field default language server transcript exits cleanly");
    expect(errors.str().empty(), "field default requests write no server errors");
    expect(hoverResponse.find("label String = makeLabel()") != std::string::npos,
           "field hover includes the declared default expression");
    expect(symbolResponse.find("$field_default") == std::string::npos,
           "field default implementation functions stay hidden from editor symbols");

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
        "fn main()i32{\\nconst value=Value<i32>{item=2}\\nprint(  \\\"h\\u00e9\\\"  )\\n"
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
               documentEdit.find("const value = Value<i32> { item = 2 }") != std::string::npos,
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
        "    fn name(self) String\n"
        "}\n"
        "contract Tagged extends Named {}\n"
        "struct Identity implements Named {\n"
        "    fn name(self) String { \"identity\" }\n"
        "}\n"
        "struct Admin implements Tagged {\n"
        "    fn name(self) String { \"admin\" }\n"
        "}\n"
        "struct Wrapper implements Named {\n"
        "    identity Identity\n"
        "    delegate identity as Named\n"
        "}\n"
        "fn make() Identity { Identity {} }\n"
        "fn use() Identity {\n"
        "    const value = make()\n"
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
    const auto callableTypeDefinition = typeDefinitionRequest(25, 15, 4);
    const auto localTypeDefinition = typeDefinitionRequest(26, 18, 6);
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
        "    fn value(self) i32 { 1 }\n"
        "}\n"
        "struct Right {\n"
        "    fn value(self) i32 { 2 }\n"
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

void semanticIntelliSenseKeepsDocumentationTextual() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "attribute Source() targets(parameter)\n"
        "struct User {}\n"
        "enum ResolveError { Missing }\n"
        "// Resolves `user` while `missingSymbol` remains ordinary Markdown.\n"
        "fn Resolve(@Source() user User) Result<User, ResolveError> {\n"
        "    .Ok(user)\n"
        "}\n"
        "fn main() i32 {\n"
        "    const user = User {}\n"
        "    discard Resolve(user)\n"
        "    0\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{"
        "\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(80, "hover", 10, 14)) +
        frame(request(81, "signatureHelp", 10, 23)) +
        frame(request(82, "definition", 4, 30)) +
        frame(request(83, "hover", 5, 22)) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto hover = responseFor(transcript, 80);
    const auto signature = responseFor(transcript, 81);
    const auto documentationDefinition = responseFor(transcript, 82);
    const auto parameterHover = responseFor(transcript, 83);
    const auto documentation = std::string("`missingSymbol` remains ordinary Markdown");

    expect(status == 0, "semantic documentation transcript exits cleanly");
    expect(errors.str().empty(), "semantic documentation requests write no server errors");
    expect(hover.find("fn Resolve(@Source() user User) Result<User, ResolveError>") !=
                   std::string::npos &&
               hover.find(documentation) != std::string::npos &&
               hover.find("**Parameters**") == std::string::npos,
           "hover combines compiler signature facts with unchanged Markdown prose");
    expect(hover.find("\"foundationTypes\":[") != std::string::npos &&
               hover.find("\"label\":\"User\"") != std::string::npos &&
               hover.find("\"label\":\"ResolveError\"") != std::string::npos,
           "hover publishes compiler-resolved nominal type targets");
    expect(signature.find("fn Resolve(@Source() user User) Result<User, ResolveError>") !=
                   std::string::npos &&
               signature.find("\"foundationTypes\":[") != std::string::npos &&
               signature.find("\"label\":\"@Source() user User\"") !=
                   std::string::npos &&
               signature.find(documentation) != std::string::npos &&
               signature.find(documentation, signature.find(documentation) + 1) ==
                   std::string::npos,
           "signature help publishes decorators, callable facts, and nominal type targets");
    expect(documentationDefinition.find("\"result\":null") != std::string::npos,
           "documentation names do not participate in semantic navigation");
    expect(parameterHover.find(documentation) == std::string::npos,
           "inline parameters do not inherit the callable documentation block");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void distributedMethodsExposeDocumentationAndParameters() {
    const auto root = temporaryRoot();
    const auto app = root / "app" / "main.fdn";
    const auto user = root / "profile" / "user.fdn";
    const auto create = root / "profile" / "create.fdn";
    const auto rename = root / "profile" / "rename.fdn";
    const std::string userContents =
        "package sample.profile\n"
        "// A profile edited across source files.\n"
        "struct User {\n"
        "    // The name shown to people.\n"
        "    Name String\n"
        "}\n";
    writeFile(user, userContents);
    writeFile(create,
              "package sample.profile\n"
              "methods User {\n"
              "    // Creates a user with the supplied display name.\n"
              "    fn New($name String) User { User { Name = name } }\n"
              "}\n");
    writeFile(rename,
              "package sample.profile\n"
              "methods User {\n"
              "    // Replaces the displayed profile name.\n"
              "    fn Rename(\n"
              "        &self,\n"
              "        // The new user-facing name.\n"
              "        $name String\n"
              "    ) void { self.Name = name }\n"
              "}\n");
    const std::string appContents =
        "package sample.app\n"
        "import sample.profile\n"
        "fn main() i32 {\n"
        "    var user = profile.User.New(\"Ada\")\n"
        "    user.Rename(\"Grace\")\n"
        "    0\n"
        "}\n";
    writeFile(app, appContents);

    const auto rootUri = fileUri(root);
    const auto appUri = fileUri(app);
    const auto userUri = fileUri(user);
    const auto renameUri = fileUri(rename);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        rootUri + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" + appUri +
        "\",\"languageId\":\"foundation\",\"version\":1,\"text\":\"" +
        jsonEscape(appContents) + "\"}}}";
    const auto openUser =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" + userUri +
        "\",\"languageId\":\"foundation\",\"version\":1,\"text\":\"" +
        jsonEscape(userContents) + "\"}}}";
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
    const std::string associatedCompletionContents =
        "package sample.app\n"
        "import sample.profile\n"
        "fn main() i32 {\n"
        "    const unused = 0\n"
        "    profile.User.\n"
        "    0\n"
        "}\n";
    const auto changeForAssociatedCompletion =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" + appUri +
        "\",\"version\":3},\"contentChanges\":[{\"text\":\"" +
        jsonEscape(associatedCompletionContents) + "\"}]}}";
    const auto samePackageCompletionContents = userContents + "User.\n";
    const auto changeForSamePackageCompletion =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" + userUri +
        "\",\"version\":2},\"contentChanges\":[{\"text\":\"" +
        jsonEscape(samePackageCompletionContents) + "\"}]}}";
    const auto restoreApp =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" + appUri +
        "\",\"version\":4},\"contentChanges\":[{\"text\":\"" +
        jsonEscape(appContents) + "\"}]}}";
    const auto samePackageCompletion =
        "{\"jsonrpc\":\"2.0\",\"id\":98,\"method\":\"textDocument/completion\","
        "\"params\":{\"textDocument\":{\"uri\":\"" + userUri +
        "\"},\"position\":{\"line\":6,\"character\":5}}}";
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
    const auto codeLens =
        "{\"jsonrpc\":\"2.0\",\"id\":96,\"method\":\"textDocument/codeLens\","
        "\"params\":{\"textDocument\":{\"uri\":\"" + userUri + "\"}}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(frame(initialize) + frame(open) + frame(openUser) +
                             frame(request(90, "hover", 10)) +
                             frame(typeHover) + frame(composite) +
                             frame(request(92, "signatureHelp", 22)) +
                             frame(request(93, "definition", 10)) +
                             frame(codeLens) +
                             frame(changeForCompletion) +
                             frame(request(91, "completion", 9)) +
                             frame(changeForAssociatedCompletion) +
                             frame(request(97, "completion", 17)) +
                             frame(restoreApp) +
                             frame(changeForSamePackageCompletion) +
                             frame(samePackageCompletion) + frame(shutdown) + frame(exit));
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
    const auto codeLenses = responseFor(transcript, 96);
    const auto associatedCompletion = responseFor(transcript, 97);
    const auto localTypeCompletion = responseFor(transcript, 98);

    expect(status == 0, "distributed-method language server transcript exits cleanly");
    expect(errors.str().empty(), "distributed-method requests write no server errors");
    expect(hover.find("fn Rename(&self, $name String) void") != std::string::npos &&
               hover.find("Replaces the displayed profile name.") != std::string::npos &&
               hover.find("**Parameters**") != std::string::npos &&
               hover.find("The new user-facing name.") != std::string::npos,
           "method hover combines its signature, documentation, and parameter documentation");
    expect(completion.find("\"label\":\"Rename\"") != std::string::npos &&
               completion.find("Replaces the displayed profile name.") != std::string::npos &&
               completion.find("Rename($${1:name})$0") != std::string::npos &&
               completion.find("editor.action.triggerParameterHints") != std::string::npos,
           "member completion carries documentation, parameter placeholders, and signature trigger");
    expect(associatedCompletion.find("\"label\":\"New\"") != std::string::npos &&
               associatedCompletion.find("Creates a user with the supplied display name.") !=
                   std::string::npos &&
               associatedCompletion.find("New($${1:name})$0") != std::string::npos,
           "type completion exposes associated functions without instance members");
    expect(localTypeCompletion.find("\"label\":\"New\"") != std::string::npos &&
               localTypeCompletion.find("New($${1:name})$0") != std::string::npos &&
               localTypeCompletion.find("\"label\":\"Rename\"") == std::string::npos,
           "same-package type completion survives an incomplete top-level access");
    expect(signature.find("\"label\":\"$name String\"") != std::string::npos &&
               signature.find("The new user-facing name.") != std::string::npos &&
               signature.find("Replaces the displayed profile name.") != std::string::npos,
           "signature help carries callable and parameter documentation");
    expect(definition.find(renameUri) != std::string::npos,
           "method definition navigates to its distributed source file");
    expect(compositeType.find("\"typeName\":\"User\"") != std::string::npos &&
               compositeType.find("\"methodCount\":2") != std::string::npos &&
               compositeType.find("\"fileCount\":3") != std::string::npos &&
               compositeType.find("\"kind\":\"method\"") != std::string::npos &&
               compositeType.find("// Replaces the displayed profile name.") != std::string::npos &&
               compositeType.find(renameUri) != std::string::npos,
           "composite type request preserves documented source locations across files");
    expect(userHover.find("1 field, 2 methods across 3 files") != std::string::npos &&
               userHover.find("**Fields**") != std::string::npos &&
               userHover.find("The name shown to people.") != std::string::npos &&
               userHover.find("foundationComposite") != std::string::npos,
           "struct hover documents fields, summarizes its shape, and exposes the composite action");
    expect(codeLenses.find("\"end\":{\"character\":1,\"line\":1}") !=
               std::string::npos &&
               codeLenses.find("\"end\":{\"character\":1,\"line\":3}") !=
                   std::string::npos,
           "code lenses stay above attached documentation blocks");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void channelOperationsExposeEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "task work($sender Sender<String>) void {\n"
        "    const copy = sender.clone()\n"
        "    discard copy\n"
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
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                     int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" +
               std::to_string(character) + "}}}";
    };
    const auto inlay =
        "{\"jsonrpc\":\"2.0\",\"id\":103,\"method\":\"textDocument/inlayHint\","
        "\"params\":{\"textDocument\":{\"uri\":\"" +
        sourceUri +
        "\"},\"range\":{\"start\":{\"line\":0,\"character\":0},"
        "\"end\":{\"line\":6,\"character\":20}}}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(frame(initialize) + frame(open) +
                             frame(request(101, "hover", 4, 20)) +
                             frame(request(102, "signatureHelp", 4, 25)) +
                             frame(request(104, "hover", 2, 25)) +
                             frame(request(105, "signatureHelp", 2, 30)) + frame(inlay) +
                             frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto hover = responseFor(transcript, 101);
    const auto signature = responseFor(transcript, 102);
    const auto cloneHover = responseFor(transcript, 104);
    const auto cloneSignature = responseFor(transcript, 105);
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
    expect(cloneHover.find("fn clone() Sender<String>") != std::string::npos &&
               cloneHover.find("another owned sender handle") != std::string::npos,
           "sender clone hover explains shared channel ownership");
    expect(cloneSignature.find("fn clone() Sender<String>") != std::string::npos,
           "sender clone signature help exposes its owned result");
    expect(hints.find("value:") != std::string::npos,
           "channel send contributes a parameter inlay hint");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void selectExposesEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "task work($receiver Receiver<String>) Result<void, ChannelError> {\n"
        "    select {\n"
        "        const message = receiver.receive(): print(message)\n"
        "        timeout 5.seconds: return .Err(.Timeout)\n"
        "        else error: return .Err(error)\n"
        "    }\n"
        "    .Ok\n"
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
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(111, "hover", 2, 7)) +
        frame(request(112, "hover", 4, 11)) + frame(request(113, "hover", 3, 52)) +
        frame(request(114, "definition", 3, 52)) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto selectHover = responseFor(transcript, 111);
    const auto timeoutHover = responseFor(transcript, 112);
    const auto bindingHover = responseFor(transcript, 113);
    const auto definition = responseFor(transcript, 114);

    expect(status == 0, "select language server transcript exits cleanly");
    expect(errors.str().empty(), "select requests write no server errors");
    expect(selectHover.find("Ready branches use source order") != std::string::npos &&
               selectHover.find("else error") != std::string::npos,
           "select hover explains deterministic readiness and typed errors");
    expect(timeoutHover.find("monotonic duration") != std::string::npos &&
               timeoutHover.find("milliseconds") != std::string::npos,
           "timeout hover explains its clock and supported units");
    expect(bindingHover.find("message String") != std::string::npos,
           "select payload binding receives compiler-backed hover");
    expect(definition.find(sourceUri) != std::string::npos &&
               definition.find("\"character\":14") != std::string::npos,
           "select payload usage opens its branch binding");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void forLoopsExposeEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "fn main() i32 {\n"
        "    const values = [\"first\", \"second\"]\n"
        "    for index, value in values {\n"
        "        if index == 0 {\n"
        "            print(value)\n"
        "        }\n"
        "    }\n"
        "    0\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(115, "hover", 4, 12)) +
        frame(request(116, "hover", 5, 19)) +
        frame(request(117, "definition", 4, 12)) +
        frame(request(118, "definition", 5, 19)) +
        frame(request(119, "completion", 5, 12)) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto indexHover = responseFor(transcript, 115);
    const auto valueHover = responseFor(transcript, 116);
    const auto indexDefinition = responseFor(transcript, 117);
    const auto valueDefinition = responseFor(transcript, 118);
    const auto completion = responseFor(transcript, 119);

    expect(status == 0, "for language server transcript exits cleanly");
    expect(errors.str().empty(), "for requests write no server errors");
    expect(indexHover.find("index usize") != std::string::npos,
           "for index binding receives compiler-backed hover");
    expect(valueHover.find("value String") != std::string::npos,
           "for read binding hides its internal in hover");
    expect(indexDefinition.find("\"character\":8") != std::string::npos,
           "for index usage opens its binding");
    expect(valueDefinition.find("\"character\":15") != std::string::npos,
           "for value usage opens its binding");
    expect(completion.find("\"label\":\"index\"") != std::string::npos &&
               completion.find("\"label\":\"value\"") != std::string::npos,
           "for bindings participate in scoped completion");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void blockingImportsExposeEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "@blocking\n"
        "extern c fn nativeRead() i32 as sample_native_read\n"
        "task read() i32 {\n"
        "    const value = nativeRead()\n"
        "    value\n"
        "}\n"
        "fn main() i32 {\n"
        "    const pending = spawn read()\n"
        "    $pending.wait()\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(121, "completion", 1, 1)) +
        frame(request(122, "hover", 1, 5)) + frame(request(123, "hover", 2, 15)) +
        frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto completion = responseFor(transcript, 121);
    const auto attributeHover = responseFor(transcript, 122);
    const auto functionHover = responseFor(transcript, 123);

    expect(status == 0, "blocking import language server transcript exits cleanly");
    expect(errors.str().empty(), "blocking import requests write no server errors");
    expect(completion.find("\"label\":\"@blocking\"") != std::string::npos,
           "attribute completion offers the compiler-owned blocking marker");
    expect(attributeHover.find("bounded native worker pool") != std::string::npos &&
               attributeHover.find("standalone binding or discard") != std::string::npos,
           "blocking attribute hover explains suspension and placement");
    expect(functionHover.find("@blocking extern c fn nativeRead() i32") !=
               std::string::npos,
           "blocking import hover preserves its compiler-owned modifier");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void callbackImportsExposeEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "@callback(cancel = sample_native_cancel)\n"
        "extern c fn nativeRead(&result i32) i32 as sample_native_read\n"
        "task read() i32 {\n"
        "    var result = 0\n"
        "    const status = nativeRead(&result)\n"
        "    status + result\n"
        "}\n"
        "fn main() i32 {\n"
        "    const pending = spawn read()\n"
        "    $pending.wait()\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(131, "completion", 1, 1)) +
        frame(request(132, "hover", 1, 5)) + frame(request(133, "hover", 2, 15)) +
        frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto completion = responseFor(transcript, 131);
    const auto attributeHover = responseFor(transcript, 132);
    const auto functionHover = responseFor(transcript, 133);

    expect(status == 0, "callback import language server transcript exits cleanly");
    expect(errors.str().empty(), "callback import requests write no server errors");
    expect(completion.find("\"label\":\"@callback\"") != std::string::npos,
           "attribute completion offers the compiler-owned callback marker");
    expect(attributeHover.find("native completion reactor") != std::string::npos &&
               attributeHover.find("exactly once") != std::string::npos &&
               attributeHover.find("standalone binding or discard") != std::string::npos,
           "callback attribute hover explains ABI completion and placement");
    expect(functionHover.find("@callback extern c fn nativeRead(&result i32) i32") !=
               std::string::npos,
           "callback import hover preserves its compiler-owned modifier");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void functionValuesExposeTargetOwnershipModes() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "struct Value { count i32 }\n"
        "fn inspect(value String) bool { value == \"ready\" }\n"
        "fn mutate(&value Value) void { value.count = value.count + 1 }\n"
        "fn take($value String) i32 { if value == \"owned\" { return 1 } 0 }\n"
        "fn main() i32 {\n"
        "    const check = inspect\n"
        "    const editor = mutate\n"
        "    const consumer = take\n"
        "    const label = \"ready\"\n"
        "    var value = Value { count = 0 }\n"
        "    const payload = \"owned\"\n"
        "    const ready = check(label)\n"
        "    editor(&value)\n"
        "    const size = consumer($payload)\n"
        "    if ready { return size }\n"
        "    value.count\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, int line, int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/hover\",\"params\":{"
               "\"textDocument\":{\"uri\":\"" +
               sourceUri + "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(frame(initialize) + frame(open) + frame(request(141, 12, 20)) +
                             frame(request(142, 13, 6)) + frame(request(143, 14, 19)) +
                             frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto checkHover = responseFor(transcript, 141);
    const auto editorHover = responseFor(transcript, 142);
    const auto consumerHover = responseFor(transcript, 143);

    expect(status == 0, "function value hover transcript exits cleanly");
    expect(errors.str().empty(), "function value hover requests write no server errors");
    expect(checkHover.find("check fn(String) bool") != std::string::npos,
           "read function values use target type syntax");
    expect(editorHover.find("editor fn(&Value) void") != std::string::npos,
           "&function values use target type syntax");
    expect(consumerHover.find("consumer fn($String) i32") != std::string::npos,
           "transfer function values use target type syntax");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void pluginPackageExposesEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "import foundation.plugin\n"
        "fn main(args [String]) i32 {\n"
        "    if len(args) != 1 return 1\n"
        "    const loaded = plugin.LoadNative(args[0]) else error {\n"
        "        discard error\n"
        "        return 2\n"
        "    }\n"
        "    print(loaded.Name())\n"
        "    const sandbox = plugin.NewExecSandbox(args[0]) else error {\n"
        "        discard error\n"
        "        return 3\n"
        "    }\n"
        "    discard sandbox.IsRunning()\n"
        "    const factories = plugin.NewFactoryRegistry()\n"
        "    discard factories\n"
        "    0\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(141, "hover", 1, 12)) +
        frame(request(142, "hover", 4, 27)) + frame(request(143, "hover", 8, 19)) +
        frame(request(144, "definition", 4, 27)) +
        frame(request(145, "signatureHelp", 4, 42)) +
        frame(request(146, "hover", 9, 32)) +
        frame(request(147, "signatureHelp", 9, 47)) +
        frame(request(148, "hover", 13, 23)) +
        frame(request(149, "hover", 14, 38)) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto packageHover = responseFor(transcript, 141);
    const auto loaderHover = responseFor(transcript, 142);
    const auto nameHover = responseFor(transcript, 143);
    const auto definition = responseFor(transcript, 144);
    const auto signature = responseFor(transcript, 145);
    const auto sandboxHover = responseFor(transcript, 146);
    const auto sandboxSignature = responseFor(transcript, 147);
    const auto sandboxStateHover = responseFor(transcript, 148);
    const auto factoryHover = responseFor(transcript, 149);

    expect(status == 0, "plugin language server transcript exits cleanly");
    expect(errors.str().empty(), "plugin requests write no server errors");
    expect(packageHover.find("foundation.plugin") != std::string::npos,
           "plugin package import receives hover");
    expect(loaderHover.find("LoadNative") != std::string::npos &&
               loaderHover.find("Result") != std::string::npos &&
               loaderHover.find("Loads and validates") != std::string::npos,
           "native plugin loader receives compiler-backed documentation");
    expect(nameHover.find("fn Name(self) String") != std::string::npos,
           "native plugin methods receive typed hover");
    expect(definition.find("foundation/plugin/plugin.fdn") != std::string::npos,
           "native plugin loader navigates to framework source");
    expect(signature.find("fn LoadNative(path String)") != std::string::npos,
           "native plugin loader receives signature help");
    expect(sandboxHover.find("NewExecSandbox") != std::string::npos &&
               sandboxHover.find("Result<own ExecSandbox") != std::string::npos &&
               sandboxHover.find("without starting") != std::string::npos,
           "process sandbox constructor receives compiler-backed documentation");
    expect(sandboxSignature.find("fn NewExecSandbox(path String)") != std::string::npos,
           "process sandbox constructor receives signature help");
    expect(sandboxStateHover.find("fn IsRunning(self) bool") != std::string::npos,
           "process sandbox methods receive typed hover");
    expect(factoryHover.find("NewFactoryRegistry") != std::string::npos &&
               factoryHover.find("own FactoryRegistry") != std::string::npos,
           "plugin factory registry receives compiler-backed hover");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void supervisorPackageExposesEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "import foundation.worker\n"
        "task finish() void {\n"
        "    return\n"
        "}\n"
        "fn main() i32 {\n"
        "    const supervisor = worker.NewSupervisor()\n"
        "    supervisor.Start(spawn finish())\n"
        "    supervisor.Shutdown()\n"
        "    0\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(141, "hover", 1, 12)) +
        frame(request(142, "hover", 6, 32)) + frame(request(143, "hover", 7, 17)) +
        frame(request(144, "definition", 6, 32)) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto packageHover = responseFor(transcript, 141);
    const auto constructorHover = responseFor(transcript, 142);
    const auto startHover = responseFor(transcript, 143);
    const auto definition = responseFor(transcript, 144);

    expect(status == 0, "supervisor language server transcript exits cleanly");
    expect(errors.str().empty(), "supervisor requests write no server errors");
    expect(packageHover.find("foundation.worker") != std::string::npos,
           "framework package import receives hover");
    expect(constructorHover.find("NewSupervisor") != std::string::npos &&
               constructorHover.find("own Supervisor") != std::string::npos,
           "supervisor constructor receives compiler-backed hover");
    expect(startHover.find("Start") != std::string::npos &&
               startHover.find("Task<void>") != std::string::npos,
           "supervisor start hover exposes the detached task contract");
    expect(definition.find("foundation/worker/worker.fdn") != std::string::npos,
           "supervisor constructor navigates to framework source");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void parallelPoolExposesEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "import foundation.worker\n"
        "task calculate(value i32) void {\n"
        "    discard value\n"
        "}\n"
        "fn main() i32 {\n"
        "    const pool = worker.NewPool(2)\n"
        "    pool.Start(spawn calculate(42))\n"
        "    pool.Shutdown()\n"
        "    0\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(151, "hover", 6, 28)) +
        frame(request(152, "hover", 7, 9)) + frame(request(153, "definition", 6, 28)) +
        frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto constructorHover = responseFor(transcript, 151);
    const auto startHover = responseFor(transcript, 152);
    const auto definition = responseFor(transcript, 153);

    expect(status == 0, "parallel pool language server transcript exits cleanly");
    expect(errors.str().empty(), "parallel pool requests write no server errors");
    expect(constructorHover.find("NewPool") != std::string::npos &&
               constructorHover.find("own Pool") != std::string::npos &&
               constructorHover.find("greater than zero") != std::string::npos,
           "parallel pool constructor exposes its bounded worker contract");
    expect(startHover.find("Start") != std::string::npos &&
               startHover.find("directly spawned task") != std::string::npos,
           "parallel pool start hover exposes direct task transfer");
    expect(definition.find("foundation/worker/worker.fdn") != std::string::npos,
           "parallel pool constructor navigates to framework source");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void rawPointersExposeEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "\n"
        "fn inspect(pointer *const i32) bool {\n"
        "    isNull(pointer)\n"
        "}\n"
        "\n"
        "fn expose(source [u8]) void {\n"
        "    // SAFETY: the slice remains live while its pointer is inspected.\n"
        "    unsafe {\n"
        "        discard source.pointer\n"
        "    }\n"
        "}\n"
        "\n"
        "fn main() i32 {\n"
        "    // SAFETY: this block constructs and inspects only a null pointer.\n"
        "    unsafe {\n"
        "        const pointer = null<*const i32>()\n"
        "        if isNull(pointer) return 0\n"
        "    }\n"
        "    1\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(161, "completion", 15, 4)) +
        frame(request(162, "completion", 9, 23)) +
        frame(request(163, "hover", 15, 6)) + frame(request(164, "hover", 16, 25)) +
        frame(request(165, "hover", 17, 13)) +
        frame(request(166, "signatureHelp", 16, 41)) +
        frame(request(167, "signatureHelp", 17, 18)) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto globalCompletion = responseFor(transcript, 161);
    const auto memberCompletion = responseFor(transcript, 162);
    const auto unsafeHover = responseFor(transcript, 163);
    const auto nullHover = responseFor(transcript, 164);
    const auto isNullHover = responseFor(transcript, 165);
    const auto nullSignature = responseFor(transcript, 166);
    const auto isNullSignature = responseFor(transcript, 167);

    expect(status == 0, "raw pointer language server transcript exits cleanly");
    expect(errors.str().empty(), "raw pointer requests write no server errors");
    expect(globalCompletion.find("\"label\":\"unsafe\"") != std::string::npos &&
               globalCompletion.find("\"label\":\"null\"") != std::string::npos &&
               globalCompletion.find("\"label\":\"isNull\"") != std::string::npos,
           "raw pointer keywords and builtins receive completion");
    expect(memberCompletion.find("\"label\":\"pointer\"") != std::string::npos &&
               memberCompletion.find("*const u8") != std::string::npos,
           "read slice pointer completion preserves constness");
    expect(unsafeHover.find("SAFETY: proof") != std::string::npos,
           "unsafe hover explains the proof boundary");
    expect(nullHover.find("fn null&lt;P&gt;() P") != std::string::npos ||
               nullHover.find("fn null<P>() P") != std::string::npos,
           "null hover exposes its typed construction signature");
    expect(isNullHover.find("fn isNull(pointer P) bool") != std::string::npos,
           "isNull hover exposes its safe inspection signature");
    expect(nullSignature.find("fn null<P>() P") != std::string::npos,
           "generic null calls receive signature help");
    expect(isNullSignature.find("fn isNull(pointer P) bool") != std::string::npos &&
               isNullSignature.find("pointer P") != std::string::npos,
           "isNull calls receive parameter signature help");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void compilerBuiltinsExposeEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "fn main() i32 {\n"
        "    print(\"ready\")\n"
        "    const length = len(\"ready\")\n"
        "    if length == 0 { panic(\"unreachable\") }\n"
        "    const Channel { sender receiver } = channel<String>(1)\n"
        "    discard sender\n"
        "    discard receiver\n"
        "    0\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(180, "hover", 1, 6)) +
        frame(request(181, "hover", 2, 21)) + frame(request(182, "hover", 3, 23)) +
        frame(request(183, "hover", 4, 43)) +
        frame(request(184, "signatureHelp", 1, 16)) +
        frame(request(185, "signatureHelp", 2, 29)) +
        frame(request(186, "signatureHelp", 3, 39)) +
        frame(request(187, "signatureHelp", 4, 57)) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();

    expect(status == 0, "builtin language server transcript exits cleanly");
    expect(errors.str().empty(), "builtin requests write no server errors");
    expect(responseFor(transcript, 180).find("fn print(value String) void") !=
               std::string::npos,
           "print hover exposes its typed signature");
    expect(responseFor(transcript, 181).find("usize") != std::string::npos,
           "len hover exposes its machine-sized result");
    expect(responseFor(transcript, 182).find("complete Foundation source trace") !=
               std::string::npos,
           "panic hover explains fatal source traces");
    expect(responseFor(transcript, 183).find("Channel&lt;T&gt;") != std::string::npos ||
               responseFor(transcript, 183).find("Channel<T>") != std::string::npos,
           "channel hover exposes its generic endpoint result");
    expect(responseFor(transcript, 184).find("value String") != std::string::npos,
           "print calls receive parameter signature help");
    expect(responseFor(transcript, 185).find("String | [N]T | [T]") != std::string::npos,
           "len calls receive supported sequence signature help");
    expect(responseFor(transcript, 186).find("message String") != std::string::npos,
           "panic calls receive parameter signature help");
    expect(responseFor(transcript, 187).find("capacity u64") != std::string::npos,
           "channel calls receive capacity signature help");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void preludeUUIDExposesEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "fn parseUUID(value String) Result<UUID, UUIDError> {\n"
        "    UUID.Parse(value)\n"
        "}\n"
        "fn main() i32 {\n"
        "    const generated = UUID.NewV7()\n"
        "    print(generated.String())\n"
        "    0\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(188, "hover", 1, 36)) +
        frame(request(189, "completion", 5, 27)) +
        frame(request(190, "signatureHelp", 2, 16)) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();

    expect(status == 0, "UUID language server transcript exits cleanly");
    expect(errors.str().empty(), "UUID requests write no server errors");
    expect(responseFor(transcript, 188).find("canonical 128-bit UUID") !=
               std::string::npos,
           "UUID hover exposes its standard value contract");
    expect(responseFor(transcript, 189).find("NewV4") != std::string::npos &&
               responseFor(transcript, 189).find("NewV7") != std::string::npos,
           "UUID associated completion exposes both generators");
    expect(responseFor(transcript, 190).find("value String") != std::string::npos,
           "UUID Parse calls receive parameter signature help");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void parseBoolExposesEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "import std.parse\n"
        "fn main() i32 {\n"
        "    const enabled = parse.Bool(\"yes\") else error {\n"
        "        discard error\n"
        "        return 1\n"
        "    }\n"
        "    0 if enabled else 1\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(196, "hover", 3, 28)) +
        frame(request(197, "signatureHelp", 3, 34)) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto hover = responseFor(transcript, 196);
    const auto signature = responseFor(transcript, 197);

    expect(status == 0, "parse Bool language server transcript exits cleanly");
    expect(errors.str().empty(), "parse Bool requests write no server errors");
    expect(hover.find("Unicode whitespace") != std::string::npos,
           "parse Bool hover includes its standard library contract");
    expect(signature.find("fn Bool(value String) Result<bool, BooleanError>") !=
               std::string::npos,
           "parse Bool calls receive compiler-backed signature help");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void emptyTestsExposeResolvedMeaning() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "fn main() i32 {\n"
        "    const name = \"\"\n"
        "    if !name return 0\n"
        "    1\n"
        "}\n";
    writeFile(source, contents);
    const auto uri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" + uri +
        "\",\"languageId\":\"foundation\",\"version\":1,\"text\":\"" +
        jsonEscape(contents) + "\"}}}";
    const auto hover =
        "{\"jsonrpc\":\"2.0\",\"id\":201,\"method\":\"textDocument/hover\","
        "\"params\":{\"textDocument\":{\"uri\":\"" + uri +
        "\"},\"position\":{\"line\":3,\"character\":7}}}";
    const auto hints =
        "{\"jsonrpc\":\"2.0\",\"id\":202,\"method\":\"textDocument/inlayHint\","
        "\"params\":{\"textDocument\":{\"uri\":\"" + uri +
        "\"},\"range\":{\"start\":{\"line\":0,\"character\":0},"
        "\"end\":{\"line\":6,\"character\":0}}}}";
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit =
        "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(frame(initialize) + frame(open) + frame(hover) + frame(hints) +
                             frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto hoverResponse = responseFor(transcript, 201);
    const auto hintResponse = responseFor(transcript, 202);

    expect(status == 0, "empty-test language server transcript exits cleanly");
    expect(errors.str().empty(), "empty-test requests write no server errors");
    expect(hoverResponse.find("!String bool") != std::string::npos &&
               hoverResponse.find("Tests whether the value is empty") != std::string::npos,
           "empty-test hover explains its resolved typed meaning");
    expect(hintResponse.find("\"label\":\"is empty\"") != std::string::npos,
           "empty-test inlay hint makes the operator meaning visible");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void servicesAndActionsExposeEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "\n"
        "service CounterService {\n"
        "    value i32\n"
        "\n"
        "    fn New(initial i32) CounterService {\n"
        "        CounterService { value = initial }\n"
        "    }\n"
        "\n"
        "    action Add(&self, amount i32) i32 {\n"
        "        self.value = self.value + amount\n"
        "        self.value\n"
        "    }\n"
        "}\n"
        "\n"
        "fn main() i32 {\n"
        "    var counter = CounterService.New(40)\n"
        "    counter.Add(2) - 42\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(171, "hover", 2, 10)) +
        frame(request(172, "hover", 9, 12)) +
        frame(request(173, "signatureHelp", 17, 17)) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto serviceHover = responseFor(transcript, 171);
    const auto actionHover = responseFor(transcript, 172);
    const auto actionSignature = responseFor(transcript, 173);

    expect(status == 0, "service language server transcript exits cleanly");
    expect(errors.str().empty(), "service requests write no server errors");
    expect(serviceHover.find("service CounterService") != std::string::npos,
           "service hover preserves the declaration kind");
    expect(actionHover.find("action Add(&amp;self, amount i32) i32") != std::string::npos ||
               actionHover.find("action Add(&self, amount i32) i32") != std::string::npos,
           "action hover preserves its receiver and handler signature");
    expect(actionSignature.find("action Add(&self, amount i32) i32") != std::string::npos &&
               actionSignature.find("amount i32") != std::string::npos,
           "action calls receive compiler-backed signature help");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void derivedValidationExposesEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "\n"
        "import foundation.validation\n"
        "\n"
        "@validation.Validatable()\n"
        "struct Profile {\n"
        "    @validation.Required()\n"
        "    Name String\n"
        "\n"
        "    @validation.Pattern(\"^[A-Z][a-z]+$\")\n"
        "    Code String\n"
        "}\n"
        "\n"
        "fn inspect(profile Profile) i32 {\n"
        "    const errors = profile.Validate()\n"
        "    errors.Len()\n"
        "}\n"
        "\n"
        "fn main() i32 {\n"
        "    inspect(Profile { Name = \"Ada\" Code = \"Ada\" })\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(211, "hover", 14, 29)) +
        frame(request(212, "signatureHelp", 14, 36)) +
        frame(request(213, "definition", 14, 29)) +
        frame(request(214, "hover", 9, 20)) +
        frame(request(215, "signatureHelp", 9, 32)) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto hover = responseFor(transcript, 211);
    const auto signature = responseFor(transcript, 212);
    const auto definition = responseFor(transcript, 213);
    const auto patternHover = responseFor(transcript, 214);
    const auto patternSignature = responseFor(transcript, 215);

    expect(status == 0, "derived validation language server transcript exits cleanly");
    expect(errors.str().empty(), "derived validation requests write no server errors");
    expect(hover.find("fn Validate(self) own Errors") != std::string::npos,
           "derived validation exposes compiler-backed hover");
    expect(signature.find("fn Validate(self) own Errors") != std::string::npos,
           "derived validation exposes compiler-backed signature help");
    expect(definition.find(sourceUri) != std::string::npos &&
               definition.find(".foundation.generated.fdn") == std::string::npos,
           "derived validation definition returns to its source struct");
    expect(patternHover.find("attribute Pattern(expression String)") != std::string::npos &&
               patternHover.find("portable safe pattern") != std::string::npos,
           "validation pattern hover exposes its signature and documentation");
    expect(patternSignature.find("attribute Pattern(expression String)") !=
               std::string::npos,
           "validation pattern arguments receive signature help");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void stateMachinesExposeEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "\n"
        "state_machine Order {\n"
        "    state Draft\n"
        "    state Submitted\n"
        "\n"
        "    on Submit from Draft to Submitted\n"
        "}\n"
        "\n"
        "fn main() i32 {\n"
        "    var order Order = .Draft\n"
        "    order.Submit() else transition {\n"
        "        discard transition\n"
        "        return 1\n"
        "    }\n"
        "    0\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(181, "hover", 2, 15)) +
        frame(request(182, "hover", 6, 8)) +
        frame(request(183, "completion", 11, 10)) +
        frame(request(184, "signatureHelp", 11, 17)) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto machineHover = responseFor(transcript, 181);
    const auto transitionHover = responseFor(transcript, 182);
    const auto completion = responseFor(transcript, 183);
    const auto signature = responseFor(transcript, 184);

    expect(status == 0, "state machine language server transcript exits cleanly");
    expect(errors.str().empty(), "state machine requests write no server errors");
    expect(machineHover.find("state_machine Order") != std::string::npos,
           "state machine hover preserves its declaration kind");
    expect(transitionHover.find("fn Submit(&amp;self) Result&lt;void, OrderTransitionError&gt;") !=
                   std::string::npos ||
               transitionHover.find("fn Submit(&self) Result<void, OrderTransitionError>") !=
                   std::string::npos,
           "generated transition hover exposes its typed result");
    expect(completion.find("\"label\":\"Submit\"") != std::string::npos,
           "state machine values complete generated transitions");
    expect(signature.find("fn Submit(&self) Result<void, OrderTransitionError>") !=
               std::string::npos,
           "state transition calls receive compiler-backed signature help");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void workflowsExposeEditorDetails() {
    const auto root = temporaryRoot();
    const auto source = root / "main.fdn";
    const std::string contents =
        "package sample\n"
        "\n"
        "fn double(value i32) Result<i32, bool> {\n"
        "    .Ok(value * 2)\n"
        "}\n"
        "\n"
        "pipeline Transform(input i32) Result<i32, bool> {\n"
        "    step double using double\n"
        "}\n"
        "\n"
        "fn reserve(value i32) Result<void, bool> { .Ok }\n"
        "fn release(value i32) Result<void, bool> { .Ok }\n"
        "fn confirm(value i32) Result<i32, bool> { .Ok(value) }\n"
        "\n"
        "saga Checkout(input i32) Result<i32, bool> {\n"
        "    step reserve using reserve\n"
        "        compensate release\n"
        "    step confirm using confirm\n"
        "}\n"
        "\n"
        "fn main() i32 {\n"
        "    const value = Transform(21) else error {\n"
        "        discard error\n"
        "        return 1\n"
        "    }\n"
        "    value - 42\n"
        "}\n";
    writeFile(source, contents);
    const auto sourceUri = fileUri(source);
    const auto initialize =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"" +
        fileUri(root) + "\"}}";
    const auto open =
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
        "\"textDocument\":{\"uri\":\"" +
        sourceUri + "\",\"version\":1,\"text\":\"" + jsonEscape(contents) + "\"}}}";
    const auto request = [&sourceUri](int id, std::string_view method, int line,
                                      int character) {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
               ",\"method\":\"textDocument/" + std::string(method) +
               "\",\"params\":{\"textDocument\":{\"uri\":\"" + sourceUri +
               "\"},\"position\":{\"line\":" + std::to_string(line) +
               ",\"character\":" + std::to_string(character) + "}}}";
    };
    const auto shutdown =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"shutdown\",\"params\":null}";
    const auto exit = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
    std::istringstream input(
        frame(initialize) + frame(open) + frame(request(191, "hover", 6, 12)) +
        frame(request(192, "hover", 7, 25)) + frame(request(193, "hover", 14, 8)) +
        frame(request(194, "definition", 16, 22)) +
        frame(request(195, "signatureHelp", 21, 29)) + frame(shutdown) + frame(exit));
    std::ostringstream output;
    std::ostringstream errors;
    const auto status = foundation::runLanguageServer(input, output, errors);
    const auto transcript = output.str();
    const auto pipelineHover = responseFor(transcript, 191);
    const auto stepHover = responseFor(transcript, 192);
    const auto sagaHover = responseFor(transcript, 193);
    const auto compensationDefinition = responseFor(transcript, 194);
    const auto signature = responseFor(transcript, 195);

    expect(status == 0, "workflow language server transcript exits cleanly");
    expect(errors.str().empty(), "workflow requests write no server errors");
    expect(pipelineHover.find("pipeline Transform(input i32) Result&lt;i32, bool&gt;") !=
                   std::string::npos ||
               pipelineHover.find("pipeline Transform(input i32) Result<i32, bool>") !=
                   std::string::npos,
           "pipeline hover preserves its declaration kind and callable type");
    expect(stepHover.find("fn double(value i32) Result&lt;i32, bool&gt;") != std::string::npos ||
               stepHover.find("fn double(value i32) Result<i32, bool>") != std::string::npos,
           "workflow step references resolve to their functions");
    expect(sagaHover.find("saga Checkout(input i32) Result&lt;i32, CheckoutFailure&gt;") !=
                   std::string::npos ||
               sagaHover.find("saga Checkout(input i32) Result<i32, CheckoutFailure>") !=
                   std::string::npos,
           "saga hover exposes its generated structured failure type");
    expect(compensationDefinition.find(sourceUri) != std::string::npos,
           "saga compensation references navigate to their definitions");
    expect(signature.find("pipeline Transform(input i32) Result<i32, bool>") !=
               std::string::npos,
           "pipeline calls receive compiler-backed signature help");

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

} // namespace

int main() {
    diagnosticsUseUnsavedCompilerInput();
    namedArgumentsSelectDeclaredSignatureParameter();
    namedEnumPayloadsExposeEditorDetails();
    testDeclarationsExposeEditorDetails();
    malformedJsonGetsProtocolError();
    lifecycleRejectsInvalidRequestOrder();
    invalidJsonRpcEnvelopeIsRejected();
    unopenedLibraryDoesNotRequireMain();
    importedPackagesExposeHoverAndAllDefinitions();
    completionsRespectScopesAndMemberAccess();
    numericConversionsExposeEditorDetails();
    fieldDefaultsExposeEditorDetails();
    foldingAndSelectionRangesFollowCompilerTokens();
    semanticNavigationSeparatesHomonyms();
    formatsUnsavedDocumentsAndRanges();
    offersCompilerBackedDiscardQuickFixes();
    contractImplementationsIncludeInheritedAndDelegatedTypes();
    callHierarchySeparatesHomonymousMethods();
    lockedPackageWorkspaceLoadsCachedDependencies();
    workspaceFoldersAreIndependentAndDynamic();
    diagnosticsStayScopedToTheirWorkspace();
    semanticIntelliSenseKeepsDocumentationTextual();
    distributedMethodsExposeDocumentationAndParameters();
    channelOperationsExposeEditorDetails();
    selectExposesEditorDetails();
    forLoopsExposeEditorDetails();
    compilerBuiltinsExposeEditorDetails();
    parseBoolExposesEditorDetails();
    preludeUUIDExposesEditorDetails();
    emptyTestsExposeResolvedMeaning();
    blockingImportsExposeEditorDetails();
    callbackImportsExposeEditorDetails();
    functionValuesExposeTargetOwnershipModes();
    pluginPackageExposesEditorDetails();
    supervisorPackageExposesEditorDetails();
    parallelPoolExposesEditorDetails();
    rawPointersExposeEditorDetails();
    servicesAndActionsExposeEditorDetails();
    derivedValidationExposesEditorDetails();
    stateMachinesExposeEditorDetails();
    workflowsExposeEditorDetails();

    if (failures != 0) {
        std::cerr << failures << " language server assertions failed\n";
        return 1;
    }
    std::cout << "language server tests passed\n";
    return 0;
}
