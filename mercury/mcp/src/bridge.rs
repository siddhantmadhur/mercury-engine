// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! MCP transport bridge. The C++ editor owns the tool implementations; this
//! crate owns the HTTP transport, the tool catalogue (`tools/list` and the
//! read-only gate) and the JSON handshake with `ToolHost`.

use std::{io, sync::Arc, time::Duration};

use axum::{
    Router,
    body::Body,
    extract::State,
    http::{Request, StatusCode, header::AUTHORIZATION, header::HOST, header::ORIGIN},
    middleware::{self, Next},
    response::{IntoResponse, Response},
};
use cxx::SharedPtr;
use rmcp::{
    ErrorData as McpError, ServerHandler,
    model::{
        CallToolRequestParams, CallToolResponse, CallToolResult, ContentBlock,
        Implementation, ListResourceTemplatesResult, ListResourcesResult, ListToolsResult,
        PaginatedRequestParams, ReadResourceRequestParams, ReadResourceResponse,
        ReadResourceResult, Resource, ResourceContents, ResourceTemplate, ServerCapabilities,
        ServerConfig, Tool, ToolAnnotations,
    },
    service::RequestContext,
    transport::streamable_http_server::{
        StreamableHttpServerConfig, StreamableHttpService,
        session::local::LocalSessionManager,
    },
};
use serde_json::Value;
use tokio::sync::oneshot;

use crate::tools::{self, ToolSpec};

#[cxx::bridge(namespace = "Mercury::mcp")]
mod ffi {
    /// Configuration handed over from C++ when the editor starts the server.
    struct ServerConfig {
        port: u16,
        read_only: bool,
        token: String,
        /// The editor's version, reported by get_server_info and initialize;
        /// empty means fall back to this crate's version.
        editor_version: String,
    }

    /// A PNG image the editor captured for a tool result.
    struct ImagePng {
        data: Vec<u8>,
        label: String,
    }

    extern "Rust" {
        type Server;
        type PendingReply;

        #[cxx_name = "StartServer"]
        fn start_server(cfg: &ServerConfig, host: SharedPtr<ToolHost>) -> Result<Box<Server>>;
        fn stop(self: &mut Server);
        fn url(self: &Server) -> String;
        #[cxx_name = "CompleteReply"]
        fn complete_reply(
            reply: Box<PendingReply>,
            ok: bool,
            result_json: String,
            images: Vec<ImagePng>,
        );
    }

    unsafe extern "C++" {
        // Resolved against the `editor/` include dir (and `sdk/include/editor`
        // in the installed SDK).
        include!("mcp/tool_host.h");

        type ToolHost;

        #[cxx_name = "Invoke"]
        // Only the ABI is declared; dispatch routes through the `Host` trait
        // below so the tests can swap in a mock without touching the bridge.
        fn invoke(self: &ToolHost, tool: &str, args_json: &str, reply: Box<PendingReply>);
    }
}

// The editor implements Invoke so that it is thread-safe (the MCP runtime
// calls it from this crate's worker threads, never the UI thread) and so that
// it only queues the request onto the UI thread with CallAfter, guarded by an
// alive token. That is what makes sharing the host across the runtime's
// threads sound.
unsafe impl Send for ffi::ToolHost {}
unsafe impl Sync for ffi::ToolHost {}

/// The editor-side tool host, abstracted so the tests can swap in a mock.
///
/// Implementations forward one tool invocation to the editor and own `reply`
/// until they complete it exactly once with `complete_reply`. A reply handle
/// that is dropped without being completed simply leaves the caller waiting
/// until the tool timeout.
pub(crate) trait Host: Send + Sync {
    fn invoke(&self, tool: &str, args_json: &str, reply: Box<PendingReply>);
}

impl Host for SharedPtr<ffi::ToolHost> {
    fn invoke(&self, tool: &str, args_json: &str, reply: Box<PendingReply>) {
        // Named `ToolHost::invoke`, not `self.invoke`: the inherent extern
        // "C++" method would otherwise resolve first (after deref) and shadow
        // this trait method, recursing forever. cxx generates the Invoke
        // binding safe (the C++ side owns the thread-safety contract).
        ffi::ToolHost::invoke(self, tool, args_json, reply)
    }
}

/// Result handed back to the Rust HTTP handler when C++ calls `CompleteReply`.
struct Reply {
    ok: bool,
    result_json: String,
    images: Vec<ffi::ImagePng>,
}

/// Opaque reply handle owned by C++ between `Invoke` and `CompleteReply`.
pub struct PendingReply {
    sender: oneshot::Sender<Reply>,
}

/// How long a call may wait on the editor before the transport answers
/// "editor did not respond", for the calls that have no tool spec (the
/// reserved resource tools). A tool's own spec carries its timeout
/// (`ToolSpec::timeout_secs`, this same value unless the tool asks for more).
const DEFAULT_TOOL_TIMEOUT: Duration = Duration::from_secs(tools::DEFAULT_TIMEOUT_SECS);

/// Reserved tool names the transport answers through the same `Invoke`
/// handshake but that no editor handler is registered for; they carry the
/// editor's resources instead. They are not part of the tool catalogue, so a
/// client cannot reach them via `tools/call`.
const RESOURCES_LIST: &str = "__resources/list";
const RESOURCES_READ: &str = "__resources/read";

/// Owns the transport runtime so it lives exactly as long as the C++ server.
/// `stop` is idempotent and `Drop` falls back to it, so both an explicit
/// `StopServer` from C++ and the final destruction shut the runtime down.
pub struct Server {
    transport: Transport,
}

impl Server {
    fn stop(&mut self) {
        self.transport.stop();
    }

    fn url(&self) -> String {
        self.transport.url()
    }
}

impl Drop for Server {
    fn drop(&mut self) {
        self.stop();
    }
}

/// The HTTP transport of one server instance, testable without a C++ host.
struct Transport {
    runtime: Option<tokio::runtime::Runtime>,
    shutdown: Option<oneshot::Sender<()>>,
    url: String,
}

impl Transport {
    fn stop(&mut self) {
        if let Some(shutdown) = self.shutdown.take() {
            let _ = shutdown.send(());
        }
        if let Some(runtime) = self.runtime.take() {
            // Deliberately not graceful: in-flight tool calls may be parked on
            // the editor's UI thread for a long time, and `stop` has to come
            // back quickly (its Drop path runs at editor shutdown). Abort them
            // after a short grace period; the pending reply handles are simply
            // dropped.
            runtime.shutdown_timeout(Duration::from_millis(500));
        }
    }

    fn url(&self) -> String {
        self.url.clone()
    }
}

/// The rmcp server handler: catalogue, dispatch and resource forwarding.
#[derive(Clone)]
struct BridgeHandler {
    host: Arc<dyn Host>,
    read_only: bool,
    /// The editor's version, empty until the editor hands one over.
    editor_version: String,
    /// Filled in once the listener is bound (the port may be ephemeral); it
    /// is the URL `get_server_info` reports.
    url: String,
    /// Test-only override: when set it replaces every timeout, the specs'
    /// included. Production leaves it `None`.
    tool_timeout: Option<Duration>,
    catalogue: Vec<ToolSpec>,
}

impl BridgeHandler {
    fn new(
        host: Arc<dyn Host>,
        read_only: bool,
        editor_version: String,
        tool_timeout: Option<Duration>,
    ) -> Self {
        BridgeHandler {
            host,
            read_only,
            editor_version,
            url: String::new(),
            tool_timeout,
            catalogue: tools::all(),
        }
    }

    /// The version get_server_info and initialize report: the editor's when
    /// it handed one over, this crate's otherwise.
    fn version(&self) -> &str {
        if self.editor_version.is_empty() {
            tools::core::SERVER_VERSION
        } else {
            &self.editor_version
        }
    }

    /// Forward one invocation to the host and wait for its reply.
    async fn invoke(
        &self,
        tool: &str,
        args_json: &str,
        timeout: Duration,
    ) -> Result<Reply, McpError> {
        let (sender, receiver) = oneshot::channel();
        self.host
            .invoke(tool, args_json, Box::new(PendingReply { sender }));
        match tokio::time::timeout(self.tool_timeout.unwrap_or(timeout), receiver).await {
            Ok(Ok(reply)) => Ok(reply),
            // The editor timed out, or dropped the handle without completing
            // it (for example while the server is shutting down): one failure.
            _ => Err(McpError::internal_error("editor did not respond", None)),
        }
    }

    /// Forward one invocation and parse its successful result as JSON.
    async fn invoke_json(&self, tool: &str, args_json: &str) -> Result<Value, McpError> {
        let reply = self.invoke(tool, args_json, DEFAULT_TOOL_TIMEOUT).await?;
        if !reply.ok {
            return Err(McpError::internal_error(
                format!("{tool} failed: {}", reply.result_json),
                None,
            ));
        }
        serde_json::from_str(&reply.result_json).map_err(|error| {
            McpError::internal_error(
                format!("{tool} returned invalid JSON: {error}"),
                None,
            )
        })
    }
}

impl ServerHandler for BridgeHandler {
    fn get_info(&self) -> ServerConfig {
        ServerConfig::new(
            ServerCapabilities::builder()
                .enable_tools()
                .enable_resources()
                .build(),
        )
        .with_server_info(Implementation::new("mercury", self.version()))
    }

    fn get_tool(&self, name: &str) -> Option<Tool> {
        self.catalogue
            .iter()
            .find(|tool| tool.name == name)
            .map(rmcp_tool)
    }

    async fn list_tools(
        &self,
        _request: Option<PaginatedRequestParams>,
        _context: RequestContext<rmcp::RoleServer>,
    ) -> Result<ListToolsResult, McpError> {
        Ok(ListToolsResult::with_all_items(
            self.catalogue.iter().map(rmcp_tool).collect(),
        ))
    }

    async fn call_tool(
        &self,
        mut request: CallToolRequestParams,
        _context: RequestContext<rmcp::RoleServer>,
    ) -> Result<CallToolResponse, McpError> {
        // Extract the arguments first: `take` needs a `mut` borrow while
        // `name` below borrows the request for the rest of the dispatch.
        let args_json = serde_json::to_string(&request.arguments.take().unwrap_or_default())
            .unwrap_or_else(|_| "{}".to_owned());
        let name = request.name.as_ref();

        // Answered entirely in Rust; no editor round-trip.
        if name == "get_server_info" {
            let info = serde_json::json!({
                "version": self.version(),
                "url": self.url,
                "read_only": self.read_only,
                "tool_count": self.catalogue.len(),
            });
            return Ok(CallToolResult::structured(info).into());
        }

        let Some(spec) = self.catalogue.iter().find(|tool| tool.name == name) else {
            return Err(McpError::invalid_params(format!("unknown tool '{name}'"), None));
        };
        if self.read_only && !spec.is_read_only() {
            return Ok(
                CallToolResult::error(vec![ContentBlock::text("read-only mode")]).into(),
            );
        }

        let timeout = Duration::from_secs(spec.timeout_secs);
        match self.invoke(spec.name, &args_json, timeout).await {
            Ok(reply) => Ok(reply_to_result(reply).into()),
            Err(_) => Ok(
                CallToolResult::error(vec![ContentBlock::text("editor did not respond")])
                    .into(),
            ),
        }
    }

    async fn list_resources(
        &self,
        _request: Option<PaginatedRequestParams>,
        _context: RequestContext<rmcp::RoleServer>,
    ) -> Result<ListResourcesResult, McpError> {
        let payload = self.invoke_json(RESOURCES_LIST, "{}").await?;
        Ok(ListResourcesResult::with_all_items(parse_resources(
            &payload,
        )?))
    }

    async fn list_resource_templates(
        &self,
        _request: Option<PaginatedRequestParams>,
        _context: RequestContext<rmcp::RoleServer>,
    ) -> Result<ListResourceTemplatesResult, McpError> {
        let payload = self.invoke_json(RESOURCES_LIST, "{}").await?;
        Ok(ListResourceTemplatesResult::with_all_items(
            parse_resource_templates(&payload)?,
        ))
    }

    async fn read_resource(
        &self,
        request: ReadResourceRequestParams,
        _context: RequestContext<rmcp::RoleServer>,
    ) -> Result<ReadResourceResponse, McpError> {
        let args = serde_json::json!({ "uri": request.uri });
        let payload = self.invoke_json(RESOURCES_READ, &args.to_string()).await?;
        Ok(ReadResourceResponse::Complete(ReadResourceResult::new(
            parse_resource_contents(&payload)?,
        )))
    }
}

/// Map a catalogue entry to the rmcp `Tool` the client sees.
fn rmcp_tool(spec: &ToolSpec) -> Tool {
    let mut tool = Tool::new(spec.name, spec.description, Arc::new(spec.schema()));
    tool.annotations = Some(ToolAnnotations::default().read_only(spec.is_read_only()));
    tool
}

/// Map an editor reply to a tool result: the JSON as text, one image block
/// per captured PNG, and structured content when the JSON is an object.
fn reply_to_result(reply: Reply) -> CallToolResult {
    let mut content = Vec::with_capacity(reply.images.len() + 1);
    content.push(ContentBlock::text(reply.result_json.clone()));
    for image in reply.images {
        content.push(ContentBlock::image(base64_encode(&image.data), "image/png"));
    }
    let structured = serde_json::from_str::<Value>(&reply.result_json)
        .ok()
        .filter(|value| value.is_object());
    let mut result = if reply.ok {
        CallToolResult::success(content)
    } else {
        CallToolResult::error(content)
    };
    result.structured_content = structured;
    result
}

/// Parse the `resources` array of the `__resources/list` payload.
fn parse_resources(payload: &Value) -> Result<Vec<Resource>, McpError> {
    let entries = payload
        .get("resources")
        .and_then(Value::as_array)
        .ok_or_else(|| {
            McpError::internal_error("__resources/list returned no resources array", None)
        })?;
    entries
        .iter()
        .map(|entry| {
            let uri = required_str(entry, "uri", RESOURCES_LIST)?;
            let name = required_str(entry, "name", RESOURCES_LIST)?;
            let mut resource = Resource::new(uri, name);
            if let Some(description) = optional_str(entry, "description") {
                resource = resource.with_description(description);
            }
            if let Some(mime_type) = optional_str(entry, "mimeType") {
                resource = resource.with_mime_type(mime_type);
            }
            Ok(resource)
        })
        .collect()
}

/// Parse the `templates` array of the `__resources/list` payload.
fn parse_resource_templates(payload: &Value) -> Result<Vec<ResourceTemplate>, McpError> {
    let entries = payload
        .get("templates")
        .and_then(Value::as_array)
        .ok_or_else(|| {
            McpError::internal_error("__resources/list returned no templates array", None)
        })?;
    entries
        .iter()
        .map(|entry| {
            let uri_template = required_str(entry, "uriTemplate", RESOURCES_LIST)?;
            let name = required_str(entry, "name", RESOURCES_LIST)?;
            let mut template = ResourceTemplate::new(uri_template, name);
            if let Some(mime_type) = optional_str(entry, "mimeType") {
                template = template.with_mime_type(mime_type);
            }
            Ok(template)
        })
        .collect()
}

/// Parse the `contents` array of the `__resources/read` payload.
fn parse_resource_contents(payload: &Value) -> Result<Vec<ResourceContents>, McpError> {
    let entries = payload
        .get("contents")
        .and_then(Value::as_array)
        .ok_or_else(|| {
            McpError::internal_error("__resources/read returned no contents array", None)
        })?;
    entries
        .iter()
        .map(|entry| {
            let uri = required_str(entry, "uri", RESOURCES_READ)?;
            let text = required_str(entry, "text", RESOURCES_READ)?;
            let mut content = ResourceContents::text(text, uri);
            if let Some(mime_type) = optional_str(entry, "mimeType") {
                content = content.with_mime_type(mime_type);
            }
            Ok(content)
        })
        .collect()
}

/// A required string field of a payload entry.
fn required_str<'a>(
    entry: &'a Value,
    field: &str,
    tool: &str,
) -> Result<&'a str, McpError> {
    entry.get(field).and_then(Value::as_str).ok_or_else(|| {
        McpError::internal_error(format!("{tool} returned an entry without '{field}'"), None)
    })
}

/// An optional string field of a payload entry.
fn optional_str(entry: &Value, field: &str) -> Option<String> {
    entry.get(field).and_then(Value::as_str).map(str::to_owned)
}

/// RFC 4648 base64 with padding, for PNG image blocks.
fn base64_encode(data: &[u8]) -> String {
    const TABLE: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut encoded = String::with_capacity(data.len().div_ceil(3) * 4);
    for chunk in data.chunks(3) {
        let bytes = [
            chunk[0],
            chunk.get(1).copied().unwrap_or(0),
            chunk.get(2).copied().unwrap_or(0),
        ];
        let bits = u32::from_be_bytes([0, bytes[0], bytes[1], bytes[2]]);
        encoded.push(TABLE[(bits >> 18 & 0x3f) as usize] as char);
        encoded.push(TABLE[(bits >> 12 & 0x3f) as usize] as char);
        encoded.push(if chunk.len() > 1 {
            TABLE[(bits >> 6 & 0x3f) as usize] as char
        } else {
            '='
        });
        encoded.push(if chunk.len() > 2 {
            TABLE[(bits & 0x3f) as usize] as char
        } else {
            '='
        });
    }
    encoded
}

/// Byte comparison that takes the same time for every equal-length input.
fn constant_time_eq(a: &[u8], b: &[u8]) -> bool {
    // The length itself is not a secret (the header is visible on the wire),
    // so an early length check is standard practice.
    if a.len() != b.len() {
        return false;
    }
    let mut diff = 0u8;
    for (byte_a, byte_b) in a.iter().zip(b) {
        diff |= byte_a ^ byte_b;
    }
    diff == 0
}

/// Is this `Host` header the loopback authority this server is bound to?
fn host_is_loopback(header: &str, port: u16) -> bool {
    let Some((host, header_port)) = header.rsplit_once(':') else {
        return false; // no port, or a bare host: reject
    };
    // An IPv6 host never matches the bracket-stripped names below, which is
    // fail-closed on purpose: this server binds 127.0.0.1 only.
    header_port.parse::<u16>() == Ok(port)
        && (host.eq_ignore_ascii_case("127.0.0.1") || host.eq_ignore_ascii_case("localhost"))
}

/// Is this `Origin` an http loopback origin (port optional)?
fn origin_is_loopback(header: &str) -> bool {
    let Some(authority) = header.strip_prefix("http://") else {
        return false;
    };
    let authority = authority.trim_end_matches('/');
    let (host, port) = match authority.split_once(':') {
        Some((host, port)) => (host, Some(port)),
        None => (authority, None),
    };
    let port_ok = port.is_none_or(|port| port.parse::<u16>().is_ok());
    port_ok
        && (host.eq_ignore_ascii_case("127.0.0.1") || host.eq_ignore_ascii_case("localhost"))
}

// Fails closed: anything but an exact `Bearer <token>` is rejected. An empty
// token never reaches here in production (StartServer refuses to start one),
// but this middleware stays fail-closed so a misconfigured server cannot fall
// back to open access. Host/Origin are checked first so a DNS-rebinding
// request is 403'd before it can learn anything about the token (rmcp's own
// `allowed_hosts`/`allowed_origins` check them again inside the service).
async fn secure_transport(
    State((port, token)): State<(u16, String)>,
    request: Request<Body>,
    next: Next,
) -> Response {
    let host_ok = request
        .headers()
        .get(HOST)
        .and_then(|value| value.to_str().ok())
        .is_some_and(|host| host_is_loopback(host, port));
    if !host_ok {
        return StatusCode::FORBIDDEN.into_response();
    }

    // No Origin header is normal for a non-browser client; a present one must
    // be an http loopback origin.
    let origin = request.headers().get(ORIGIN).and_then(|value| value.to_str().ok());
    if origin.is_some_and(|origin| !origin_is_loopback(origin)) {
        return StatusCode::FORBIDDEN.into_response();
    }

    let header = request
        .headers()
        .get(AUTHORIZATION)
        .and_then(|value| value.to_str().ok());
    if header.is_some_and(|value| {
        constant_time_eq(value.as_bytes(), format!("Bearer {token}").as_bytes())
    }) {
        next.run(request).await
    } else {
        StatusCode::UNAUTHORIZED.into_response()
    }
}

fn start_server(
    cfg: &ffi::ServerConfig,
    host: SharedPtr<ffi::ToolHost>,
) -> io::Result<Box<Server>> {
    let handler = BridgeHandler::new(
        Arc::new(host),
        cfg.read_only,
        cfg.editor_version.clone(),
        None,
    );
    let transport = start_transport(cfg.port, cfg.token.clone(), handler)?;
    Ok(Box::new(Server { transport }))
}

fn start_transport(port: u16, token: String, handler: BridgeHandler) -> io::Result<Transport> {
    // An empty token would disable the bearer middleware entirely and expose
    // the editor to every local process; refuse to start instead.
    if token.is_empty() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "the MCP server refuses to start without a bearer token",
        ));
    }

    let runtime = tokio::runtime::Builder::new_multi_thread()
        .worker_threads(2)
        .enable_all()
        .build()?;
    let _guard = runtime.enter();

    let listener = std::net::TcpListener::bind(("127.0.0.1", port))?;
    listener.set_nonblocking(true)?;
    let listener = tokio::net::TcpListener::from_std(listener)?;
    let local = listener.local_addr()?;

    // The handler's URL is only known once the (possibly ephemeral) port is
    // bound; it is reported by get_server_info and by Server::url.
    let mut handler = handler;
    handler.url = format!("http://{local}/mcp");
    let url = handler.url.clone();

    let service = StreamableHttpService::new(
        move || Ok(handler.clone()),
        Arc::new(LocalSessionManager::default()),
        {
            // Defense in depth: rmcp re-checks the Host and Origin headers
            // inside the service with its own parsing, on top of the
            // middleware above. The default `allowed_hosts` already covers
            // 127.0.0.1/localhost/::1; restrict Origin to the loopback
            // http origins the middleware allows. Plain JSON responses keep
            // the raw-HTTP tests (and clients that prefer them) simple.
            let mut config = StreamableHttpServerConfig::default();
            config.json_response = true;
            config.allowed_origins = vec![
                "http://127.0.0.1:*".to_owned(),
                "http://localhost:*".to_owned(),
            ];
            config
        },
    );
    let router = Router::new()
        .nest_service("/mcp", service)
        .layer(middleware::from_fn_with_state(
            (local.port(), token),
            secure_transport,
        ));

    let (shutdown, shutdown_rx) = oneshot::channel::<()>();
    runtime.spawn(async move {
        let _ = axum::serve(listener, router)
            .with_graceful_shutdown(async move {
                let _ = shutdown_rx.await;
            })
            .await;
    });

    Ok(Transport {
        runtime: Some(runtime),
        shutdown: Some(shutdown),
        url,
    })
}

// The box is the cxx ABI (C++ owns the handle between Invoke and
// CompleteReply), not a choice clippy gets to question.
#[allow(clippy::boxed_local)]
fn complete_reply(reply: Box<PendingReply>, ok: bool, result_json: String, images: Vec<ffi::ImagePng>) {
    let _ = reply.sender.send(Reply {
        ok,
        result_json,
        images,
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tools::Mode;
    use std::sync::Mutex;

    use serde_json::json;

    /// One raw HTTP request against the running transport; the style of the
    /// W0-A probe: std::net only, no HTTP client crate.
    struct Probe {
        status: u16,
        body: String,
    }

    /// POST a JSON-RPC body (plus extra headers) to /mcp.
    fn post(
        port: u16,
        host: &str,
        authorization: Option<&str>,
        origin: Option<&str>,
        protocol_version: Option<&str>,
        body: &str,
    ) -> io::Result<Probe> {
        use std::io::{Read, Write};

        let mut request = format!(
            "POST /mcp HTTP/1.1\r\nHost: {host}\r\n\
             Accept: application/json, text/event-stream\r\n\
             Content-Type: application/json\r\n\
             Content-Length: {}\r\nConnection: close\r\n",
            body.len(),
        );
        if let Some(value) = authorization {
            request.push_str("Authorization: Bearer ");
            request.push_str(value);
            request.push_str("\r\n");
        }
        if let Some(value) = origin {
            request.push_str("Origin: ");
            request.push_str(value);
            request.push_str("\r\n");
        }
        if let Some(value) = protocol_version {
            request.push_str("Mcp-Protocol-Version: ");
            request.push_str(value);
            request.push_str("\r\n");
            // The 2026-07-28 protocol requires an Mcp-Method header matching
            // the body's method (SEP-2243), and, for tools/call, an
            // Mcp-Name header carrying the base64-encoded tool name. Take
            // both from the body like a compliant client would.
            let body_json = serde_json::from_str::<serde_json::Value>(body).ok();
            let method = body_json
                .as_ref()
                .and_then(|request| request["method"].as_str().map(str::to_owned));
            if let Some(method) = &method {
                request.push_str("Mcp-Method: ");
                request.push_str(method);
                request.push_str("\r\n");
            }
            if let Some(name) = body_json.as_ref().and_then(|request| {
                let params = &request["params"];
                params["name"].as_str().or_else(|| params["uri"].as_str())
            }) {
                // Plain header-safe names go through as-is; only values that
                // are not header-safe get wrapped in rmcp's `=?base64?...?=`
                // sentinel, and test names are ASCII.
                request.push_str("Mcp-Name: ");
                request.push_str(name);
                request.push_str("\r\n");
            }
        }
        request.push_str("\r\n");
        request.push_str(body);

        let mut stream = std::net::TcpStream::connect(("127.0.0.1", port))?;
        stream.set_read_timeout(Some(Duration::from_secs(5)))?;
        stream.write_all(request.as_bytes())?;
        let mut response = Vec::new();
        stream.read_to_end(&mut response)?;
        let response = String::from_utf8_lossy(&response).into_owned();
        let status = response
            .lines()
            .next()
            .and_then(|line| line.split_whitespace().nth(1))
            .and_then(|status| status.parse().ok())
            .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "no HTTP status line"))?;
        let body = response
            .split_once("\r\n\r\n")
            .map_or("", |(_, body)| body)
            .to_owned();
        Ok(Probe { status, body })
    }

    /// The JSON-RPC payload of a probe response, whether the server answered
    /// with a plain JSON body or an SSE stream (`data:` lines).
    fn payload(probe: &Probe) -> serde_json::Value {
        let body = probe.body.trim();
        if body.starts_with('{') {
            return serde_json::from_str(body).expect("JSON body");
        }
        for line in body.lines() {
            if let Some(data) = line.strip_prefix("data:")
                && let Ok(value) = serde_json::from_str::<serde_json::Value>(data.trim())
            {
                return value;
            }
        }
        panic!("no JSON payload in response body: {:?}", probe.body);
    }

    /// The `result` of a successful JSON-RPC response.
    fn result(probe: &Probe) -> serde_json::Value {
        let payload = payload(probe);
        assert!(payload.get("error").is_none(), "unexpected error: {payload}");
        payload["result"].clone()
    }

    fn rpc(id: u32, method: &str, params: serde_json::Value) -> String {
        json!({"jsonrpc":"2.0","id":id,"method":method,"params":params}).to_string()
    }

    /// The per-request `_meta` the 2026-07-28 protocol requires on every
    /// non-initialize request (SEP-1319).
    fn meta_2026() -> serde_json::Value {
        json!({
            "io.modelcontextprotocol/protocolVersion": "2026-07-28",
            "io.modelcontextprotocol/clientCapabilities": {},
        })
    }

    const INIT: &str = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\
\"params\":{\"protocolVersion\":\"2026-07-28\",\"capabilities\":{},\
\"clientInfo\":{\"name\":\"probe\",\"version\":\"0\"}}}";

    /// A mock editor host. Replies are one-shot: the first `invoke` gets the
    /// queued reply (if any), later ones never complete.
    struct MockHost {
        calls: Mutex<Vec<(String, String)>>,
        reply: Mutex<Option<(bool, String, Vec<ffi::ImagePng>)>>,
    }

    impl MockHost {
        fn replying(ok: bool, result_json: &str, images: Vec<ffi::ImagePng>) -> Arc<Self> {
            Arc::new(MockHost {
                calls: Mutex::new(Vec::new()),
                reply: Mutex::new(Some((ok, result_json.to_owned(), images))),
            })
        }

        fn never_replying() -> Arc<Self> {
            Arc::new(MockHost {
                calls: Mutex::new(Vec::new()),
                reply: Mutex::new(None),
            })
        }

        fn calls(&self) -> Vec<(String, String)> {
            self.calls.lock().unwrap().clone()
        }
    }

    impl Host for MockHost {
        fn invoke(&self, tool: &str, args_json: &str, reply: Box<PendingReply>) {
            self.calls
                .lock()
                .unwrap()
                .push((tool.to_owned(), args_json.to_owned()));
            if let Some((ok, result_json, images)) = self.reply.lock().unwrap().take() {
                complete_reply(reply, ok, result_json, images);
            }
        }
    }

    /// A mock editor whose every reply arrives `delay` after the call, from
    /// another thread: a tool whose editor-side work takes a while.
    struct DelayedHost {
        delay: Duration,
    }

    impl Host for DelayedHost {
        fn invoke(&self, _tool: &str, _args_json: &str, reply: Box<PendingReply>) {
            let delay = self.delay;
            std::thread::spawn(move || {
                std::thread::sleep(delay);
                complete_reply(reply, true, "{\"done\":true}".to_owned(), vec![]);
            });
        }
    }

    fn png(label: &str, data: Vec<u8>) -> ffi::ImagePng {
        ffi::ImagePng { data, label: label.to_owned() }
    }

    /// Start a transport against `host` and return it with the bound port.
    fn start(
        host: Arc<dyn Host>,
        read_only: bool,
        timeout: Duration,
        extra_tools: Vec<ToolSpec>,
    ) -> (Transport, u16) {
        start_with_version(host, read_only, String::new(), timeout, extra_tools)
    }

    /// Like `start`, with an editor version handed over by the editor.
    fn start_with_version(
        host: Arc<dyn Host>,
        read_only: bool,
        editor_version: String,
        timeout: Duration,
        extra_tools: Vec<ToolSpec>,
    ) -> (Transport, u16) {
        let mut handler = BridgeHandler::new(host, read_only, editor_version, Some(timeout));
        handler.catalogue.extend(extra_tools);
        let transport = start_transport(0, "test-token".to_owned(), handler)
            .expect("server starts");
        let port: u16 = transport
            .url
            .split(':')
            .nth(2)
            .and_then(|rest| rest.split('/').next())
            .and_then(|port| port.parse().ok())
            .expect("the URL carries the bound port");
        (transport, port)
    }

    // -- catalogue ---------------------------------------------------------

    #[test]
    fn catalogue_has_the_core_tools() {
        let all = tools::all();
        let names: Vec<&str> = all.iter().map(|tool| tool.name).collect();
        assert_eq!(names[..2], ["ping", "get_server_info"]);
        let mut unique = names.clone();
        unique.sort_unstable();
        unique.dedup();
        assert_eq!(unique.len(), names.len(), "tool names are unique");
        for tool in &all {
            let schema = tool.schema();
            assert_eq!(schema.get("type"), Some(&json!("object")));
        }
    }

    // -- base64 and constant-time compare ----------------------------------

    #[test]
    fn base64_matches_the_rfc_4648_vectors() {
        assert_eq!(base64_encode(b""), "");
        assert_eq!(base64_encode(b"f"), "Zg==");
        assert_eq!(base64_encode(b"fo"), "Zm8=");
        assert_eq!(base64_encode(b"foo"), "Zm9v");
        assert_eq!(base64_encode(b"foob"), "Zm9vYg==");
        assert_eq!(base64_encode(b"fooba"), "Zm9vYmE=");
        assert_eq!(base64_encode(b"foobar"), "Zm9vYmFy");
        // A PNG magic number, as the image blocks use.
        assert_eq!(base64_encode(&[0x89, b'P', b'N', b'G']), "iVBORw==");
    }

    #[test]
    fn constant_time_eq_behaves() {
        assert!(constant_time_eq(b"", b""));
        assert!(constant_time_eq(b"secret", b"secret"));
        assert!(!constant_time_eq(b"secret", b"secreT"));
        assert!(!constant_time_eq(b"secret", b"secre"));
        assert!(!constant_time_eq(b"secret", b"secretx"));
    }

    // -- security middleware ------------------------------------------------

    #[test]
    fn loopback_headers() {
        assert!(host_is_loopback("127.0.0.1:8080", 8080));
        assert!(host_is_loopback("Localhost:8080", 8080));
        assert!(!host_is_loopback("127.0.0.1:8081", 8080));
        assert!(!host_is_loopback("evil.example:8080", 8080));
        assert!(!host_is_loopback("127.0.0.1", 8080));
        assert!(!host_is_loopback("[::1]:8080", 8080));

        assert!(origin_is_loopback("http://127.0.0.1:8080"));
        assert!(origin_is_loopback("http://localhost"));
        assert!(!origin_is_loopback("https://127.0.0.1:8080"));
        assert!(!origin_is_loopback("http://evil.example"));
        assert!(!origin_is_loopback("http://127.0.0.1.evil.example"));
    }

    #[test]
    fn bad_host_and_origin_are_403_before_the_token() {
        let host_mock = MockHost::replying(true, "{}", vec![]);
        let (mut transport, port) = start(host_mock.clone(), false, DEFAULT_TOOL_TIMEOUT, vec![]);

        // A foreign Host is 403 even with the right token.
        let bad_host = post(
            port,
            "evil.example",
            Some("test-token"),
            None,
            None,
            INIT,
        )
        .expect("probe runs");
        // A foreign Origin is 403 even with the right token; an https loopback
        // origin is foreign too.
        let bad_origin = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            Some("http://evil.example"),
            None,
            INIT,
        )
        .expect("probe runs");
        let bad_scheme = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            Some("https://127.0.0.1"),
            None,
            INIT,
        )
        .expect("probe runs");
        // A loopback Origin with the right token gets through the middleware
        // (status is whatever the MCP service replies with, just not 401/403).
        let good = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            Some("http://localhost"),
            None,
            INIT,
        )
        .expect("probe runs");
        transport.stop();

        assert_eq!(bad_host.status, 403, "bad Host must be rejected");
        assert_eq!(bad_origin.status, 403, "bad Origin must be rejected");
        assert_eq!(bad_scheme.status, 403, "non-http Origin must be rejected");
        assert_ne!(good.status, 403, "loopback Origin passes");
        assert_ne!(good.status, 401, "right token passes");
        assert!(
            host_mock.calls().is_empty(),
            "rejected requests must not reach the editor"
        );
    }

    // -- tools flow ---------------------------------------------------------

    #[test]
    fn initialize_and_tools_list() {
        let mock = MockHost::replying(true, "{}", vec![]);
        let (mut transport, port) = start(mock, false, DEFAULT_TOOL_TIMEOUT, vec![]);
        let init = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            None,
            None,
            INIT,
        )
        .expect("initialize runs");
        let list = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            None,
            Some("2026-07-28"),
            &rpc(2, "tools/list", json!({"_meta": meta_2026()})),
        )
        .expect("tools/list runs");
        transport.stop();

        assert_eq!(init.status, 200, "initialize succeeds");
        assert_eq!(
            result(&init)["serverInfo"]["version"],
            json!(env!("CARGO_PKG_VERSION")),
            "initialize reports the crate version without an editor version"
        );
        let names = result(&list)["tools"]
            .as_array()
            .expect("tools array")
            .iter()
            .map(|tool| tool["name"].as_str().expect("tool name").to_owned())
            .collect::<Vec<_>>();
        assert!(
            names.contains(&"ping".to_owned()) && names.contains(&"get_server_info".to_owned()),
            "tools/list must contain the core tools, got {names:?}"
        );
    }

    #[test]
    fn version_reports_the_editor_and_falls_back() {
        // No editor version handed over: the crate's version.
        let mock = MockHost::replying(true, "{}", vec![]);
        let (mut transport, port) = start(mock, true, DEFAULT_TOOL_TIMEOUT, vec![]);
        let info = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            None,
            Some("2026-07-28"),
            &rpc(1, "tools/call", json!({"name":"get_server_info","arguments":{},"_meta":meta_2026()})),
        )
        .expect("call runs");
        transport.stop();
        assert_eq!(
            result(&info)["structuredContent"]["version"],
            json!(env!("CARGO_PKG_VERSION")),
            "an empty editor version falls back to the crate's"
        );

        // The editor's version wins when it hands one over.
        let mock = MockHost::replying(true, "{}", vec![]);
        let (mut transport, port) = start_with_version(
            mock,
            true,
            "9.8.7-editor".to_owned(),
            DEFAULT_TOOL_TIMEOUT,
            vec![],
        );
        let info = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            None,
            Some("2026-07-28"),
            &rpc(1, "tools/call", json!({"name":"get_server_info","arguments":{},"_meta":meta_2026()})),
        )
        .expect("call runs");
        transport.stop();
        assert_eq!(
            result(&info)["structuredContent"]["version"],
            json!("9.8.7-editor")
        );
    }

    #[test]
    fn get_server_info_is_answered_in_rust() {
        let mock = MockHost::replying(true, "{}", vec![]);
        let (mut transport, port) = start(mock, true, DEFAULT_TOOL_TIMEOUT, vec![]);
        let info = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            None,
            Some("2026-07-28"),
            &rpc(1, "tools/call", json!({"name":"get_server_info","arguments":{},"_meta":meta_2026()})),
        )
        .expect("call runs");
        transport.stop();

        assert_eq!(info.status, 200);
        let result = result(&info);
        assert_eq!(result["isError"], false);
        assert_eq!(
            result["structuredContent"]["read_only"],
            json!(true),
            "get_server_info reports the read-only flag"
        );
        assert_eq!(
            result["structuredContent"]["tool_count"],
            json!(tools::all().len())
        );
        assert!(
            result["structuredContent"]["url"]
                .as_str()
                .is_some_and(|url| url.starts_with("http://127.0.0.1:")),
            "get_server_info reports the bound URL"
        );
    }

    #[test]
    fn call_ping_routes_through_the_host() {
        let mock = MockHost::replying(
            true,
            "{\"pong\":true,\"editor_version\":\"9.9.9-mock\"}",
            vec![],
        );
        let (mut transport, port) = start(mock.clone(), false, DEFAULT_TOOL_TIMEOUT, vec![]);
        let call = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            None,
            Some("2026-07-28"),
            &rpc(1, "tools/call", json!({"name":"ping","arguments":{},"_meta":meta_2026()})),
        )
        .expect("call runs");
        transport.stop();

        assert_eq!(call.status, 200);
        let result = result(&call);
        assert_eq!(result["isError"], false);
        assert_eq!(
            result["content"][0]["text"],
            json!("{\"pong\":true,\"editor_version\":\"9.9.9-mock\"}"),
            "the editor's JSON is the text content"
        );
        assert_eq!(result["structuredContent"]["pong"], json!(true));
        assert_eq!(
            mock.calls(),
            vec![("ping".to_owned(), "{}".to_owned())],
            "the call is forwarded to the editor with its arguments"
        );
    }

    #[test]
    fn images_map_to_image_blocks() {
        let mock = MockHost::replying(
            true,
            "{\"pong\":true}",
            vec![png("frame", vec![0x89, b'P', b'N', b'G'])],
        );
        let (mut transport, port) = start(mock, false, DEFAULT_TOOL_TIMEOUT, vec![]);
        let call = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            None,
            Some("2026-07-28"),
            &rpc(1, "tools/call", json!({"name":"ping","arguments":{},"_meta":meta_2026()})),
        )
        .expect("call runs");
        transport.stop();

        let content = result(&call)["content"].as_array().expect("content").clone();
        assert_eq!(content.len(), 2, "text plus one image block");
        assert_eq!(content[0]["type"], json!("text"));
        assert_eq!(content[1]["type"], json!("image"));
        assert_eq!(content[1]["mimeType"], json!("image/png"));
        assert_eq!(
            content[1]["data"], json!("iVBORw=="),
            "the PNG is delivered base64-encoded"
        );
    }

    #[test]
    fn editor_failures_map_to_tool_errors() {
        // ok=false still delivers the JSON, marked as a tool error.
        let failing = MockHost::replying(false, "{\"error\":\"boom\"}", vec![]);
        let (mut transport, port) = start(failing, false, DEFAULT_TOOL_TIMEOUT, vec![]);
        let call = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            None,
            Some("2026-07-28"),
            &rpc(1, "tools/call", json!({"name":"ping","arguments":{},"_meta":meta_2026()})),
        )
        .expect("call runs");
        transport.stop();
        let result = result(&call);
        assert_eq!(result["isError"], true);
        assert_eq!(result["content"][0]["text"], json!("{\"error\":\"boom\"}"));

        // An unknown tool is a protocol error, not a tool error; probe it
        // against its own transport.
        let unknown_mock = MockHost::replying(true, "{}", vec![]);
        let (mut transport, port) = start(unknown_mock, false, DEFAULT_TOOL_TIMEOUT, vec![]);
        let unknown = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            None,
            Some("2026-07-28"),
            &rpc(2, "tools/call", json!({"name":"nope","arguments":{},"_meta":meta_2026()})),
        )
        .expect("call runs");
        transport.stop();
        let unknown_result = payload(&unknown);
        assert_eq!(unknown.status, 200);
        assert!(
            unknown_result.get("error").is_some(),
            "an unknown tool is an MCP error: {unknown_result}"
        );
    }

    /// A Write tool for the read-only gate tests; test-only, never implemented.
    #[derive(serde::Deserialize, schemars::JsonSchema)]
    #[allow(dead_code)] // the schema reads the shape, not the fields
    struct WriteThingArgs {
        path: String,
        content: String,
    }

    fn write_spec() -> ToolSpec {
        ToolSpec::new(
            "write_thing",
            "test-only write tool",
            Mode::Write,
            || tools::schema::<WriteThingArgs>(),
        )
    }

    #[test]
    fn write_tools_are_gated_by_read_only() {
        // Read-only: rejected as a tool error, never forwarded.
        let mock = MockHost::replying(true, "{}", vec![]);
        let (mut transport, port) =
            start(mock.clone(), true, DEFAULT_TOOL_TIMEOUT, vec![write_spec()]);
        let call = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            None,
            Some("2026-07-28"),
            &rpc(1, "tools/call", json!({"name":"write_thing","arguments":{},"_meta":meta_2026()})),
        )
        .expect("call runs");
        transport.stop();
        let rejected = result(&call);
        assert_eq!(rejected["isError"], true);
        assert_eq!(rejected["content"][0]["text"], json!("read-only mode"));
        assert!(
            mock.calls().is_empty(),
            "a read-only server must not forward a Write tool"
        );

        // Not read-only: forwarded like any other tool.
        let open = MockHost::replying(true, "{\"wrote\":true}", vec![]);
        let (mut transport, port) = start(open.clone(), false, DEFAULT_TOOL_TIMEOUT, vec![write_spec()]);
        let call = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            None,
            Some("2026-07-28"),
            &rpc(1, "tools/call", json!({"name":"write_thing","arguments":{},"_meta":meta_2026()})),
        )
        .expect("call runs");
        transport.stop();
        assert_eq!(result(&call)["isError"], false);
        assert_eq!(
            open.calls()[0].0, "write_thing",
            "a writable server forwards the Write tool"
        );
    }

    #[test]
    fn timeout_maps_to_the_tool_error() {
        let mock = MockHost::never_replying();
        let (mut transport, port) =
            start(mock, false, Duration::from_millis(100), vec![]);
        let call = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            None,
            Some("2026-07-28"),
            &rpc(1, "tools/call", json!({"name":"ping","arguments":{},"_meta":meta_2026()})),
        )
        .expect("call runs");
        transport.stop();
        let result = result(&call);
        assert_eq!(result["isError"], true);
        assert_eq!(
            result["content"][0]["text"],
            json!("editor did not respond"),
            "a timed-out call is a tool error"
        );
    }

    /// A test-only tool with its own timeout.
    fn slow_spec(timeout_secs: u64) -> ToolSpec {
        ToolSpec::new(
            "slow_thing",
            "test-only tool with its own timeout",
            Mode::Read,
            || tools::schema::<tools::core::PingArgs>(),
        )
        .with_timeout_secs(timeout_secs)
    }

    /// Calls `slow_thing` against a host that answers after `delay`, with no
    /// test override: the spec's timeout is the only one in play.
    fn call_slow_thing(delay: Duration, timeout_secs: u64) -> serde_json::Value {
        let host: Arc<dyn Host> = Arc::new(DelayedHost { delay });
        let mut handler = BridgeHandler::new(host, false, String::new(), None);
        handler.catalogue.push(slow_spec(timeout_secs));
        let mut transport =
            start_transport(0, "test-token".to_owned(), handler).expect("server starts");
        let port: u16 = transport
            .url
            .split(':')
            .nth(2)
            .and_then(|rest| rest.split('/').next())
            .and_then(|port| port.parse().ok())
            .expect("the URL carries the bound port");
        let call = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            None,
            Some("2026-07-28"),
            &rpc(1, "tools/call", json!({"name":"slow_thing","arguments":{},"_meta":meta_2026()})),
        )
        .expect("call runs");
        transport.stop();
        result(&call)
    }

    #[test]
    fn a_spec_timeout_longer_than_the_reply_is_honoured() {
        // The reply takes 1.5 s; a 4 s spec timeout waits for it.
        let result = call_slow_thing(Duration::from_millis(1500), 4);
        assert_eq!(result["isError"], false, "{result}");
        assert_eq!(result["structuredContent"]["done"], true, "{result}");
    }

    #[test]
    fn a_spec_timeout_shorter_than_the_reply_times_out() {
        // The same 1.5 s reply with a 1 s spec timeout: the call fails at the
        // spec's timeout, not the 30 s default.
        let started = std::time::Instant::now();
        let result = call_slow_thing(Duration::from_millis(1500), 1);
        assert_eq!(result["isError"], true, "{result}");
        assert_eq!(result["content"][0]["text"], json!("editor did not respond"));
        assert!(
            started.elapsed() < Duration::from_secs(10),
            "the call waited for the 30 s default, took {:?}",
            started.elapsed()
        );
    }

    #[test]
    fn specs_default_to_the_default_timeout() {
        // Every tool uses the default reply timeout except build_scripts,
        // which legitimately waits longer for a child process.
        for spec in tools::all() {
            let expected = if spec.name == "build_scripts" {
                300
            } else {
                tools::DEFAULT_TIMEOUT_SECS
            };
            assert_eq!(
                spec.timeout_secs, expected,
                "{} should use a {expected} s timeout",
                spec.name
            );
        }
    }

    #[test]
    fn stop_returns_while_calls_are_in_flight() {
        let mock = MockHost::never_replying();
        // The default 30 s timeout: stopping must not wait for it.
        let (mut transport, port) = start(mock, false, DEFAULT_TOOL_TIMEOUT, vec![]);
        let caller = std::thread::spawn(move || {
            post(
                port,
                &format!("127.0.0.1:{port}"),
                Some("test-token"),
                None,
                Some("2026-07-28"),
                &rpc(1, "tools/call", json!({"name":"ping","arguments":{},"_meta":meta_2026()})),
            )
        });
        std::thread::sleep(Duration::from_millis(100)); // let the call go in flight

        let started = std::time::Instant::now();
        transport.stop();
        let elapsed = started.elapsed();
        assert!(
            elapsed < Duration::from_secs(1),
            "stop must return quickly even with a call in flight, took {elapsed:?}"
        );
        let _ = caller.join(); // the server dropped the connection
    }

    // -- resources ----------------------------------------------------------

    #[test]
    fn resources_parse_from_the_reserved_payload() {
        let payload = json!({
            "resources": [
                {"uri": "mercury://project/settings",
                 "name": "settings",
                 "mimeType": "application/json",
                 "description": "Project settings"},
                {"uri": "mercury://project/assets", "name": "assets"},
            ],
            "templates": [
                {"uriTemplate": "mercury://entity/{id}", "name": "entity",
                 "mimeType": "application/json"},
            ],
        });
        let resources = parse_resources(&payload).expect("resources parse");
        assert_eq!(resources.len(), 2);
        assert_eq!(resources[0].uri, "mercury://project/settings");
        assert_eq!(resources[0].name, "settings");
        assert_eq!(resources[0].mime_type.as_deref(), Some("application/json"));
        assert_eq!(resources[0].description.as_deref(), Some("Project settings"));
        assert_eq!(resources[1].description, None, "optional fields stay absent");

        let templates = parse_resource_templates(&payload).expect("templates parse");
        assert_eq!(templates.len(), 1);
        assert_eq!(templates[0].uri_template, "mercury://entity/{id}");
        assert_eq!(templates[0].name, "entity");
    }

    #[test]
    fn resources_forward_through_the_reserved_names() {
        // One mock answers every reserved call from one canned reply list.
        struct MultiHost {
            replies: Mutex<std::collections::VecDeque<(String, bool, Vec<ffi::ImagePng>)>>,
            calls: Mutex<Vec<String>>,
        }
        impl Host for MultiHost {
            fn invoke(&self, tool: &str, _args: &str, reply: Box<PendingReply>) {
                self.calls.lock().unwrap().push(tool.to_owned());
                let (result_json, ok, images) = self.replies.lock().unwrap().pop_front().unwrap_or_else(
                    || {
                        (
                            format!("{{\"error\":\"no reply for {tool}\"}}"),
                            false,
                            vec![],
                        )
                    },
                );
                complete_reply(reply, ok, result_json, images);
            }
        }

        let list_payload = json!({
            "resources": [{"uri": "mercury://project/settings", "name": "settings"}],
            "templates": [{"uriTemplate": "mercury://entity/{id}", "name": "entity"}],
        })
        .to_string();
        let read_payload = json!({
            "contents": [{"uri": "mercury://project/settings",
                          "mimeType": "application/json", "text": "{}"}],
        })
        .to_string();
        let host = Arc::new(MultiHost {
            replies: Mutex::new(vec![
                (list_payload.clone(), true, vec![]),
                (list_payload.clone(), true, vec![]),
                (read_payload.clone(), true, vec![]),
            ]
            .into_iter()
            .collect()),
            calls: Mutex::new(Vec::new()),
        });
        let (mut transport, port) = start(host.clone(), false, DEFAULT_TOOL_TIMEOUT, vec![]);

        let list = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            None,
            Some("2026-07-28"),
            &rpc(1, "resources/list", json!({"_meta": meta_2026()})),
        )
        .expect("resources/list runs");
        let templates = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            None,
            Some("2026-07-28"),
            &rpc(2, "resources/templates/list", json!({"_meta": meta_2026()})),
        )
        .expect("resources/templates/list runs");
        let read = post(
            port,
            &format!("127.0.0.1:{port}"),
            Some("test-token"),
            None,
            Some("2026-07-28"),
            &rpc(3, "resources/read", json!({"uri":"mercury://project/settings","_meta":meta_2026()})),
        )
        .expect("resources/read runs");
        transport.stop();

        assert_eq!(list.status, 200, "resources/list succeeds");
        let resources = result(&list)["resources"].as_array().expect("resources").clone();
        assert_eq!(resources[0]["uri"], json!("mercury://project/settings"));
        assert_eq!(resources[0]["name"], json!("settings"));

        assert_eq!(templates.status, 200, "resources/templates/list succeeds");
        let templates_result = result(&templates)["resourceTemplates"]
            .as_array()
            .expect("templates")
            .clone();
        assert_eq!(templates_result[0]["uriTemplate"], json!("mercury://entity/{id}"));
        assert_eq!(templates_result[0]["name"], json!("entity"));

        assert_eq!(read.status, 200, "resources/read succeeds: {}", read.body);
        let contents = result(&read)["contents"].as_array().expect("contents").clone();
        assert_eq!(contents[0]["uri"], json!("mercury://project/settings"));
        assert_eq!(contents[0]["mimeType"], json!("application/json"));
        assert_eq!(contents[0]["text"], json!("{}"));

        let calls = host.calls.lock().unwrap().clone();
        assert!(
            calls.contains(&RESOURCES_LIST.to_owned())
                && calls.contains(&RESOURCES_READ.to_owned()),
            "resources go through the reserved Invoke names, got {calls:?}"
        );
    }
}
