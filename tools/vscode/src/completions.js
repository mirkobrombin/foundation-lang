"use strict";

const parseIntegerCompletions = [
    ["I8", "i8"], ["I16", "i16"], ["I32", "i32"], ["I64", "i64"],
    ["Isize", "isize"], ["U8", "u8"], ["U16", "u16"], ["U32", "u32"],
    ["U64", "u64"], ["Usize", "usize"]
].map(([name, type]) => ({
    label: `parse.${name}`,
    kind: "Function",
    detail: `fn ${name}(value String) Result<${type}, parse.IntegerError>`,
    insertText: `parse.${name}(\${1:value})`
}));

const parseBooleanCompletion = {
    label: "parse.Bool",
    kind: "Function",
    detail: "fn Bool(value String) Result<bool, parse.BooleanError>",
    documentation: "Parse an ASCII boolean token after trimming Unicode whitespace.",
    insertText: "parse.Bool(\${1:value})"
};

const parseFloatCompletions = [
    ["F32", "f32"], ["F64", "f64"]
].map(([name, type]) => ({
    label: `parse.${name}`,
    kind: "Function",
    detail: `fn ${name}(value String) Result<${type}, parse.FloatError>`,
    documentation: `Parse a locale-independent IEEE 754 ${type} value.`,
    insertText: `parse.${name}(\${1:value})`
}));

const formatScalarCompletions = [
    ["Bool", "bool"], ["I8", "i8"], ["I16", "i16"], ["I32", "i32"],
    ["I64", "i64"], ["Isize", "isize"], ["U8", "u8"], ["U16", "u16"],
    ["U32", "u32"], ["U64", "u64"], ["Usize", "usize"], ["F32", "f32"],
    ["F64", "f64"]
].map(([name, type]) => ({
    label: `format.${name}`,
    kind: "Function",
    detail: `fn ${name}(value ${type}) String`,
    insertText: `format.${name}(\${1:value})`
}));

const staticCompletions = [
    { label: "package", kind: "Keyword" },
    { label: "import", kind: "Keyword" },
    { label: "as", kind: "Keyword" },
    { label: "attribute", kind: "Keyword", detail: "Declare typed compile-time metadata" },
    { label: "targets(...)", kind: "Keyword", insertText: "targets(${1|fn,struct,service,enum,contract,method,action,field,variant,parameter|})" },
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
    { label: "step", kind: "Keyword", detail: "Declare an ordered workflow step" },
    { label: "using", kind: "Keyword", detail: "Select a workflow function" },
    { label: "retry", kind: "Keyword", detail: "Apply a bounded workflow retry policy" },
    { label: "exponential", kind: "Keyword", detail: "Use deterministic exponential waiting" },
    { label: "max", kind: "Keyword", detail: "Set the total workflow attempt count" },
    { label: "compensate", kind: "Keyword", detail: "Compensate a completed saga step" },
    { label: "task", kind: "Keyword", detail: "Declare a suspendable function" },
    { label: "spawn", kind: "Keyword", detail: "Start a task on the active scheduler" },
    { label: "select", kind: "Keyword", detail: "Wait on typed channel operations" },
    { label: "timeout", kind: "Keyword", detail: "Use a monotonic select deadline" },
    { label: "test", kind: "Keyword", detail: "Declare an executable test" },
    { label: "unsafe", kind: "Keyword", detail: "Bound raw pointer operations" },
    { label: "fn", kind: "Keyword" },
    { label: "const", kind: "Keyword", detail: "Declare an immutable binding" },
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
    { label: "NumberError", kind: "Enum", detail: "checked numeric conversion failure" },
    { label: "Task", kind: "TypeParameter", detail: "owned concurrent result handle" },
    { label: "Channel", kind: "TypeParameter", detail: "owned channel endpoint pair" },
    { label: "Sender", kind: "TypeParameter", detail: "owned channel send endpoint" },
    { label: "Receiver", kind: "TypeParameter", detail: "owned channel receive endpoint" },
    { label: "TcpConnection", kind: "Struct", detail: "owned TCP connection" },
    { label: "TcpListener", kind: "Struct", detail: "owned TCP listener" },
    { label: "TcpReader", kind: "Struct", detail: "owned TCP read half" },
    { label: "TcpWriter", kind: "Struct", detail: "owned TCP write half" },
    { label: "StreamPair", kind: "Struct", detail: "owned TCP read and write halves" },
    { label: "std.platform", kind: "Module", detail: "Compilation target information" },
    { label: "std.env", kind: "Module", detail: "Read-only process environment" },
    { label: "std.text", kind: "Module", detail: "UTF-8 String inspection" },
    { label: "std.path", kind: "Module", detail: "Portable path operations" },
    { label: "std.parse", kind: "Module", detail: "Primitive value parsing" },
    { label: "std.fs", kind: "Module", detail: "Read-only filesystem operations" },
    { label: "std.net", kind: "Module", detail: "Portable TCP client and server operations" },
    { label: "std.format", kind: "Module", detail: "Primitive value formatting" },
    { label: "std.json", kind: "Module", detail: "Owned JSON values and parsing" },
    { label: "std.time", kind: "Module", detail: "Unix time values" },
    { label: "std.concurrent", kind: "Module", detail: "Cancellation and executor transfer" },
    { label: "@concurrent.Transferable()", kind: "Keyword", detail: "Opt a custom-drop owner into structural executor transfer", insertText: "@concurrent.Transferable()" },
    { label: "std.bytes", kind: "Module", detail: "Owned binary data and cryptographic encoding" },
    { label: "std.pattern", kind: "Module", detail: "Portable bounded pattern matching" },
    { label: "std.ring", kind: "Module", detail: "Preallocated bounded FIFO buffers" },
    { label: "std.safemap", kind: "Module", detail: "Actor-owned task-safe maps" },
    { label: "foundation.worker", kind: "Module", detail: "Supervised application tasks" },
    { label: "foundation.hosting", kind: "Module", detail: "Owned application lifecycle" },
    { label: "foundation.health", kind: "Module", detail: "Typed deterministic health checks" },
    { label: "foundation.plugin", kind: "Module", detail: "Validated native plugin lifecycle" },
    { label: "foundation.bind", kind: "Module", detail: "Compiler-generated typed struct binding" },
    { label: "foundation.validation", kind: "Module", detail: "Compiler-generated typed validation" },
    { label: "foundation.web", kind: "Module", detail: "Typed HTTP routing and serving" },
    { label: "foundation.auth", kind: "Module", detail: "Typed token signing and key rotation" },
    { label: "foundation.auth.web", kind: "Module", detail: "Typed Bearer handler boundary" },
    { label: "foundation.resiliency", kind: "Module", detail: "Typed nonblocking resilience policies" },
    { label: "foundation.resiliency.web", kind: "Module", detail: "Owned stateful web resilience middleware" },
    { label: "foundation.di", kind: "Module", detail: "Static dependency graph metadata" },
    { label: "foundation.actions", kind: "Module", detail: "Typed action dispatch metadata" },
    { label: "@di.Inject()", kind: "Keyword", detail: "Select a service constructor", insertText: "@di.Inject()" },
    { label: "@di.Scope(...)", kind: "Keyword", detail: "Set a service lifetime", insertText: "@di.Scope(${1|.Transient,.Scoped,.Singleton|})" },
    { label: "@di.Input()", kind: "Keyword", detail: "Supply an application boundary value", insertText: "@di.Input()" },
    { label: "@di.Name(...)", kind: "Keyword", detail: "Name a service provider", insertText: "@di.Name(\"${1:name}\")" },
    { label: "@di.From(...)", kind: "Keyword", detail: "Select a named provider", insertText: "@di.From(\"${1:name}\")" },
    { label: "@actions.Name(...)", kind: "Keyword", detail: "Set an action dispatch name", insertText: "@actions.Name(\"${1:name}\")" },
    { label: "@actions.Key(...)", kind: "Keyword", detail: "Attach an action key binding", insertText: "@actions.Key(\"${1:key}\")" },
    { label: "@actions.Policy(...)", kind: "Keyword", detail: "Require an action policy", insertText: "@actions.Policy(\"${1:policy}\")" },
    { label: "@bind.Bindable()", kind: "Keyword", detail: "Generate a typed Bind method for a concrete struct", insertText: "@bind.Bindable()" },
    { label: "@bind.Name(...)", kind: "Keyword", detail: "Select a generated binding source key", insertText: "@bind.Name(\"${1:key}\")" },
    { label: "@bind.Ignore()", kind: "Keyword", detail: "Exclude a field from generated binding", insertText: "@bind.Ignore()" },
    { label: "@bind.From(...)", kind: "Keyword", detail: "Select a named binding source and key", insertText: "@bind.From(\"${1:source}\", \"${2:key}\")" },
    { label: "@bind.Default(...)", kind: "Keyword", detail: "Set a fallback when named sources are absent or empty", insertText: "@bind.Default(\"${1:value}\")" },
    { label: "@bind.JsonName(...)", kind: "Keyword", detail: "Select a strict JSON property name", insertText: "@bind.JsonName(\"${1:name}\")" },
    { label: "@bind.JSON()", kind: "Keyword", detail: "Select the single strict JSON body field", insertText: "@bind.JSON()" },
    { label: "@validation.Validatable()", kind: "Keyword", detail: "Generate ordered validation for a concrete struct", insertText: "@validation.Validatable()" },
    { label: "@validation.Required()", kind: "Keyword", detail: "Reject an empty String or List field", insertText: "@validation.Required()" },
    { label: "@validation.Min(...)", kind: "Keyword", detail: "Set an inclusive numeric minimum", insertText: "@validation.Min(${1:value})" },
    { label: "@validation.Max(...)", kind: "Keyword", detail: "Set an inclusive numeric maximum", insertText: "@validation.Max(${1:value})" },
    { label: "@validation.Email()", kind: "Keyword", detail: "Require the standard email shape", insertText: "@validation.Email()" },
    { label: "@validation.Pattern(...)", kind: "Keyword", detail: "Require a portable bounded pattern", insertText: "@validation.Pattern(\"${1:^.+$}\")" },
    { label: "@validation.Nested()", kind: "Keyword", detail: "Validate a nested model and prefix its field paths", insertText: "@validation.Nested()" },
    { label: "@validation.Rule()", kind: "Keyword", detail: "Mark a typed custom validation method", insertText: "@validation.Rule()" },
    { label: "@web.Route(...)", kind: "Keyword", detail: "Declare a typed HTTP endpoint", insertText: "@web.Route(${1|.GET,.POST,.PUT,.PATCH,.DELETE,.HEAD,.OPTIONS|}, \"${2:/path}\")" },
    { label: "@web.GlobalMiddleware(...)", kind: "Keyword", detail: "Wrap the complete generated HTTP application", insertText: "@web.GlobalMiddleware(${1:10})" },
    { label: "@web.GroupMiddleware(...)", kind: "Keyword", detail: "Wrap generated routes below a static path prefix", insertText: "@web.GroupMiddleware(\"${1:/api}\", ${2:10})" },
    { label: "@web.RouteMiddleware(...)", kind: "Keyword", detail: "Wrap one exact generated HTTP route", insertText: "@web.RouteMiddleware(${1|.GET,.POST,.PUT,.PATCH,.DELETE,.HEAD,.OPTIONS|}, \"${2:/path}\", ${3:10})" },
    { label: "@web.Path(...)", kind: "Keyword", detail: "Bind a route parameter", insertText: "@web.Path(\"${1:name}\")" },
    { label: "@web.Query(...)", kind: "Keyword", detail: "Bind a query value", insertText: "@web.Query(\"${1:name}\")" },
    { label: "@web.Header(...)", kind: "Keyword", detail: "Bind a request header", insertText: "@web.Header(\"${1:name}\")" },
    { label: "@web.Form(...)", kind: "Keyword", detail: "Bind a form value", insertText: "@web.Form(\"${1:name}\")" },
    { label: "@web.Body()", kind: "Keyword", detail: "Bind raw String or a strict @bind.Bindable JSON body", insertText: "@web.Body()" },
    { label: "@web.Inject()", kind: "Keyword", detail: "Resolve the unique singleton service for a parameter type", insertText: "@web.Inject()" },
    { label: "worker.Supervisor", kind: "Struct", detail: "owned supervised task lifetime" },
    { label: "worker.Group", kind: "Struct", detail: "bounded typed task completion group" },
    { label: "worker.GroupNext", kind: "Struct", detail: "completed value and still-owned task group" },
    { label: "worker.GroupWait", kind: "Struct", detail: "task completion or stop wake with still-owned group" },
    { label: "worker.GroupWake", kind: "Enum", detail: "typed completion or explicit stop reason" },
    { label: "worker.GroupError", kind: "Enum", detail: "task group admission or completion failure" },
    { label: "worker.Pool", kind: "Struct", detail: "bounded parallel task executor" },
    { label: "hosting.Host", kind: "Struct", detail: "owned one-shot application host" },
    { label: "hosting.HostedService", kind: "Interface", detail: "typed Start and Stop lifecycle contract" },
    { label: "hosting.BackgroundService", kind: "Interface", detail: "typed background task lifecycle contract" },
    { label: "hosting.RunReport", kind: "Struct", detail: "typed host stop reason and cleanup failures" },
    { label: "hosting.RunReason", kind: "Enum", detail: "host run completion reason" },
    { label: "hosting.State", kind: "Enum", detail: "host lifecycle state" },
    { label: "health.Registry", kind: "Struct", detail: "owned deterministic health checker registry" },
    { label: "health.Checker", kind: "Interface", detail: "typed health check contract" },
    { label: "health.Report", kind: "Struct", detail: "typed health status, duration, and details" },
    { label: "health.NamedReport", kind: "Struct", detail: "registered checker name and report" },
    { label: "health.Status", kind: "Enum", detail: "healthy, degraded, or unhealthy status" },
    { label: "plugin.Plugin", kind: "Interface", detail: "typed plugin lifecycle contract" },
    { label: "plugin.NativePlugin", kind: "Struct", detail: "owned validated native plugin" },
    { label: "plugin.Registry", kind: "Struct", detail: "owned deterministic plugin registry" },
    { label: "plugin.FactoryRegistry", kind: "Struct", detail: "owned named plugin factories" },
    { label: "plugin.ExecSandbox", kind: "Struct", detail: "owned external plugin process" },
    { label: "plugin.ErrorKind", kind: "Enum", detail: "stable native plugin failure kind" },
    { label: "plugin.Error", kind: "Struct", detail: "native plugin failure and copied detail" },
    { label: "plugin.NamedError", kind: "Struct", detail: "plugin name and lifecycle failure" },
    { label: "plugin.RegistrationFailure", kind: "Struct", detail: "rejected plugin and name" },
    { label: "plugin.StartFailure", kind: "Struct", detail: "startup error and rollback failures" },
    { label: "plugin.FactoryErrorKind", kind: "Enum", detail: "plugin factory lookup or registration failure" },
    { label: "plugin.FactoryRegistrationFailure", kind: "Struct", detail: "rejected named plugin factory" },
    { label: "plugin.SandboxErrorKind", kind: "Enum", detail: "external plugin process failure kind" },
    { label: "plugin.SandboxError", kind: "Struct", detail: "external plugin process failure" },
    { label: "plugin.SandboxStartOutcome", kind: "Struct", detail: "started process and still-owned sandbox" },
    { label: "plugin.SandboxStopOutcome", kind: "Struct", detail: "process exit and still-owned sandbox" },
    { label: "bind.Values", kind: "Struct", detail: "owned reusable string binding source" },
    { label: "bind.Entry", kind: "Struct", detail: "binding source key and value" },
    { label: "validation.ErrorKind", kind: "Enum", detail: "typed validation failure kind" },
    { label: "validation.Error", kind: "Struct", detail: "field validation failure" },
    { label: "validation.Errors", kind: "Struct", detail: "owned ordered validation failures" },
    { label: "bind.SourceEntry", kind: "Struct", detail: "named binding source, key, and value" },
    { label: "bind.Sources", kind: "Struct", detail: "ordered evaluated named binding sources" },
    { label: "bind.Error", kind: "Struct", detail: "typed binding failure with field, key, value, JSON cause, and offset" },
    { label: "bind.ErrorKind", kind: "Enum", detail: "conversion, JSON syntax, shape, or unknown-field failure" },
    { label: "web.Server", kind: "Struct", detail: "owned typed HTTP server" },
    { label: "web.Router", kind: "Struct", detail: "owned deterministic HTTP router" },
    { label: "web.RouteTable", kind: "Struct", detail: "validated handler-free route metadata" },
    { label: "web.RouteMatch", kind: "Struct", detail: "matched route ID and owned path parameters" },
    { label: "web.Handler", kind: "Interface", detail: "typed HTTP handler contract" },
    { label: "web.Middleware", kind: "Interface", detail: "owned reusable stateful middleware contract" },
    { label: "web.Application", kind: "Interface", detail: "shared typed request dispatch graph" },
    { label: "web.Request", kind: "Struct", detail: "owned parsed HTTP request" },
    { label: "web.Response", kind: "Struct", detail: "owned HTTP response" },
    { label: "web.Method", kind: "Enum", detail: "supported HTTP request method" },
    { label: "web.MatchError", kind: "Enum", detail: "not found or method not allowed route result" },
    { label: "web.DispatchError", kind: "Enum", detail: "transport or typed handler dispatch failure" },
    { label: "web.MiddlewareRegistrationError", kind: "Enum", detail: "invalid manual middleware scope or duplicate order" },
    { label: "web.ServeOutcome", kind: "Struct", detail: "served request and reusable server" },
    { label: "bytes.Bytes", kind: "Struct", detail: "owned binary data cleared before release" },
    { label: "bytes.Error", kind: "Enum", detail: "binary data or encoding failure" },
    { label: "bytes.FromText", kind: "Function", detail: "fn FromText(value String) own Bytes", insertText: "bytes.FromText(${1:value})" },
    { label: "bytes.EncodeBase64URL", kind: "Function", detail: "fn EncodeBase64URL(value Bytes) Result<String, Error>", insertText: "bytes.EncodeBase64URL(${1:value})" },
    { label: "bytes.DecodeBase64URL", kind: "Function", detail: "fn DecodeBase64URL(value String) Result<own Bytes, Error>", insertText: "bytes.DecodeBase64URL(${1:value})" },
    { label: "bytes.HmacSha256", kind: "Function", detail: "fn HmacSha256(key Bytes, value Bytes) Result<own Bytes, Error>", insertText: "bytes.HmacSha256(${1:key}, ${2:value})" },
    { label: "bytes.ConstantTimeEqual", kind: "Function", detail: "fn ConstantTimeEqual(left Bytes, right Bytes) bool", insertText: "bytes.ConstantTimeEqual(${1:left}, ${2:right})" },
    { label: "bytes.Bytes.Copy", kind: "Method", detail: "fn Copy(self) Result<own Bytes, Error>", insertText: "Copy()" },
    { label: "bytes.Bytes.Len", kind: "Method", detail: "fn Len(self) Result<u64, Error>", insertText: "Len()" },
    { label: "bytes.Bytes.At", kind: "Method", detail: "fn At(self, index u64) Result<u64, Error>", insertText: "At(${1:index})" },
    { label: "bytes.Bytes.Text", kind: "Method", detail: "fn Text(self) Result<String, Error>", insertText: "Text()" },
    { label: "bytes.Bytes.Close", kind: "Method", detail: "fn Close(&self) bool", insertText: "Close()" },
    { label: "ring.Buffer", kind: "Struct", detail: "preallocated bounded FIFO for owned values" },
    { label: "ring.ByteBuffer", kind: "Struct", detail: "preallocated bounded FIFO for bytes" },
    { label: "ring.ConfigurationError", kind: "Enum", detail: "invalid ring capacity" },
    { label: "ring.PushError", kind: "Enum", detail: "full ring with returned value" },
    { label: "ring.New", kind: "Function", detail: "fn New<T>(capacity i32) Result<own Buffer<T>, ConfigurationError>", insertText: "ring.New<${1:T}>(${2:capacity})" },
    { label: "ring.NewBytes", kind: "Function", detail: "fn NewBytes(capacity i32) Result<own ByteBuffer, ConfigurationError>", insertText: "ring.NewBytes(${1:capacity})" },
    { label: "ring.Buffer.Cap", kind: "Method", detail: "fn Cap(self) i32", insertText: "Cap()" },
    { label: "ring.Buffer.Len", kind: "Method", detail: "fn Len(self) i32", insertText: "Len()" },
    { label: "ring.Buffer.Space", kind: "Method", detail: "fn Space(self) i32", insertText: "Space()" },
    { label: "ring.Buffer.IsEmpty", kind: "Method", detail: "fn IsEmpty(self) bool", insertText: "IsEmpty()" },
    { label: "ring.Buffer.Push", kind: "Method", detail: "fn Push(&self, $value T) Result<void, PushError<T>>", insertText: "Push(\\$${1:value})" },
    { label: "ring.Buffer.Pop", kind: "Method", detail: "fn Pop(&self) Option<T>", insertText: "Pop()" },
    { label: "ring.Buffer.Peek", kind: "Method", detail: "fn Peek(&self, operation fn(T) void) bool", insertText: "Peek(${1:operation})" },
    { label: "ring.Buffer.Drain", kind: "Method", detail: "fn Drain(&self) own collections.Queue<T>", insertText: "Drain()" },
    { label: "ring.Buffer.Reset", kind: "Method", detail: "fn Reset(&self) void", insertText: "Reset()" },
    { label: "ring.ByteBuffer.Write", kind: "Method", detail: "fn Write(&self, source [u8]) usize", insertText: "Write(${1:source})" },
    { label: "ring.ByteBuffer.Read", kind: "Method", detail: "fn Read(&self, &destination [u8]) usize", insertText: "Read(&${1:destination})" },
    { label: "ring.ByteBuffer.Reset", kind: "Method", detail: "fn Reset(&self) void", insertText: "Reset()" },
    { label: "safemap.Error", kind: "Enum", detail: "map owner task failure" },
    { label: "safemap.Pair", kind: "Struct", detail: "owned key-value snapshot entry" },
    { label: "safemap.Handle", kind: "Struct", detail: "clonable task-safe map handle" },
    { label: "safemap.Map", kind: "Struct", detail: "actor-owned task-safe map" },
    { label: "safemap.ShardedMap", kind: "Struct", detail: "hashed task-safe map with optional TTL" },
    { label: "safemap.New", kind: "Function", detail: "fn New<K, V>($equal fn(K, K) bool, $cloneKey fn(K) K, $cloneValue fn(V) V) own Map<K, V>", insertText: "safemap.New<${1:K}, ${2:V}>(\\$${3:equal}, \\$${4:cloneKey}, \\$${5:cloneValue})" },
    { label: "safemap.NewSharded", kind: "Function", detail: "fn NewSharded<K, V>($equal fn(K, K) bool, $cloneKey fn(K) K, $cloneValue fn(V) V, $hash fn(K) u64, shardCount i32) own ShardedMap<K, V>", insertText: "safemap.NewSharded<${1:K}, ${2:V}>(\\$${3:equal}, \\$${4:cloneKey}, \\$${5:cloneValue}, \\$${6:hash}, ${7:shardCount})" },
    { label: "safemap.StringEqual", kind: "Function", detail: "fn StringEqual(left String, right String) bool", insertText: "safemap.StringEqual" },
    { label: "safemap.CloneString", kind: "Function", detail: "fn CloneString(value String) String", insertText: "safemap.CloneString" },
    { label: "safemap.StringHasher", kind: "Function", detail: "fn StringHasher(value String) u64", insertText: "safemap.StringHasher" },
    { label: "safemap.Handle.Clone", kind: "Method", detail: "fn Clone(self) Handle<K, V>", insertText: "Clone()" },
    { label: "safemap.Map.Handle", kind: "Method", detail: "fn Handle(self) Handle<K, V>", insertText: "Handle()" },
    { label: "safemap.Map.Set", kind: "Method", detail: "fn Set(self, $key K, $value V) Task<Result<void, Error>>", insertText: "Set(\\$${1:key}, \\$${2:value})" },
    { label: "safemap.Map.Get", kind: "Method", detail: "fn Get(self, $key K) Task<Result<Option<V>, Error>>", insertText: "Get(\\$${1:key})" },
    { label: "safemap.Map.Delete", kind: "Method", detail: "fn Delete(self, $key K) Task<Result<bool, Error>>", insertText: "Delete(\\$${1:key})" },
    { label: "safemap.Map.Has", kind: "Method", detail: "fn Has(self, $key K) Task<Result<bool, Error>>", insertText: "Has(\\$${1:key})" },
    { label: "safemap.Map.Len", kind: "Method", detail: "fn Len(self) Task<Result<i32, Error>>", insertText: "Len()" },
    { label: "safemap.Map.Keys", kind: "Method", detail: "fn Keys(self) Task<Result<own collections.List<K>, Error>>", insertText: "Keys()" },
    { label: "safemap.Map.Values", kind: "Method", detail: "fn Values(self) Task<Result<own collections.List<V>, Error>>", insertText: "Values()" },
    { label: "safemap.Map.Snapshot", kind: "Method", detail: "fn Snapshot(self) Task<Result<own collections.List<Pair<K, V>>, Error>>", insertText: "Snapshot()" },
    { label: "safemap.Map.Clear", kind: "Method", detail: "fn Clear(self) Task<Result<void, Error>>", insertText: "Clear()" },
    { label: "safemap.Map.GetOrSet", kind: "Method", detail: "fn GetOrSet(self, $key K, $value V) Task<Result<V, Error>>", insertText: "GetOrSet(\\$${1:key}, \\$${2:value})" },
    { label: "safemap.Map.Compute", kind: "Method", detail: "fn Compute(self, $key K, $initial V, $update fn(V) V) Task<Result<V, Error>>", insertText: "Compute(\\$${1:key}, \\$${2:initial}, \\$${3:update})" },
    { label: "safemap.ShardedMap.WithExpiry", kind: "Method", detail: "fn WithExpiry(self, duration time.Duration) Task<Result<void, Error>>", insertText: "WithExpiry(${1:duration})" },
    { label: "auth.Payload", kind: "Struct", detail: "subject and Unix expiration claims" },
    { label: "auth.Claims", kind: "Interface", detail: "typed claim validation contract" },
    { label: "auth.StandardClaims", kind: "Struct", detail: "standard subject, expiration, issue, token, issuer, and audience claims" },
    { label: "auth.Error", kind: "Enum", detail: "typed token or key-ring failure" },
    { label: "auth.Key", kind: "Struct", detail: "owned validated HMAC-SHA256 key" },
    { label: "auth.Service", kind: "Struct", detail: "ordered signing and verification key ring" },
    { label: "auth.SignedToken", kind: "Struct", detail: "token, key ID, payload, and raw text" },
    { label: "auth.SignToken", kind: "Function", detail: "fn SignToken(payload Payload, secret bytes.Bytes) Result<String, Error>", insertText: "auth.SignToken(${1:payload}, ${2:secret})" },
    { label: "auth.VerifyToken", kind: "Function", detail: "fn VerifyToken(token String, secret bytes.Bytes) Result<Payload, Error>", insertText: "auth.VerifyToken(${1:token}, ${2:secret})" },
    { label: "auth.ValidateHMACSecret", kind: "Function", detail: "fn ValidateHMACSecret(secret bytes.Bytes) Result<void, Error>", insertText: "auth.ValidateHMACSecret(${1:secret})" },
    { label: "auth.NewHMACKey", kind: "Function", detail: "fn NewHMACKey($id String, $secret own bytes.Bytes) Result<own Key, Error>", insertText: "auth.NewHMACKey(\"${1:key-id}\", \\$${2:secret})" },
    { label: "auth.NewService", kind: "Function", detail: "fn NewService($primary own Key) own Service", insertText: "auth.NewService(\\$${1:primary})" },
    { label: "auth.Service.AddKey", kind: "Method", detail: "fn AddKey(&self, $key own Key) Result<void, Error>", insertText: "AddKey(\\$${1:key})" },
    { label: "auth.Service.Sign", kind: "Method", detail: "fn Sign(&self, claims StandardClaims) Result<SignedToken, Error>", insertText: "Sign(${1:claims})" },
    { label: "auth.Service.Verify", kind: "Method", detail: "fn Verify(&self, token String) Result<StandardClaims, Error>", insertText: "Verify(${1:token})" },
    { label: "authWeb.AuthenticatedHandler", kind: "Interface", detail: "handler receiving verified claims" },
    { label: "authWeb.Bearer", kind: "Struct", detail: "typed Bearer-protected web handler" },
    { label: "authWeb.BearerError", kind: "Enum", detail: "authorization, token, or handler failure" },
    { label: "authWeb.Protect", kind: "Function", detail: "fn Protect<E>($secret own bytes.Bytes, $handler own AuthenticatedHandler<E>) Result<own Bearer<E>, auth.Error>", insertText: "authWeb.Protect<${1:Error}>(\\$${2:secret}, \\$${3:handler})" },
    { label: "resiliency.RateLimiter", kind: "Struct", detail: "monotonic nonblocking token bucket" },
    { label: "resiliency.ConfigurationError", kind: "Enum", detail: "invalid resiliency policy configuration" },
    { label: "resiliency.NewRateLimiter", kind: "Function", detail: "fn NewRateLimiter(rate i32, burst i32) Result<RateLimiter, ConfigurationError>", insertText: "resiliency.NewRateLimiter(${1:rate}, ${2:burst})" },
    { label: "resiliency.RateLimiter.Allow", kind: "Method", detail: "fn Allow(&self) bool", insertText: "Allow()" },
    { label: "resiliency.RateLimiter.AllowAt", kind: "Method", detail: "fn AllowAt(&self, now time.MonotonicInstant) bool", insertText: "AllowAt(${1:now})" },
    { label: "resiliency.Wait", kind: "Function", detail: "task Wait($limiter RateLimiter) RateLimitWait", insertText: "resiliency.Wait(\\$${1:limiter})" },
    { label: "resiliency.CircuitBreaker", kind: "Struct", detail: "typed closed, open, and half-open circuit policy" },
    { label: "resiliency.NewCircuitBreaker", kind: "Function", detail: "fn NewCircuitBreaker<E>(threshold i32, openDuration time.Duration) Result<CircuitBreaker<E>, ConfigurationError>", insertText: "resiliency.NewCircuitBreaker<${1:Error}>(${2:threshold}, ${3:openDuration})" },
    { label: "resiliency.RetryOptions", kind: "Struct", detail: "typed attempts, exponential delay, jitter, and error filter" },
    { label: "resiliency.Retry", kind: "Function", detail: "task Retry<T, E>($operation fn() Result<T, E>, $options RetryOptions<E>) Result<T, RetryError<E>>", insertText: "resiliency.Retry<${1:T}, ${2:E}>(\\$${3:operation}, \\$${4:options})" },
    { label: "resiliency.Bulkhead", kind: "Struct", detail: "owned FIFO bounded concurrency policy" },
    { label: "resiliency.NewBulkhead", kind: "Function", detail: "fn NewBulkhead(maxConcurrent u64, maxQueue u64) Result<own Bulkhead, ConfigurationError>", insertText: "resiliency.NewBulkhead(${1:maxConcurrent}, ${2:maxQueue})" },
    { label: "rateWeb.RateLimit", kind: "Function", detail: "fn RateLimit<E>(rate i32, burst i32) Result<own web.Middleware<E>, resiliency.ConfigurationError>", insertText: "rateWeb.RateLimit<${1:Error}>(${2:rate}, ${3:burst})" },
    { label: "rateWeb.RateLimitWithPolicy", kind: "Function", detail: "fn RateLimitWithPolicy<E>(rate i32, burst i32, clientTTL time.Duration, maxClients i32) Result<own web.Middleware<E>, resiliency.ConfigurationError>", insertText: "rateWeb.RateLimitWithPolicy<${1:Error}>(${2:rate}, ${3:burst}, ${4:clientTTL}, ${5:maxClients})" },
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
        detail: "fn Get(name String) Result<Option<String>, env.Error>",
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
        detail: "fn ByteLen(value String) u64",
        insertText: "text.ByteLen(view ${1:value})"
    },
    {
        label: "text.Contains",
        kind: "Function",
        detail: "fn Contains(value String, part String) bool",
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
        detail: "fn Join(left String, right String) String",
        insertText: "path.Join(view ${1:left}, view ${2:right})"
    },
    parseBooleanCompletion,
    ...parseIntegerCompletions,
    {
        label: "fs.OpenLines",
        kind: "Function",
        detail: "fn OpenLines(path String) Result<own fs.LineReader, fs.Error>",
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
        detail: "fn OpenDir(path String) Result<own fs.DirReader, fs.Error>",
        insertText: "fs.OpenDir(view ${1:path})"
    },
    {
        label: "fs.Size",
        kind: "Function",
        detail: "fn Size(path String) Result<u64, fs.Error>",
        insertText: "fs.Size(view ${1:path})"
    },
    {
        label: "fs.Modified",
        kind: "Function",
        detail: "fn Modified(path String) Result<u64, fs.Error>",
        insertText: "fs.Modified(view ${1:path})"
    },
    {
        label: "fs.LineReader.Next",
        kind: "Method",
        detail: "fn Next(&self) Result<Option<String>, fs.Error>",
        insertText: "Next()"
    },
    {
        label: "fs.LineReader.NextLimited",
        kind: "Method",
        detail: "fn NextLimited(&self, limit u64) Result<Option<String>, fs.Error>",
        insertText: "NextLimited(${1:limit})"
    },
    {
        label: "net.Listen",
        kind: "Function",
        detail: "fn Listen(address String, port u64) Result<own net.TcpListener, net.Error>",
        insertText: "net.Listen(${1:address}, ${2:port})"
    },
    {
        label: "net.Accept",
        kind: "Function",
        detail: "task Accept(listener own net.TcpListener) net.AcceptOutcome",
        insertText: "net.Accept(${1:listener})"
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
        detail: "fn Split($self) Result<net.StreamPair, net.Error>",
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
        label: "net.ReadExact",
        kind: "Function",
        detail: "task ReadExact(reader own net.TcpReader, length u64) net.ReadOutcome",
        insertText: "net.ReadExact(${1:reader}, ${2:length})"
    },
    {
        label: "net.WriteAll",
        kind: "Function",
        detail: "task WriteAll(writer own net.TcpWriter, text String) net.WriteOutcome",
        insertText: "net.WriteAll(${1:writer}, ${2:text})"
    },
    {
        label: "bind.NewValues",
        kind: "Function",
        detail: "fn NewValues() own bind.Values",
        insertText: "bind.NewValues()"
    },
    {
        label: "bind.NewSources",
        kind: "Function",
        detail: "fn NewSources() own bind.Sources",
        insertText: "bind.NewSources()"
    },
    {
        label: "bind.Append",
        kind: "Function",
        detail: "fn Append(&values collections.List<String>, $value String) void",
        insertText: "bind.Append(&${1:values}, \\$${2:value})"
    },
    {
        label: "bind.JsonObject",
        kind: "Function",
        detail: "fn JsonObject($value json.Value) Result<own json.Object, bind.Error>",
        insertText: "bind.JsonObject(\\$${1:value})"
    },
    {
        label: "bind.JsonSyntaxError",
        kind: "Function",
        detail: "fn JsonSyntaxError(error json.Error) bind.Error",
        insertText: "bind.JsonSyntaxError(${1:error})"
    },
    {
        label: "bind.JsonUnknownField",
        kind: "Function",
        detail: "fn JsonUnknownField(&source json.Object) bind.Error",
        insertText: "bind.JsonUnknownField(&${1:source})"
    },
    {
        label: "bind.CopyJsonText",
        kind: "Function",
        detail: "fn CopyJsonText(&source json.Object, &target bind.Values, jsonKey String, targetKey String, field String) Result<bool, bind.Error>",
        insertText: "bind.CopyJsonText(&${1:source}, &${2:target}, ${3:jsonKey}, ${4:targetKey}, ${5:field})"
    },
    {
        label: "bind.CopyJsonBool",
        kind: "Function",
        detail: "fn CopyJsonBool(&source json.Object, &target bind.Values, jsonKey String, targetKey String, field String) Result<bool, bind.Error>",
        insertText: "bind.CopyJsonBool(&${1:source}, &${2:target}, ${3:jsonKey}, ${4:targetKey}, ${5:field})"
    },
    {
        label: "bind.CopyJsonNumber",
        kind: "Function",
        detail: "fn CopyJsonNumber(&source json.Object, &target bind.Values, jsonKey String, targetKey String, field String) Result<bool, bind.Error>",
        insertText: "bind.CopyJsonNumber(&${1:source}, &${2:target}, ${3:jsonKey}, ${4:targetKey}, ${5:field})"
    },
    {
        label: "bind.CopyJsonTextList",
        kind: "Function",
        detail: "fn CopyJsonTextList(&source json.Object, &target collections.List<String>, jsonKey String, field String) Result<bool, bind.Error>",
        insertText: "bind.CopyJsonTextList(&${1:source}, &${2:target}, ${3:jsonKey}, ${4:field})"
    },
    {
        label: "bind.Values.Set",
        kind: "Method",
        detail: "fn Set(&self, $key String, $value String) void",
        insertText: "Set(\\$${1:key}, \\$${2:value})"
    },
    {
        label: "bind.Values.Value",
        kind: "Method",
        detail: "fn Value(&self, key String) Option<String>",
        insertText: "Value(${1:key})"
    },
    {
        label: "bind.Values.Contains",
        kind: "Method",
        detail: "fn Contains(&self, key String) bool",
        insertText: "Contains(${1:key})"
    },
    {
        label: "bind.Values.Required",
        kind: "Method",
        detail: "fn Required(&self, key String) String",
        insertText: "Required(${1:key})"
    },
    {
        label: "bind.Sources.Set",
        kind: "Method",
        detail: "fn Set(&self, $source String, $key String, $value String) void",
        insertText: "Set(\\$${1:source}, \\$${2:key}, \\$${3:value})"
    },
    {
        label: "json.Object.FirstKey",
        kind: "Method",
        detail: "fn FirstKey(&self) Option<String>",
        insertText: "FirstKey()"
    },
    {
        label: "bind.Sources.Value",
        kind: "Method",
        detail: "fn Value(&self, source String, key String) Option<String>",
        insertText: "Value(${1:source}, ${2:key})"
    },
    {
        label: "bind.Sources.CopyInto",
        kind: "Method",
        detail: "fn CopyInto(&self, &target bind.Values, source String, key String, targetKey String) bool",
        insertText: "CopyInto(&${1:target}, ${2:source}, ${3:key}, ${4:targetKey})"
    },
    {
        label: "net.TcpConnection.PeerAddress",
        kind: "Method",
        detail: "fn PeerAddress(self) Result<String, net.Error>",
        insertText: "PeerAddress()"
    },
    {
        label: "web.NewServer",
        kind: "Function",
        detail: "fn NewServer<E>($address String, port u64, $application own web.Application<E>) Result<own web.Server<E>, net.Error>",
        insertText: "web.NewServer<${1:Error}>(\$${2:address}, ${3:port}, \$${4:application})"
    },
    {
        label: "web.NewRouter",
        kind: "Function",
        detail: "fn NewRouter<E>() own web.Router<E>",
        insertText: "web.NewRouter<${1:Error}>()"
    },
    {
        label: "web.NewRouteTable",
        kind: "Function",
        detail: "fn NewRouteTable() own web.RouteTable",
        insertText: "web.NewRouteTable()"
    },
    {
        label: "web.Empty",
        kind: "Function",
        detail: "fn Empty(status i32) web.Response",
        insertText: "web.Empty(${1:204})"
    },
    {
        label: "web.Text",
        kind: "Function",
        detail: "fn Text(status i32, body String) web.Response",
        insertText: "web.Text(${1:200}, ${2:body})"
    },
    {
        label: "web.Json",
        kind: "Function",
        detail: "fn Json(status i32, body String) web.Response",
        insertText: "web.Json(${1:200}, ${2:body})"
    },
    {
        label: "web.Router.Map",
        kind: "Method",
        detail: "fn Map(&self, method web.Method, $path String, $handler own web.Handler<E>) Result<void, web.RegistrationError>",
        insertText: "Map(${1:.GET}, \"${2:/path}\", \\$${3:handler})"
    },
    {
        label: "web.Router.Use",
        kind: "Method",
        detail: "fn Use(&self, order i32, $middleware fn($web.Request, fn($web.Request) Result<web.Response, web.DispatchError<E>>) Result<web.Response, web.DispatchError<E>>) Result<void, web.MiddlewareRegistrationError>",
        insertText: "Use(${1:10}, \\$${2:middleware})"
    },
    {
        label: "web.Router.UseStateful",
        kind: "Method",
        detail: "fn UseStateful(&self, order i32, $middleware own web.Middleware<E>) Result<void, web.MiddlewareRegistrationError>",
        insertText: "UseStateful(${1:10}, \\$${2:middleware})"
    },
    {
        label: "web.Router.UseGroup",
        kind: "Method",
        detail: "fn UseGroup(&self, $prefix String, order i32, $middleware fn($web.Request, fn($web.Request) Result<web.Response, web.DispatchError<E>>) Result<web.Response, web.DispatchError<E>>) Result<void, web.MiddlewareRegistrationError>",
        insertText: "UseGroup(\"${1:/api}\", ${2:10}, \\$${3:middleware})"
    },
    {
        label: "web.Router.UseGroupStateful",
        kind: "Method",
        detail: "fn UseGroupStateful(&self, $prefix String, order i32, $middleware own web.Middleware<E>) Result<void, web.MiddlewareRegistrationError>",
        insertText: "UseGroupStateful(\"${1:/api}\", ${2:10}, \\$${3:middleware})"
    },
    {
        label: "web.Router.UseRoute",
        kind: "Method",
        detail: "fn UseRoute(&self, method web.Method, $path String, order i32, $middleware fn($web.Request, fn($web.Request) Result<web.Response, web.DispatchError<E>>) Result<web.Response, web.DispatchError<E>>) Result<void, web.MiddlewareRegistrationError>",
        insertText: "UseRoute(${1:.GET}, \"${2:/path}\", ${3:10}, \\$${4:middleware})"
    },
    {
        label: "web.Router.UseRouteStateful",
        kind: "Method",
        detail: "fn UseRouteStateful(&self, method web.Method, $path String, order i32, $middleware own web.Middleware<E>) Result<void, web.MiddlewareRegistrationError>",
        insertText: "UseRouteStateful(${1:.GET}, \"${2:/path}\", ${3:10}, \\$${4:middleware})"
    },
    {
        label: "web.RouteTable.Add",
        kind: "Method",
        detail: "fn Add(&self, id u64, method web.Method, path String) Result<void, web.RegistrationError>",
        insertText: "Add(${1:id}, ${2:.GET}, ${3:path})"
    },
    {
        label: "web.RouteTable.Match",
        kind: "Method",
        detail: "fn Match(&self, method web.Method, path String) Result<web.RouteMatch, web.MatchError>",
        insertText: "Match(${1:.GET}, ${2:path})"
    },
    {
        label: "web.Request.Param",
        kind: "Method",
        detail: "fn Param(&self, name String) Option<String>",
        insertText: "Param(${1:name})"
    },
    {
        label: "web.Request.Query",
        kind: "Method",
        detail: "fn Query(name String) Option<String>",
        insertText: "Query(${1:name})"
    },
    {
        label: "web.Request.Header",
        kind: "Method",
        detail: "fn Header(&self, name String) Option<String>",
        insertText: "Header(${1:name})"
    },
    {
        label: "web.Request.Form",
        kind: "Method",
        detail: "fn Form(name String) Option<String>",
        insertText: "Form(${1:name})"
    },
    {
        label: "web.Request.IsJSON",
        kind: "Method",
        detail: "fn IsJSON(&self) bool",
        insertText: "IsJSON()"
    },
    {
        label: "web.Response.Header",
        kind: "Method",
        detail: "fn Header(&self, name String) Option<String>",
        insertText: "Header(${1:name})"
    },
    {
        label: "web.Response.SetHeader",
        kind: "Method",
        detail: "fn SetHeader(&self, name String, value String) void",
        insertText: "SetHeader(${1:name}, ${2:value})"
    },
    {
        label: "web.Response.AddHeader",
        kind: "Method",
        detail: "fn AddHeader(&self, name String, value String) void",
        insertText: "AddHeader(${1:name}, ${2:value})"
    },
    {
        label: "web.Application.ErrorResponse",
        kind: "Method",
        detail: "fn ErrorResponse(self, $error E) Result<web.Response, E>",
        insertText: "ErrorResponse(\$${1:error})"
    },
    {
        label: "web.Server.ConfigureCORS",
        kind: "Method",
        detail: "fn ConfigureCORS(&self, allowOrigins [String]) void",
        insertText: "ConfigureCORS(${1:allowOrigins})"
    },
    {
        label: "web.Server.ServeOne",
        kind: "Method",
        detail: "fn ServeOne($self) Task<own web.ServeOutcome<E>>",
        insertText: "ServeOne()"
    },
    {
        label: "validation.NewErrors",
        kind: "Function",
        detail: "fn NewErrors() own validation.Errors",
        insertText: "validation.NewErrors()"
    },
    {
        label: "validation.IsEmail",
        kind: "Function",
        detail: "fn IsEmail(value String) bool",
        insertText: "validation.IsEmail(${1:value})"
    },
    {
        label: "validation.IsPattern",
        kind: "Function",
        detail: "fn IsPattern(value String, expression String) bool",
        insertText: "validation.IsPattern(${1:value}, \"${2:^.+$}\")"
    },
    {
        label: "pattern.IsValid",
        kind: "Function",
        detail: "fn IsValid(expression String) bool",
        insertText: "pattern.IsValid(\"${1:^.+$}\")"
    },
    {
        label: "pattern.Matches",
        kind: "Function",
        detail: "fn Matches(value String, expression String) bool",
        insertText: "pattern.Matches(${1:value}, \"${2:^.+$}\")"
    },
    {
        label: "validation.Errors.Add",
        kind: "Method",
        detail: "fn Add(&self, kind validation.ErrorKind, field String, message String) void",
        insertText: "Add(${1:.Custom}, \"${2:Field}\", \"${3:message}\")"
    },
    {
        label: "validation.Errors.IsEmpty",
        kind: "Method",
        detail: "fn IsEmpty(self) bool",
        insertText: "IsEmpty()"
    },
    {
        label: "validation.Errors.Len",
        kind: "Method",
        detail: "fn Len(self) i32",
        insertText: "Len()"
    },
    {
        label: "validation.Errors.TakeFirst",
        kind: "Method",
        detail: "fn TakeFirst(&self) Option<validation.Error>",
        insertText: "TakeFirst()"
    },
    ...formatScalarCompletions,
    ...parseFloatCompletions,
    {
        label: "json.Parse",
        kind: "Function",
        detail: "fn Parse(source String) Result<json.Value, json.Error>",
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
        label: "time.MonotonicNow",
        kind: "Function",
        detail: "fn MonotonicNow() time.MonotonicInstant",
        insertText: "time.MonotonicNow()"
    },
    {
        label: "time.Nanoseconds",
        kind: "Function",
        detail: "fn Nanoseconds(value u64) time.Duration",
        insertText: "time.Nanoseconds(${1:value})"
    },
    {
        label: "time.Milliseconds",
        kind: "Function",
        detail: "fn Milliseconds(value u64) time.Duration",
        insertText: "time.Milliseconds(${1:value})"
    },
    {
        label: "time.Seconds",
        kind: "Function",
        detail: "fn Seconds(value u64) time.Duration",
        insertText: "time.Seconds(${1:value})"
    },
    {
        label: "time.Duration.Parse",
        kind: "Function",
        detail: "fn Parse(value String) Result<time.Duration, time.DurationError>",
        documentation: "Parse a signed composite duration with nanosecond precision.",
        insertText: "time.Duration.Parse(${1:value})"
    },
    {
        label: "time.Duration.Seconds",
        kind: "Method",
        detail: "fn Seconds(self) i64",
        insertText: "Seconds()"
    },
    {
        label: "time.Duration.Nanoseconds",
        kind: "Method",
        detail: "fn Nanoseconds(self) i64",
        insertText: "Nanoseconds()"
    },
    {
        label: "time.Duration.IsNegative",
        kind: "Method",
        detail: "fn IsNegative(self) bool",
        insertText: "IsNegative()"
    },
    {
        label: "time.Instant.FormatUtc",
        kind: "Method",
        detail: "fn FormatUtc(self) Result<String, time.Error>",
        insertText: "FormatUtc()"
    },
    {
        label: "concurrent.Transferable",
        kind: "Property",
        detail: "attribute Transferable() targets(struct)",
        insertText: "concurrent.Transferable()"
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
        detail: "fn Token(self) concurrent.Cancellation",
        insertText: "Token()"
    },
    {
        label: "concurrent.CancellationSource.Cancel",
        kind: "Method",
        detail: "fn Cancel(self) void",
        insertText: "Cancel()"
    },
    {
        label: "concurrent.Cancellation.IsRequested",
        kind: "Method",
        detail: "fn IsRequested(self) bool",
        insertText: "IsRequested()"
    },
    {
        label: "worker.NewSupervisor",
        kind: "Function",
        detail: "fn NewSupervisor() own worker.Supervisor",
        insertText: "worker.NewSupervisor()"
    },
    {
        label: "hosting.NewHost",
        kind: "Function",
        detail: "fn NewHost<E>() own hosting.Host<E>",
        insertText: "hosting.NewHost<${1:E}>()"
    },
    {
        label: "hosting.Host.Add",
        kind: "Method",
        detail: "fn Add(&self, $hosted own HostedService<E>) Result<void, RegistrationError>",
        insertText: "Add(\$${1:hosted})"
    },
    {
        label: "hosting.Host.Start",
        kind: "Method",
        detail: "fn Start(&self) Result<void, StartError<E>>",
        insertText: "Start()"
    },
    {
        label: "hosting.Host.AddBackground",
        kind: "Method",
        detail: "fn AddBackground(&self, $background own BackgroundService<E>) Result<void, RegistrationError>",
        insertText: "AddBackground(\$${1:background})"
    },
    {
        label: "hosting.Host.OnStart",
        kind: "Method",
        detail: "fn OnStart(&self, $hook fn() void) Result<void, RegistrationError>",
        insertText: "OnStart(\$${1:hook})"
    },
    {
        label: "hosting.Host.OnStop",
        kind: "Method",
        detail: "fn OnStop(&self, $hook fn() void) Result<void, RegistrationError>",
        insertText: "OnStop(\$${1:hook})"
    },
    {
        label: "hosting.Host.Shutdown",
        kind: "Method",
        detail: "fn Shutdown(&self) own collections.List<E>",
        insertText: "Shutdown()"
    },
    {
        label: "hosting.Host.NextBackground",
        kind: "Method",
        detail: "fn NextBackground($self) Task<own BackgroundNext<E>>",
        insertText: "NextBackground()"
    },
    {
        label: "hosting.Run",
        kind: "Function",
        detail: "fn Run<E>($host own Host<E>, $stop Receiver<void>) Task<Result<own RunReport<E>, StartError<E>>>",
        insertText: "hosting.Run(\$${1:host}, \$${2:stop})"
    },
    {
        label: "health.NewRegistry",
        kind: "Function",
        detail: "fn NewRegistry<D>() own health.Registry<D>",
        insertText: "health.NewRegistry<${1:D}>()"
    },
    {
        label: "health.NewReport",
        kind: "Function",
        detail: "fn NewReport<D>(status health.Status, $details D) health.Report<D>",
        insertText: "health.NewReport(${1:status}, \$${2:details})"
    },
    {
        label: "health.StatusText",
        kind: "Function",
        detail: "fn StatusText(status health.Status) String",
        insertText: "health.StatusText(${1:status})"
    },
    {
        label: "health.Registry.Register",
        kind: "Method",
        detail: "fn Register(&self, $name String, $checker own Checker<D>) Result<void, RegistrationError>",
        insertText: "Register(\$${1:name}, \$${2:checker})"
    },
    {
        label: "health.Registry.CheckAll",
        kind: "Method",
        detail: "fn CheckAll(&self, cancellation Cancellation) Result<own collections.List<NamedReport<D>>, CheckError>",
        insertText: "CheckAll(${1:cancellation})"
    },
    {
        label: "plugin.LoadNative",
        kind: "Function",
        detail: "fn LoadNative(path String) Result<own plugin.NativePlugin, plugin.Error>",
        insertText: "plugin.LoadNative(${1:path})"
    },
    {
        label: "plugin.NewRegistry",
        kind: "Function",
        detail: "fn NewRegistry() own plugin.Registry",
        insertText: "plugin.NewRegistry()"
    },
    {
        label: "plugin.NewFactoryRegistry",
        kind: "Function",
        detail: "fn NewFactoryRegistry() own plugin.FactoryRegistry",
        insertText: "plugin.NewFactoryRegistry()"
    },
    {
        label: "plugin.NewExecSandbox",
        kind: "Function",
        detail: "fn NewExecSandbox(path String) Result<own plugin.ExecSandbox, plugin.SandboxError>",
        insertText: "plugin.NewExecSandbox(${1:path})"
    },
    {
        label: "plugin.StartSandbox",
        kind: "Function",
        detail: "task StartSandbox($sandbox own plugin.ExecSandbox, deadline time.Duration) plugin.SandboxStartOutcome",
        insertText: "plugin.StartSandbox(\$${1:sandbox}, ${2:deadline})"
    },
    {
        label: "plugin.StopSandbox",
        kind: "Function",
        detail: "task StopSandbox($sandbox own plugin.ExecSandbox, deadline time.Duration) plugin.SandboxStopOutcome",
        insertText: "plugin.StopSandbox(\$${1:sandbox}, ${2:deadline})"
    },
    {
        label: "plugin.FactoryRegistry.Register",
        kind: "Method",
        detail: "fn Register(&self, name String, $factory fn() own plugin.Plugin) Result<bool, own plugin.FactoryRegistrationFailure>",
        insertText: "Register(${1:name}, \$${2:factory})"
    },
    {
        label: "plugin.FactoryRegistry.Create",
        kind: "Method",
        detail: "fn Create(&self, name String) Result<own plugin.Plugin, plugin.FactoryErrorKind>",
        insertText: "Create(${1:name})"
    },
    {
        label: "plugin.ExecSandbox.Argument",
        kind: "Method",
        detail: "fn Argument(&self, argument String) Result<bool, plugin.SandboxError>",
        insertText: "Argument(${1:argument})"
    },
    {
        label: "plugin.ExecSandbox.IsRunning",
        kind: "Method",
        detail: "fn IsRunning(self) bool",
        insertText: "IsRunning()"
    },
    {
        label: "plugin.ExecSandbox.Close",
        kind: "Method",
        detail: "fn Close(&self) bool",
        insertText: "Close()"
    },
    {
        label: "plugin.NativePlugin.Name",
        kind: "Method",
        detail: "fn Name(self) String",
        insertText: "Name()"
    },
    {
        label: "plugin.NativePlugin.Start",
        kind: "Method",
        detail: "fn Start(&self) Result<bool, plugin.Error>",
        insertText: "Start()"
    },
    {
        label: "plugin.NativePlugin.Stop",
        kind: "Method",
        detail: "fn Stop(&self) Result<bool, plugin.Error>",
        insertText: "Stop()"
    },
    {
        label: "plugin.NativePlugin.Close",
        kind: "Method",
        detail: "fn Close(&self) Result<bool, plugin.Error>",
        insertText: "Close()"
    },
    {
        label: "plugin.NativePlugin.IsRunning",
        kind: "Method",
        detail: "fn IsRunning(self) bool",
        insertText: "IsRunning()"
    },
    {
        label: "plugin.Registry.Register",
        kind: "Method",
        detail: "fn Register(&self, $plugin own plugin.Plugin) Result<bool, own plugin.RegistrationFailure>",
        insertText: "Register(\$${1:plugin})"
    },
    {
        label: "plugin.Registry.StartAll",
        kind: "Method",
        detail: "fn StartAll(&self) Result<bool, own plugin.StartFailure>",
        insertText: "StartAll()"
    },
    {
        label: "plugin.Registry.StopAll",
        kind: "Method",
        detail: "fn StopAll(&self) own collections.List<plugin.NamedError>",
        insertText: "StopAll()"
    },
    {
        label: "plugin.Registry.Names",
        kind: "Method",
        detail: "fn Names(&self) own collections.List<String>",
        insertText: "Names()"
    },
    {
        label: "worker.Supervisor.Start",
        kind: "Method",
        detail: "fn Start(self, pending Task<void>) void",
        insertText: "Start(${1:pending})"
    },
    {
        label: "worker.Supervisor.Shutdown",
        kind: "Method",
        detail: "fn Shutdown($self) void",
        insertText: "Shutdown()"
    },
    {
        label: "worker.Supervisor.Cancel",
        kind: "Method",
        detail: "fn Cancel($self) void",
        insertText: "Cancel()"
    },
    {
        label: "worker.NewGroup",
        kind: "Function",
        detail: "fn NewGroup<T>(capacity u64) own worker.Group<T>",
        insertText: "worker.NewGroup<${1:T}>(${2:capacity})"
    },
    {
        label: "worker.Group.Add",
        kind: "Method",
        detail: "fn Add(&self, $pending Task<T>) Result<void, GroupError>",
        insertText: "Add(\$${1:pending})"
    },
    {
        label: "worker.Group.Next",
        kind: "Method",
        detail: "fn Next($self) Task<own GroupNext<T>>",
        insertText: "Next()"
    },
    {
        label: "worker.Group.WaitOrStop",
        kind: "Method",
        detail: "fn WaitOrStop($self, $stop Receiver<void>) Task<own GroupWait<T>>",
        insertText: "WaitOrStop(\$${1:stop})"
    },
    {
        label: "worker.Group.Shutdown",
        kind: "Method",
        detail: "fn Shutdown($self) void",
        insertText: "Shutdown()"
    },
    {
        label: "worker.Group.Cancel",
        kind: "Method",
        detail: "fn Cancel($self) void",
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
        detail: "fn Start(self, pending Task<void>) void",
        insertText: "Start(spawn ${1:work}(${2}))"
    },
    {
        label: "worker.Pool.Shutdown",
        kind: "Method",
        detail: "fn Shutdown($self) void",
        insertText: "Shutdown()"
    },
    {
        label: "worker.Pool.Cancel",
        kind: "Method",
        detail: "fn Cancel($self) void",
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
    { label: "NumberError.OutOfRange", kind: "EnumMember", insertText: "NumberError.OutOfRange" },
    { label: "NumberError.NonFinite", kind: "EnumMember", insertText: "NumberError.NonFinite" },
    { label: "NumberError.PrecisionLoss", kind: "EnumMember", insertText: "NumberError.PrecisionLoss" },
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
        label: "clone",
        kind: "Method",
        detail: "fn clone() Sender<T>",
        insertText: "clone()"
    },
    {
        label: "range",
        kind: "Function",
        detail: "fn range(start i32, stop i32, step i32) Range",
        insertText: "range(${1:start}, ${2:stop}, step = ${3:1})"
    },
    {
        label: "expect",
        kind: "Function",
        detail: "fn expect(condition bool) void",
        insertText: "expect(${1:condition})"
    },
    {
        label: "fail",
        kind: "Function",
        detail: "fn fail<T>(value T) never",
        insertText: "fail(${1:value})"
    },
    {
        label: "pass",
        kind: "Function",
        detail: "fn pass() void",
        insertText: "pass()"
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

function enumPayloadName(payload) {
    if (!payload) {
        return null;
    }
    const match = payload.trim().match(/^([A-Za-z_][A-Za-z0-9_]*)\s+(.+)$/);
    if (!match || ["edit", "fn", "own", "view"].includes(match[1])) {
        return "value";
    }
    return match[1];
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
        )].map((variant) => ({
            name: variant[1],
            payload: Boolean(variant[2]),
            payloadName: enumPayloadName(variant[2])
        }))
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
                            ? `${qualified}.${variant.name}(\${${declaration.typeParameters.length + 1}:${variant.payloadName}})`
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
    const bindings = /\b(?:const|var)\s+([A-Za-z_][A-Za-z0-9_]*)/g;
    const loopBindings = /\bfor\s+&?\s*([A-Za-z_][A-Za-z0-9_]*)(?:\s*,\s*&?\s*([A-Za-z_][A-Za-z0-9_]*))?\s+in\b/g;
    const structPatterns = /\bconst\s+[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*\s*\{([^}]*)\}\s*=/g;
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
            const payloadName = enumPayloadName(variant[2]);
            completions.push({
                label: `${name}.${variant[1]}`,
                kind: "EnumMember",
                detail: `Variant of ${name}`,
                insertText: variant[2]
                    ? `${qualifier}.${variant[1]}(\${${typeParameters.length + 1}:${payloadName}})`
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

    while ((match = loopBindings.exec(masked)) !== null) {
        const names = match[2] === undefined ? [match[1]] : [match[1], match[2]];
        for (const name of names) {
            completions.push({
                label: name,
                kind: "Variable",
                detail: "Loop binding"
            });
        }
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
