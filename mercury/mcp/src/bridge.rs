// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! MCP transport bridge. The C++ editor owns the tool implementations; this
//! crate owns only the HTTP transport and the JSON handshake with `ToolHost`.

use std::{io, time::Duration};

use axum::{
    Router,
    body::Body,
    extract::State,
    http::{Request, StatusCode, header::AUTHORIZATION},
    middleware::{self, Next},
    response::{IntoResponse, Response},
};
use cxx::SharedPtr;
use rmcp::{
    ServerHandler,
    model::ServerCapabilities,
    transport::streamable_http_server::{
        StreamableHttpServerConfig, StreamableHttpService,
        session::local::LocalSessionManager,
    },
};
use tokio::sync::oneshot;

#[cxx::bridge(namespace = "Mercury::mcp")]
mod ffi {
    /// Configuration handed over from C++ when the editor starts the server.
    struct ServerConfig {
        port: u16,
        read_only: bool,
        token: String,
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
        // Only the ABI is declared for now; tool dispatch (Rust -> C++) is
        // wired up in the next work item.
        #[allow(dead_code)]
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

/// Result handed back to the Rust HTTP handler when C++ calls `CompleteReply`.
#[allow(dead_code)] // consumed when tool dispatch is wired to the editor's ToolHost
struct Reply {
    ok: bool,
    result_json: String,
    images: Vec<ffi::ImagePng>,
}

/// Opaque reply handle owned by C++ between `Invoke` and `CompleteReply`.
pub struct PendingReply {
    sender: oneshot::Sender<Reply>,
}

/// Owns the transport runtime so it lives exactly as long as the C++ server.
/// `stop` is idempotent and `Drop` falls back to it, so both an explicit
/// `StopServer` from C++ and the final destruction shut the runtime down.
pub struct Server {
    #[allow(dead_code)] // kept alive for the P1 tool dispatch (#528)
    host: SharedPtr<ffi::ToolHost>,
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
            runtime.shutdown_timeout(Duration::from_secs(5));
        }
    }

    fn url(&self) -> String {
        self.url.clone()
    }
}

#[derive(Clone, Default)]
struct BridgeHandler;

impl ServerHandler for BridgeHandler {
    fn get_info(&self) -> rmcp::model::ServerConfig {
        rmcp::model::ServerConfig::new(
            ServerCapabilities::builder()
                .enable_tools()
                .build(),
        )
    }

    // TODO(P1 #528): list the editor's real tools here (one entry per tool,
    // with per-tool JSON schemas) instead of the empty list.
    // TODO(P1 #528): dispatch CallTool through the C++ ToolHost with the
    // Read/Write/Exec mode checks, and add Origin/Host header validation.
    // Until then the empty tool list keeps MCP clients from calling anything.
}

// Fails closed: anything but an exact `Bearer <token>` is rejected. An empty
// token never reaches here in production (StartServer refuses to start one),
// but this middleware stays fail-closed so a misconfigured server cannot fall
// back to open access.
async fn require_token(State(token): State<String>, request: Request<Body>, next: Next) -> Response {
    let header = request
        .headers()
        .get(AUTHORIZATION)
        .and_then(|value| value.to_str().ok());
    if header.is_some_and(|value| value == format!("Bearer {token}")) {
        next.run(request).await
    } else {
        StatusCode::UNAUTHORIZED.into_response()
    }
}

fn start_server(
    cfg: &ffi::ServerConfig,
    host: SharedPtr<ffi::ToolHost>,
) -> io::Result<Box<Server>> {
    let transport = start_transport(cfg)?;
    Ok(Box::new(Server { host, transport }))
}

fn start_transport(cfg: &ffi::ServerConfig) -> io::Result<Transport> {
    // An empty token would disable the bearer middleware entirely and expose
    // the editor to every local process; refuse to start instead.
    if cfg.token.is_empty() {
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

    let listener = std::net::TcpListener::bind(("127.0.0.1", cfg.port))?;
    listener.set_nonblocking(true)?;
    let listener = tokio::net::TcpListener::from_std(listener)?;
    let url = format!("http://{}/mcp", listener.local_addr()?);

    let service = StreamableHttpService::new(
        || Ok(BridgeHandler::default()),
        std::sync::Arc::new(LocalSessionManager::default()),
        StreamableHttpServerConfig::default(),
    );
    let router = Router::new()
        .nest_service("/mcp", service)
        .layer(middleware::from_fn_with_state(cfg.token.clone(), require_token));

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

    fn cfg(port: u16, token: &str) -> ffi::ServerConfig {
        ffi::ServerConfig {
            port,
            read_only: false,
            token: token.to_owned(),
        }
    }

    #[test]
    fn start_stop_and_url() {
        let mut transport = start_transport(&cfg(0, "test-token")).expect("server starts");
        assert!(transport.url().starts_with("http://127.0.0.1:"));
        assert!(transport.url().ends_with("/mcp"));
        transport.stop();
    }

    #[test]
    fn refuses_an_empty_token() {
        assert!(
            start_transport(&cfg(0, "")).is_err(),
            "an empty token must not start an unauthenticated server"
        );
    }

    /// One raw HTTP POST against the running transport; returns the status.
    fn probe(port: u16, authorization: Option<&str>) -> io::Result<u16> {
        use std::io::{Read, Write};

        let mut stream = std::net::TcpStream::connect(("127.0.0.1", port))?;
        stream.set_read_timeout(Some(Duration::from_secs(5)))?;
        let mut request = format!(
            "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n\
             Content-Length: 0\r\nConnection: close\r\n"
        );
        if let Some(value) = authorization {
            request.push_str("Authorization: ");
            request.push_str(value);
            request.push_str("\r\n");
        }
        request.push_str("\r\n");
        stream.write_all(request.as_bytes())?;
        let mut response = Vec::new();
        stream.read_to_end(&mut response)?;
        let head = String::from_utf8_lossy(&response);
        head.lines()
            .next()
            .and_then(|line| line.split_whitespace().nth(1))
            .and_then(|status| status.parse().ok())
            .ok_or_else(|| {
                io::Error::new(io::ErrorKind::InvalidData, "no HTTP status line")
            })
    }

    #[test]
    fn token_middleware_fails_closed() {
        let mut transport = start_transport(&cfg(0, "secret-token")).expect("server starts");
        let port: u16 = transport
            .url()
            .split(':')
            .nth(2)
            .and_then(|rest| rest.split('/').next())
            .and_then(|port| port.parse().ok())
            .expect("the URL carries the bound port");

        // Missing header and wrong token are both rejected; the exact token
        // gets past the middleware (whatever the MCP service replies with).
        let missing = probe(port, None).expect("probe runs");
        let wrong = probe(port, Some("Bearer nope")).expect("probe runs");
        let right = probe(port, Some("Bearer secret-token")).expect("probe runs");
        transport.stop();

        assert_eq!((missing, wrong), (401, 401), "fail closed on bad tokens");
        assert_ne!(right, 401, "the right token passes the middleware");
    }

    #[test]
    fn complete_reply_delivers_the_payload() {
        let (sender, receiver) = oneshot::channel();
        let reply = Box::new(PendingReply { sender });
        complete_reply(
            reply,
            false,
            "{\"error\":\"nope\"}".to_owned(),
            vec![ffi::ImagePng {
                data: vec![0x89, b'P', b'N', b'G'],
                label: "frame".to_owned(),
            }],
        );
        let reply = receiver.blocking_recv().expect("reply delivered");
        assert!(!reply.ok);
        assert_eq!(reply.result_json, "{\"error\":\"nope\"}");
        assert_eq!(reply.images[0].label, "frame");
    }
}
