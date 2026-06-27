"use strict";

const staticCompletions = [
    { label: "package", kind: "Keyword" },
    { label: "import", kind: "Keyword" },
    { label: "as", kind: "Keyword" },
    { label: "attribute", kind: "Keyword", detail: "Declare typed compile-time metadata" },
    { label: "targets(...)", kind: "Keyword", insertText: "targets(${1|fn,struct,enum,contract,method,field,variant,parameter|})" },
    { label: "repeatable", kind: "Keyword", detail: "Allow repeated applications of an attribute" },
    { label: "extern", kind: "Keyword", detail: "Declare a C ABI import or export" },
    { label: "c", kind: "Value", detail: "C application binary interface" },
    {
        label: "@target(...)",
        kind: "Keyword",
        detail: "Select a declaration for one compilation target",
        insertText: "@target(${1|linux,macos,windows|})"
    },
    {
        label: "@blocking",
        kind: "Keyword",
        detail: "Run a bodyless C ABI import on the blocking executor",
        insertText: "@blocking"
    },
    {
        label: "@callback",
        kind: "Keyword",
        detail: "Suspend a task on a native callback operation",
        insertText: "@callback"
    },
    { label: "struct", kind: "Keyword" },
    { label: "enum", kind: "Keyword" },
    { label: "contract", kind: "Keyword" },
    { label: "implements", kind: "Keyword" },
    { label: "extends", kind: "Keyword", detail: "Inherit contract requirements" },
    { label: "delegate", kind: "Keyword", detail: "Delegate a contract through a field" },
    { label: "methods", kind: "Keyword", detail: "Add methods to a package-owned type" },
    { label: "service", kind: "Keyword", detail: "Declare a Foundation service" },
    { label: "action", kind: "Keyword", detail: "Declare a typed action handler" },
    { label: "state_machine", kind: "Keyword", detail: "Declare a typed state machine" },
    { label: "pipeline", kind: "Keyword", detail: "Declare a typed pipeline" },
    { label: "saga", kind: "Keyword", detail: "Declare a compensated workflow" },
    { label: "task", kind: "Keyword", detail: "Declare a suspendable function" },
    { label: "spawn", kind: "Keyword", detail: "Start a task on the active scheduler" },
    { label: "select", kind: "Keyword", detail: "Wait on typed channel operations" },
    { label: "timeout", kind: "Keyword", detail: "Use a monotonic select deadline" },
    { label: "test", kind: "Keyword", detail: "Declare an executable test" },
    { label: "unsafe", kind: "Keyword", detail: "Bound raw pointer operations" },
    { label: "fn", kind: "Keyword" },
    { label: "const", kind: "Keyword", detail: "Declare an immutable binding" },
    { label: "let", kind: "Keyword", detail: "Bootstrap immutable binding" },
    { label: "var", kind: "Keyword" },
    { label: "return", kind: "Keyword" },
    { label: "discard", kind: "Keyword" },
    { label: "if", kind: "Keyword" },
    { label: "else", kind: "Keyword" },
    { label: "while", kind: "Keyword" },
    { label: "for", kind: "Keyword" },
    { label: "in", kind: "Keyword" },
    { label: "break", kind: "Keyword" },
    { label: "continue", kind: "Keyword" },
    { label: "match", kind: "Keyword" },
    { label: "capture", kind: "Keyword", detail: "Declare explicit closure captures" },
    { label: "replace", kind: "Keyword", detail: "Replace a mutable place and return its previous value" },
    { label: "with", kind: "Keyword", detail: "Introduce a replacement value" },
    { label: "new", kind: "Keyword", detail: "Construct a fresh owned value" },
    { label: "&value", kind: "Keyword", detail: "Grant an exclusive edit loan" },
    { label: "$value", kind: "Keyword", detail: "Transfer ownership" },
    { label: "own", kind: "Keyword", detail: "Create or declare an exclusive owner" },
    { label: "view", kind: "Keyword", detail: "Create or declare a shared borrow" },
    { label: "edit", kind: "Keyword", detail: "Create or declare an exclusive mutable borrow" },
    { label: "true", kind: "Constant" },
    { label: "false", kind: "Constant" },
    { label: "i8", kind: "TypeParameter" },
    { label: "i16", kind: "TypeParameter" },
    { label: "i32", kind: "TypeParameter" },
    { label: "i64", kind: "TypeParameter" },
    { label: "u8", kind: "TypeParameter" },
    { label: "u16", kind: "TypeParameter" },
    { label: "u32", kind: "TypeParameter" },
    { label: "u64", kind: "TypeParameter" },
    { label: "f32", kind: "TypeParameter" },
    { label: "f64", kind: "TypeParameter" },
    { label: "isize", kind: "TypeParameter" },
    { label: "usize", kind: "TypeParameter" },
    { label: "bool", kind: "TypeParameter" },
    { label: "String", kind: "TypeParameter" },
    { label: "UUID", kind: "TypeParameter", detail: "standard RFC 9562 UUID value" },
    { label: "void", kind: "TypeParameter" },
    { label: "never", kind: "TypeParameter" },
    { label: "Option", kind: "TypeParameter", detail: "primitive Option<T>" },
    { label: "Result", kind: "TypeParameter", detail: "primitive Result<T, E>" },
    { label: "ChannelError", kind: "Enum", detail: "typed channel operation failure" },
    { label: "Task", kind: "TypeParameter", detail: "owned concurrent result handle" },
    { label: "Channel", kind: "TypeParameter", detail: "owned channel endpoint pair" },
    { label: "Sender", kind: "TypeParameter", detail: "owned channel send endpoint" },
    { label: "Receiver", kind: "TypeParameter", detail: "owned channel receive endpoint" },
    { label: "TcpConnection", kind: "Struct", detail: "owned TCP connection" },
    { label: "TcpReader", kind: "Struct", detail: "owned TCP read half" },
    { label: "TcpWriter", kind: "Struct", detail: "owned TCP write half" },
    { label: "StreamPair", kind: "Struct", detail: "owned TCP read and write halves" },
    { label: "std.platform", kind: "Module", detail: "Compilation target information" },
    { label: "std.env", kind: "Module", detail: "Read-only process environment" },
    { label: "std.text", kind: "Module", detail: "UTF-8 String inspection" },
    { label: "std.path", kind: "Module", detail: "Portable path operations" },
    { label: "std.parse", kind: "Module", detail: "Primitive value parsing" },
    { label: "std.fs", kind: "Module", detail: "Read-only filesystem operations" },
    { label: "std.net", kind: "Module", detail: "Portable TCP client operations" },
    { label: "std.format", kind: "Module", detail: "Primitive value formatting" },
    { label: "std.json", kind: "Module", detail: "Owned JSON values and parsing" },
    { label: "std.time", kind: "Module", detail: "Unix time values" },
    { label: "std.concurrent", kind: "Module", detail: "Structured cancellation values" },
    { label: "foundation.worker", kind: "Module", detail: "Supervised application tasks" },
    { label: "worker.Supervisor", kind: "Struct", detail: "owned supervised task lifetime" },
    { label: "worker.Pool", kind: "Struct", detail: "bounded parallel task executor" },
    {
        label: "platform.Current",
        kind: "Function",
        detail: "fn Current() platform.Kind",
        insertText: "platform.Current()"
    },
    {
        label: "platform.Name",
        kind: "Function",
        detail: "fn Name(platform platform.Kind) String",
        insertText: "platform.Name(${1:platform})"
    },
    {
        label: "env.Get",
        kind: "Function",
        detail: "fn Get(name view String) Result<Option<String>, env.Error>",
        insertText: "env.Get(view ${1:name})"
    },
    {
        label: "env.Home",
        kind: "Function",
        detail: "fn Home() Result<Option<String>, env.Error>",
        insertText: "env.Home()"
    },
    {
        label: "text.ByteLen",
        kind: "Function",
        detail: "fn ByteLen(value view String) u64",
        insertText: "text.ByteLen(view ${1:value})"
    },
    {
        label: "text.Contains",
        kind: "Function",
        detail: "fn Contains(value view String, part view String) bool",
        insertText: "text.Contains(view ${1:value}, view ${2:part})"
    },
    {
        label: "text.NewBuilder",
        kind: "Function",
        detail: "fn NewBuilder() own text.Builder",
        insertText: "text.NewBuilder()"
    },
    {
        label: "path.Join",
        kind: "Function",
        detail: "fn Join(left view String, right view String) String",
        insertText: "path.Join(view ${1:left}, view ${2:right})"
    },
    {
        label: "parse.U64",
        kind: "Function",
        detail: "fn U64(value view String) Result<u64, parse.IntegerError>",
        insertText: "parse.U64(view ${1:value})"
    },
    {
        label: "fs.OpenLines",
        kind: "Function",
        detail: "fn OpenLines(path view String) Result<own fs.LineReader, fs.Error>",
        insertText: "fs.OpenLines(view ${1:path})"
    },
    {
        label: "fs.ReadText",
        kind: "Function",
        detail: "task ReadText(path String) Result<String, fs.Error>",
        insertText: "fs.ReadText(${1:path})"
    },
    {
        label: "fs.ReadTextLimited",
        kind: "Function",
        detail: "task ReadTextLimited(path String, limit u64) Result<String, fs.Error>",
        insertText: "fs.ReadTextLimited(${1:path}, ${2:limit})"
    },
    {
        label: "fs.OpenDir",
        kind: "Function",
        detail: "fn OpenDir(path view String) Result<own fs.DirReader, fs.Error>",
        insertText: "fs.OpenDir(view ${1:path})"
    },
    {
        label: "fs.Size",
        kind: "Function",
        detail: "fn Size(path view String) Result<u64, fs.Error>",
        insertText: "fs.Size(view ${1:path})"
    },
    {
        label: "fs.Modified",
        kind: "Function",
        detail: "fn Modified(path view String) Result<u64, fs.Error>",
        insertText: "fs.Modified(view ${1:path})"
    },
    {
        label: "fs.LineReader.Next",
        kind: "Method",
        detail: "fn Next(edit) Result<Option<String>, fs.Error>",
        insertText: "Next()"
    },
    {
        label: "fs.LineReader.NextLimited",
        kind: "Method",
        detail: "fn NextLimited(edit, limit u64) Result<Option<String>, fs.Error>",
        insertText: "NextLimited(${1:limit})"
    },
    {
        label: "net.Connect",
        kind: "Function",
        detail: "task Connect(host String, port u64) Result<own net.TcpConnection, net.Error>",
        insertText: "net.Connect(${1:host}, ${2:port})"
    },
    {
        label: "net.TcpConnection.Split",
        kind: "Method",
        detail: "fn Split(own) Result<net.StreamPair, net.Error>",
        insertText: "Split()"
    },
    {
        label: "net.ReadLine",
        kind: "Function",
        detail: "task ReadLine(reader own net.TcpReader) net.ReadLineOutcome",
        insertText: "net.ReadLine(${1:reader})"
    },
    {
        label: "net.ReadLineLimited",
        kind: "Function",
        detail: "task ReadLineLimited(reader own net.TcpReader, limit u64) net.ReadLineOutcome",
        insertText: "net.ReadLineLimited(${1:reader}, ${2:limit})"
    },
    {
        label: "net.WriteAll",
        kind: "Function",
        detail: "task WriteAll(writer own net.TcpWriter, text String) net.WriteOutcome",
        insertText: "net.WriteAll(${1:writer}, ${2:text})"
    },
    {
        label: "format.I32",
        kind: "Function",
        detail: "fn I32(value i32) String",
        insertText: "format.I32(${1:value})"
    },
    {
        label: "format.U64",
        kind: "Function",
        detail: "fn U64(value u64) String",
        insertText: "format.U64(${1:value})"
    },
    {
        label: "json.Parse",
        kind: "Function",
        detail: "fn Parse(source view String) Result<json.Value, json.Error>",
        insertText: "json.Parse(view ${1:source})"
    },
    {
        label: "time.Now",
        kind: "Function",
        detail: "fn Now() time.Instant",
        insertText: "time.Now()"
    },
    {
        label: "time.FromUnix",
        kind: "Function",
        detail: "fn FromUnix(seconds u64) time.Instant",
        insertText: "time.FromUnix(${1:seconds})"
    },
    {
        label: "time.Instant.FormatUtc",
        kind: "Method",
        detail: "fn FormatUtc(view) Result<String, time.Error>",
        insertText: "FormatUtc()"
    },
    {
        label: "concurrent.NewCancellationSource",
        kind: "Function",
        detail: "fn NewCancellationSource() concurrent.CancellationSource",
        insertText: "concurrent.NewCancellationSource()"
    },
    {
        label: "concurrent.CancellationSource.Token",
        kind: "Method",
        detail: "fn Token(view) concurrent.Cancellation",
        insertText: "Token()"
    },
    {
        label: "concurrent.CancellationSource.Cancel",
        kind: "Method",
        detail: "fn Cancel(view) void",
        insertText: "Cancel()"
    },
    {
        label: "concurrent.Cancellation.IsRequested",
        kind: "Method",
        detail: "fn IsRequested(view) bool",
        insertText: "IsRequested()"
    },
    {
        label: "worker.NewSupervisor",
        kind: "Function",
        detail: "fn NewSupervisor() own worker.Supervisor",
        insertText: "worker.NewSupervisor()"
    },
    {
        label: "worker.Supervisor.Start",
        kind: "Method",
        detail: "fn Start(view, pending Task<void>) void",
        insertText: "Start(${1:pending})"
    },
    {
        label: "worker.Supervisor.Shutdown",
        kind: "Method",
        detail: "fn Shutdown(own) void",
        insertText: "Shutdown()"
    },
    {
        label: "worker.Supervisor.Cancel",
        kind: "Method",
        detail: "fn Cancel(own) void",
        insertText: "Cancel()"
    },
    {
        label: "worker.NewPool",
        kind: "Function",
        detail: "fn NewPool(workers u64) own worker.Pool",
        insertText: "worker.NewPool(${1:workers})"
    },
    {
        label: "worker.Pool.Start",
        kind: "Method",
        detail: "fn Start(view, pending Task<void>) void",
        insertText: "Start(spawn ${1:work}(${2}))"
    },
    {
        label: "worker.Pool.Shutdown",
        kind: "Method",
        detail: "fn Shutdown(own) void",
        insertText: "Shutdown()"
    },
    {
        label: "worker.Pool.Cancel",
        kind: "Method",
        detail: "fn Cancel(own) void",
        insertText: "Cancel()"
    },
    {
        label: "fn(...) R",
        kind: "TypeParameter",
        detail: "function value type",
        insertText: "fn(${1:parameters}) ${2:R}"
    },
    {
        label: "[N]T",
        kind: "TypeParameter",
        detail: "fixed array type",
        insertText: "[${1:N}]${2:T}"
    },
    {
        label: "[T]",
        kind: "TypeParameter",
        detail: "read-only sequence parameter",
        insertText: "[${1:T}]"
    },
    {
        label: "&[T]",
        kind: "TypeParameter",
        detail: "editable sequence parameter",
        insertText: "&[${1:T}]"
    },
    {
        label: "view [T]",
        kind: "TypeParameter",
        detail: "shared slice type",
        insertText: "view [${1:T}]"
    },
    {
        label: "edit [T]",
        kind: "TypeParameter",
        detail: "mutable slice type",
        insertText: "edit [${1:T}]"
    },
    { label: ".None", kind: "EnumMember", insertText: ".None" },
    { label: ".Some", kind: "EnumMember", insertText: ".Some(${1:value})" },
    { label: ".Ok", kind: "EnumMember", insertText: ".Ok(${1:value})" },
    { label: ".Err", kind: "EnumMember", insertText: ".Err(${1:error})" },
    { label: "Option.None", kind: "EnumMember", insertText: "Option<${1:T}>.None" },
    { label: "Option.Some", kind: "EnumMember", insertText: "Option<${1:T}>.Some(${2:value})" },
    { label: "Result.Ok", kind: "EnumMember", insertText: "Result<${1:T}, ${2:E}>.Ok(${3:value})" },
    { label: "Result.Err", kind: "EnumMember", insertText: "Result<${1:T}, ${2:E}>.Err(${3:error})" },
    { label: "ChannelError.Closed", kind: "EnumMember", insertText: "ChannelError.Closed" },
    { label: "ChannelError.Cancelled", kind: "EnumMember", insertText: "ChannelError.Cancelled" },
    { label: "ChannelError.Timeout", kind: "EnumMember", insertText: "ChannelError.Timeout" },
    {
        label: "channel",
        kind: "Function",
        detail: "builtin fn channel<T>(capacity u64) Channel<T>",
        insertText: "channel<${1:T}>(${2:capacity})"
    },
    {
        label: "send",
        kind: "Method",
        detail: "fn send(value T) Result<void, ChannelError>",
        insertText: "send(${1:value})"
    },
    {
        label: "receive",
        kind: "Method",
        detail: "fn receive() Result<T, ChannelError>",
        insertText: "receive()"
    },
    {
        label: "len",
        kind: "Function",
        detail: "builtin fn len(value String | array | slice) u64",
        insertText: "len(${1:value})"
    },
    {
        label: "print",
        kind: "Function",
        detail: "builtin fn print(value String) void",
        insertText: "print(${1:value})"
    },
    {
        label: "panic",
        kind: "Function",
        detail: "builtin fn panic(value String) void",
        insertText: "panic(${1:message})"
    }
];

function maskTrivia(source) {
    let masked = "";
    let offset = 0;

    while (offset < source.length) {
        if (source[offset] === "/" && source[offset + 1] === "/") {
            while (offset < source.length && source[offset] !== "\n") {
                masked += " ";
                offset += 1;
            }
            continue;
        }

        if (source[offset] === "/" && source[offset + 1] === "*") {
            let depth = 0;
            do {
                if (source[offset] === "/" && source[offset + 1] === "*") {
                    masked += "  ";
                    offset += 2;
                    depth += 1;
                } else if (source[offset] === "*" && source[offset + 1] === "/") {
                    masked += "  ";
                    offset += 2;
                    depth -= 1;
                } else {
                    masked += source[offset] === "\n" ? "\n" : " ";
                    offset += 1;
                }
            } while (offset < source.length && depth !== 0);
            continue;
        }

        if (source[offset] === "\"") {
            masked += " ";
            offset += 1;
            while (offset < source.length) {
                const current = source[offset];
                masked += current === "\n" ? "\n" : " ";
                offset += 1;
                if (current === "\\" && offset < source.length) {
                    masked += source[offset] === "\n" ? "\n" : " ";
                    offset += 1;
                    continue;
                }
                if (current === "\"") {
                    break;
                }
            }
            continue;
        }

        masked += source[offset];
        offset += 1;
    }

    return masked;
}

function maskAttributeApplications(source) {
    const masked = source.split("");
    let offset = 0;
    while (offset < source.length) {
        if (source[offset] !== "@" || !/[A-Za-z_]/.test(source[offset + 1] || "")) {
            offset += 1;
            continue;
        }
        const start = offset;
        offset += 1;
        while (offset < source.length && /[A-Za-z0-9_.]/.test(source[offset])) {
            offset += 1;
        }
        while (offset < source.length && /\s/.test(source[offset])) {
            offset += 1;
        }
        if (source[offset] !== "(") {
            continue;
        }
        let depth = 0;
        do {
            if (source[offset] === "(") {
                depth += 1;
            } else if (source[offset] === ")") {
                depth -= 1;
            }
            offset += 1;
        } while (offset < source.length && depth !== 0);
        for (let index = start; index < offset; index += 1) {
            if (masked[index] !== "\n") {
                masked[index] = " ";
            }
        }
    }
    return masked.join("");
}

function splitTopLevel(source, separator) {
    const parts = [];
    let start = 0;
    let depth = 0;
    for (let offset = 0; offset < source.length; offset += 1) {
        if (source[offset] === "<") {
            depth += 1;
        } else if (source[offset] === ">") {
            depth = Math.max(0, depth - 1);
        } else if (source[offset] === separator && depth === 0) {
            parts.push(source.slice(start, offset));
            start = offset + 1;
        }
    }
    parts.push(source.slice(start));
    return parts;
}

function collectTypeParameters(source) {
    if (!source) {
        return [];
    }
    return splitTopLevel(source, ",")
        .map((parameter) => parameter.trim())
        .filter((parameter) => /^[A-Za-z_][A-Za-z0-9_]*$/.test(parameter));
}

function collectParameters(source) {
    return splitTopLevel(source, ",")
        .map((parameter) => parameter.match(
            /^\s*(?:@[A-Za-z_][A-Za-z0-9_.]*\([^)]*\)\s*)*[&$]?([A-Za-z_][A-Za-z0-9_]*)\s+[A-Za-z_\[]/
        ))
        .filter(Boolean)
        .map((parameter) => parameter[1]);
}

function collectBracedDeclarations(source, keyword) {
    const declarations = [];
    const header = new RegExp(
        `\\b${keyword}\\s+([A-Za-z_][A-Za-z0-9_]*)(?:\\s*<([^>{}]*)>)?` +
        "(?:\\s+(?:implements|extends)\\s+([^{}]+?))?\\s*\\{",
        "g"
    );
    let match;
    while ((match = header.exec(source)) !== null) {
        const open = header.lastIndex - 1;
        let depth = 1;
        let offset = open + 1;
        while (offset < source.length && depth !== 0) {
            if (source[offset] === "{") {
                depth += 1;
            } else if (source[offset] === "}") {
                depth -= 1;
            }
            offset += 1;
        }
        declarations.push({
            name: match[1],
            typeParameters: collectTypeParameters(match[2]),
            implementations: match[3] || "",
            body: source.slice(open + 1, depth === 0 ? offset - 1 : source.length),
            start: match.index,
            end: offset
        });
        header.lastIndex = offset;
    }
    return declarations;
}

function collectAttributeDeclarations(source) {
    const depths = topLevelDepths(source);
    const declarations = [];
    const pattern = /\battribute\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s+targets\s*\(([^)]*)\)(?:\s+(repeatable))?/g;
    let match;
    while ((match = pattern.exec(source)) !== null) {
        if (depths[match.index] !== 0) {
            continue;
        }
        declarations.push({
            name: match[1],
            kind: "Attribute",
            parameters: collectParameters(match[2]),
            targets: match[3].split(",").map((target) => target.trim()).filter(Boolean),
            repeatable: Boolean(match[4])
        });
    }
    return declarations;
}

function topLevelDepths(source) {
    const depths = new Uint16Array(source.length + 1);
    let depth = 0;
    for (let offset = 0; offset < source.length; offset += 1) {
        depths[offset] = depth;
        if (source[offset] === "{") {
            depth += 1;
        } else if (source[offset] === "}") {
            depth = Math.max(0, depth - 1);
        }
    }
    depths[source.length] = depth;
    return depths;
}

function collectMethods(source, owner, contract = false) {
    return [...source.matchAll(
        /\bfn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)/g
    )].map((match) => ({
        name: match[1],
        kind: "Method",
        owner,
        contract,
        defaultMethod: contract && /^[^\n{]*\{/.test(
            source.slice(match.index + match[0].length)
        ),
        parameters: collectParameters(match[2])
    }));
}

function topLevelSurface(source) {
    let result = "";
    let depth = 0;
    for (const character of source) {
        if (character === "{") {
            depth += 1;
            result += " ";
        } else if (character === "}") {
            depth = Math.max(0, depth - 1);
            result += " ";
        } else {
            result += depth === 0 || character === "\n" ? character : " ";
        }
    }
    return result;
}

function skipType(tokens, offset) {
    if (["own", "view", "edit"].includes(tokens[offset])) {
        offset += 1;
    }
    if (tokens[offset] === "fn") {
        offset += 1;
        if (tokens[offset] === "(") {
            let depth = 0;
            do {
                if (tokens[offset] === "(") {
                    depth += 1;
                } else if (tokens[offset] === ")") {
                    depth -= 1;
                }
                offset += 1;
            } while (offset < tokens.length && depth !== 0);
        }
        return skipType(tokens, offset);
    }
    if (tokens[offset] === "[") {
        offset += 1;
        if (/^[0-9]+$/.test(tokens[offset] || "")) {
            offset += 1;
            if (tokens[offset] === "]") {
                offset += 1;
            }
            return skipType(tokens, offset);
        }
        offset = skipType(tokens, offset);
        return tokens[offset] === "]" ? offset + 1 : offset;
    }
    offset += 1;
    if (tokens[offset] !== "<") {
        return offset;
    }
    let depth = 0;
    do {
        if (tokens[offset] === "<") {
            depth += 1;
        } else if (tokens[offset] === ">") {
            depth -= 1;
        }
        offset += 1;
    } while (offset < tokens.length && depth !== 0);
    return offset;
}

function collectStructFields(source) {
    const surface = topLevelSurface(source).replace(
        /\bfn\s+[A-Za-z_][A-Za-z0-9_]*\s*\([^)]*\)\s+[^\n{]+/g,
        ""
    );
    const tokens = [...surface.matchAll(/[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*|[0-9]+|[<>,()[\]]/g)]
        .map((match) => match[0]);
    const fields = [];
    let offset = 0;
    while (offset + 1 < tokens.length) {
        fields.push(tokens[offset]);
        offset += 1;
        offset = skipType(tokens, offset);
    }
    return fields;
}

function collectPackageDeclarations(source) {
    const lexical = maskTrivia(source);
    const masked = maskAttributeApplications(lexical);
    const depths = topLevelDepths(masked);
    const packageMatch = masked.match(/\bpackage\s+([A-Za-z_][A-Za-z0-9_.]*)/);
    const imported = [...masked.matchAll(
        /\bimport\s+([A-Za-z_][A-Za-z0-9_.]*)(?:\s+as\s+([A-Za-z_][A-Za-z0-9_]*))?/g
    )].map((match) => ({
        packageName: match[1],
        alias: match[2] || match[1].split(".").at(-1)
    }));
    const functions = [...masked.matchAll(
        /\b(?:fn|task)\s+([A-Z][A-Za-z0-9_]*)(?:\s*<([^>{}]*)>)?\s*\(([^)]*)\)/g
    )].filter((match) => depths[match.index] === 0).map((match) => ({
        name: match[1],
        kind: "Function",
        typeParameters: collectTypeParameters(match[2]),
        parameters: collectParameters(match[3])
    }));
    const structs = collectBracedDeclarations(masked, "struct")
        .filter((declaration) => /^[A-Z]/.test(declaration.name))
        .map((declaration) => ({
        name: declaration.name,
        kind: "Struct",
        typeParameters: declaration.typeParameters,
        methods: collectMethods(declaration.body, declaration.name)
            .filter((method) => /^[A-Z]/.test(method.name))
    }));
    const enums = collectBracedDeclarations(masked, "enum")
        .filter((declaration) => /^[A-Z]/.test(declaration.name))
        .map((declaration) => ({
        name: declaration.name,
        kind: "Enum",
        typeParameters: declaration.typeParameters,
        variants: [...declaration.body.matchAll(
            /(?:^|\s)([A-Z][A-Za-z0-9_]*)(?:\s*\(\s*([^)]*)\))?/g
        )].map((variant) => ({ name: variant[1], payload: Boolean(variant[2]) }))
    }));
    const contracts = collectBracedDeclarations(masked, "contract")
        .filter((declaration) => /^[A-Z]/.test(declaration.name))
        .map((declaration) => ({
            name: declaration.name,
            kind: "Contract",
            typeParameters: declaration.typeParameters,
            methods: collectMethods(declaration.body, declaration.name, true)
                .filter((method) => /^[A-Z]/.test(method.name))
        }));
    const attributes = collectAttributeDeclarations(lexical)
        .filter((declaration) => /^[A-Z]/.test(declaration.name));

    return {
        packageName: packageMatch ? packageMatch[1] : null,
        imports: imported,
        declarations: [...functions, ...structs, ...enums, ...contracts, ...attributes]
    };
}

function importedCompletions(source, projectSources) {
    const current = collectPackageDeclarations(source);
    const packages = new Map();
    for (const projectSource of projectSources) {
        const parsed = collectPackageDeclarations(projectSource);
        if (!parsed.packageName) {
            continue;
        }
        const declarations = packages.get(parsed.packageName) || [];
        declarations.push(...parsed.declarations);
        packages.set(parsed.packageName, declarations);
    }

    const completions = [...packages.keys()].map((packageName) => ({
        label: packageName,
        kind: "Module",
        detail: "Foundation package"
    }));
    for (const imported of current.imports) {
        completions.push({
            label: imported.alias,
            kind: "Module",
            detail: `Alias for ${imported.packageName}`
        });
        for (const declaration of packages.get(imported.packageName) || []) {
            const label = `${imported.alias}.${declaration.name}`;
            const typeArguments = (declaration.typeParameters || [])
                .map((parameter, index) => `\${${index + 1}:${parameter}}`)
                .join(", ");
            const qualified = `${label}${typeArguments ? `<${typeArguments}>` : ""}`;
            if (declaration.kind === "Attribute") {
                completions.push({
                    label: `@${label}`,
                    kind: "Attribute",
                    detail: `Typed attribute from ${imported.packageName} for ${declaration.targets.join(", ")}`,
                    insertText: `@${label}(${declaration.parameters.map((name, index) =>
                        `\${${index + 1}:${name}}`).join(", ")})`
                });
                continue;
            }
            if (declaration.kind === "Function") {
                const offset = declaration.typeParameters.length;
                const argumentsText = declaration.parameters
                    .map((name, index) => `\${${offset + index + 1}:${name}}`)
                    .join(", ");
                completions.push({
                    label,
                    kind: "Function",
                    detail: `Exported function from ${imported.packageName}`,
                    insertText: `${qualified}(${argumentsText})`
                });
                continue;
            }
            completions.push({
                label,
                kind: declaration.kind,
                detail: `Exported ${declaration.kind.toLowerCase()} from ${imported.packageName}`,
                insertText: qualified
            });
            for (const method of declaration.methods || []) {
                completions.push({
                    label: method.name,
                    kind: "Method",
                    detail: `Exported method of ${declaration.name} from ${imported.packageName}`,
                    insertText: `${method.name}(${method.parameters.map((name, index) =>
                        `\${${index + 1}:${name}}`).join(", ")})`
                });
            }
            if (declaration.kind === "Enum") {
                for (const variant of declaration.variants) {
                    completions.push({
                        label: `${label}.${variant.name}`,
                        kind: "EnumMember",
                        detail: `Exported variant from ${imported.packageName}`,
                        insertText: variant.payload
                            ? `${qualified}.${variant.name}(\${${declaration.typeParameters.length + 1}:value})`
                            : `${qualified}.${variant.name}`
                    });
                }
            }
        }
    }
    return completions;
}

function collectCompletions(source, projectSources = []) {
    const lexical = maskTrivia(source);
    const masked = maskAttributeApplications(lexical);
    const depths = topLevelDepths(masked);
    const completions = [...staticCompletions, ...importedCompletions(source, projectSources)];
    const functions = /\b(?:fn|task)\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*<([^>{}]*)>)?\s*\(([^)]*)\)/g;
    const structs = collectBracedDeclarations(masked, "struct");
    const methodBlocks = collectBracedDeclarations(masked, "methods");
    const enums = collectBracedDeclarations(masked, "enum");
    const contracts = collectBracedDeclarations(masked, "contract");
    const attributes = collectAttributeDeclarations(lexical);
    const bindings = /\b(?:let|const|var)\s+([A-Za-z_][A-Za-z0-9_]*)/g;
    const structPatterns = /\b(?:let|const)\s+[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*\s*\{([^}]*)\}\s*=/g;
    let match;

    for (const declaration of attributes) {
        completions.push({
            label: `@${declaration.name}`,
            kind: "Attribute",
            detail: `Typed attribute for ${declaration.targets.join(", ")}${declaration.repeatable ? ", repeatable" : ""}`,
            insertText: `@${declaration.name}(${declaration.parameters.map((name, index) =>
                `\${${index + 1}:${name}}`).join(", ")})`
        });
    }

    for (const declaration of structs) {
        const name = declaration.name;
        const typeParameters = declaration.typeParameters;
        const fields = collectStructFields(declaration.body);
        const delegates = [...declaration.body.matchAll(
            /\bdelegate\s+([A-Za-z_][A-Za-z0-9_]*)\s+as\s+([A-Za-z_][A-Za-z0-9_]*(?:\s*<[^{}()\n]+>)?)/g
        )];
        const delegation = delegates.length === 0
            ? ""
            : `; delegates ${delegates.map((entry) => `${entry[2]} to ${entry[1]}`).join(", ")}`;
        completions.push({
            label: name,
            kind: "Struct",
            detail: declaration.implementations
                ? `Foundation struct implements ${declaration.implementations.trim()}${delegation}`
                : typeParameters.length === 0
                ? "Foundation struct"
                : `Foundation struct<${typeParameters.join(", ")}>`,
            insertText: `${name} { ${fields.map((field, index) => `${field} = \${${index + 1}:${field}}`).join(" ")} }`
        });
        for (const parameter of typeParameters) {
            completions.push({ label: parameter, kind: "TypeParameter", detail: `Type parameter of ${name}` });
        }
        for (const field of fields) {
            completions.push({
                label: field,
                kind: "Field",
                detail: `Field of ${name}`
            });
        }
        for (const method of collectMethods(declaration.body, name)) {
            completions.push({
                label: method.name,
                kind: "Method",
                detail: `Method of ${name}`,
                insertText: `${method.name}(${method.parameters.map((parameter, index) =>
                    `\${${index + 1}:${parameter}}`).join(", ")})`
            });
        }
    }

    for (const declaration of methodBlocks) {
        for (const method of collectMethods(declaration.body, declaration.name)) {
            completions.push({
                label: method.name,
                kind: "Method",
                detail: `Distributed method of ${declaration.name}`,
                insertText: `${method.name}(${method.parameters.map((parameter, index) =>
                    `\${${index + 1}:${parameter}}`).join(", ")})`
            });
        }
    }

    for (const declaration of enums) {
        const name = declaration.name;
        const typeParameters = declaration.typeParameters;
        const variants = [...declaration.body.matchAll(/(?:^|\s)([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(\s*([^)]*)\))?/g)];
        const typeArguments = typeParameters
            .map((parameter, index) => `\${${index + 1}:${parameter}}`)
            .join(", ");
        const qualifier = typeParameters.length === 0 ? name : `${name}<${typeArguments}>`;
        completions.push({
            label: name,
            kind: "Enum",
            detail: typeParameters.length === 0
                ? "Foundation enum"
                : `Foundation enum<${typeParameters.join(", ")}>`
        });
        for (const parameter of typeParameters) {
            completions.push({ label: parameter, kind: "TypeParameter", detail: `Type parameter of ${name}` });
        }
        for (const variant of variants) {
            completions.push({
                label: `${name}.${variant[1]}`,
                kind: "EnumMember",
                detail: `Variant of ${name}`,
                insertText: variant[2]
                    ? `${qualifier}.${variant[1]}(\${${typeParameters.length + 1}:value})`
                    : `${qualifier}.${variant[1]}`
            });
        }
    }

    for (const declaration of contracts) {
        const name = declaration.name;
        const typeParameters = declaration.typeParameters;
        completions.push({
            label: name,
            kind: "Contract",
            detail: declaration.implementations
                ? `Foundation contract extends ${declaration.implementations.trim()}`
                : typeParameters.length === 0
                ? "Foundation contract"
                : `Foundation contract<${typeParameters.join(", ")}>`
        });
        for (const parameter of typeParameters) {
            completions.push({
                label: parameter,
                kind: "TypeParameter",
                detail: `Type parameter of ${name}`
            });
        }
        for (const method of collectMethods(declaration.body, name, true)) {
            completions.push({
                label: method.name,
                kind: "Method",
                detail: method.defaultMethod
                    ? `Default contract method of ${name}`
                    : `Contract method of ${name}`,
                insertText: `${method.name}(${method.parameters.map((parameter, index) =>
                    `\${${index + 1}:${parameter}}`).join(", ")})`
            });
        }
    }

    while ((match = functions.exec(masked)) !== null) {
        if (depths[match.index] !== 0) {
            continue;
        }
        const typeParameters = collectTypeParameters(match[2]);
        const parameters = collectParameters(match[3]);

        completions.push({
            label: match[1],
            kind: "Function",
            detail: "Foundation function",
            insertText: `${match[1]}(${parameters.map((name, index) => `\${${index + 1}:${name}}`).join(", ")})`
        });

        for (const parameter of typeParameters) {
            completions.push({
                label: parameter,
                kind: "TypeParameter",
                detail: `Type parameter of ${match[1]}`
            });
        }

        for (const parameter of parameters) {
            completions.push({
                label: parameter,
                kind: "Variable",
                detail: "Function parameter"
            });
        }
    }

    while ((match = bindings.exec(masked)) !== null) {
        if (/^\s*(?:\.[A-Za-z_][A-Za-z0-9_]*)*\s*\{/.test(
            masked.slice(bindings.lastIndex)
        )) {
            continue;
        }
        completions.push({
            label: match[1],
            kind: "Variable",
            detail: "Local binding"
        });
    }

    while ((match = structPatterns.exec(masked)) !== null) {
        for (const field of match[1].matchAll(
            /\b([A-Za-z_][A-Za-z0-9_]*)(?:\s+as\s+([A-Za-z_][A-Za-z0-9_]*))?/g
        )) {
            completions.push({
                label: field[2] || field[1],
                kind: "Variable",
                detail: "Destructured field binding"
            });
        }
    }

    const unique = new Map();
    for (const completion of completions) {
        const key = `${completion.label}\0${completion.kind}`;
        if (!unique.has(key)) {
            unique.set(key, completion);
        }
    }

    return [...unique.values()].sort((left, right) => {
        if (left.label !== right.label) {
            return left.label < right.label ? -1 : 1;
        }
        if (left.kind === right.kind) {
            return 0;
        }
        return left.kind < right.kind ? -1 : 1;
    });
}

function findHover(source, word, projectSources = []) {
    const completions = collectCompletions(source, projectSources);
    const exact = completions.find((entry) => entry.label === word ||
        entry.label === `@${word}`);
    if (exact) {
        return exact;
    }
    const qualified = completions.filter((entry) => entry.label.endsWith(`.${word}`));
    return qualified.length === 1 ? qualified[0] : undefined;
}

module.exports = {
    collectCompletions,
    collectPackageDeclarations,
    findHover,
    maskAttributeApplications,
    maskTrivia,
    splitTopLevel,
    staticCompletions
};
