// SPDX-License-Identifier: MIT
//! Reader-view proxy for the recovery's NetSurf, which has no JavaScript and no
//! flexbox or grid. It fetches a page itself, keeps the main content (text,
//! headings, lists, tables, code, images, links, GET forms) and drops scripts,
//! site CSS and the page furniture, so modern sites read as plain documents.
//!
//!   /            start page with an address and a search box
//!   /r?u=URL     a page in reader view; every link goes through the proxy again
//!   /f?__u=URL&… a GET form submitted to URL
//!   /raw?u=URL   a file streamed through unchanged, named for NetSurf's download
//!
//! Listens on 127.0.0.1:8088 (GTS7XL_READER_PORT changes the port).

use std::collections::HashMap;
use std::io::{self, BufRead, BufReader, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::thread;
use std::time::Duration;

use scraper::{ElementRef, Html, Node, Selector};
use url::Url;

const UA: &str = "Mozilla/5.0 (X11; Linux aarch64; rv:128.0) Gecko/20100101 Firefox/128.0";
const MAX_PAGE: u64 = 8 << 20;
const MAX_FRAGMENTS: usize = 6;

/// Links to these are files, not pages: they go straight to /raw
const FILE_EXT: &[&str] = &[
    ".zip", ".img", ".apk", ".tar", ".xz", ".gz", ".zst", ".7z", ".bin", ".md5", ".lz4", ".iso", ".deb",
    ".pkg.tar.zst", ".exe", ".pdf",
];

fn main() {
    let port = std::env::var("GTS7XL_READER_PORT").unwrap_or_else(|_| "8088".into());
    let listener = TcpListener::bind(format!("127.0.0.1:{port}")).expect("cannot listen");
    for stream in listener.incoming().flatten() {
        thread::spawn(move || {
            let _ = handle(stream);
        });
    }
}

fn agent() -> ureq::Agent {
    ureq::AgentBuilder::new()
        .user_agent(UA)
        .timeout_connect(Duration::from_secs(15))
        .timeout_read(Duration::from_secs(60))
        .redirects(8)
        .build()
}

fn handle(mut s: TcpStream) -> io::Result<()> {
    let mut r = BufReader::new(s.try_clone()?);
    let mut line = String::new();
    r.read_line(&mut line)?;
    // the rest of the request header
    loop {
        let mut h = String::new();
        if r.read_line(&mut h)? == 0 || h == "\r\n" || h == "\n" {
            break;
        }
    }
    let mut parts = line.split_whitespace();
    let (method, target) = (parts.next().unwrap_or(""), parts.next().unwrap_or("/"));
    if method != "GET" {
        return respond(&mut s, 405, "text/plain", b"only GET");
    }
    let (path, query) = target.split_once('?').unwrap_or((target, ""));
    let q: Vec<(String, String)> = url::form_urlencoded::parse(query.as_bytes()).into_owned().collect();
    let get = |k: &str| q.iter().find(|(n, _)| n == k).map(|(_, v)| v.clone());

    match path {
        "/" => respond(&mut s, 200, "text/html; charset=utf-8", start_page().as_bytes()),
        "/r" => match get("u").and_then(|u| parse_url(&u)) {
            Some(u) => reader(&mut s, u),
            None => respond(&mut s, 400, "text/html; charset=utf-8", error_page("No address").as_bytes()),
        },
        "/f" => {
            let Some(mut u) = get("__u").and_then(|u| parse_url(&u)) else {
                return respond(&mut s, 400, "text/html; charset=utf-8", error_page("Bad form").as_bytes());
            };
            {
                let mut qp = u.query_pairs_mut();
                for (k, v) in q.iter().filter(|(k, _)| k != "__u") {
                    qp.append_pair(k, v);
                }
            }
            reader(&mut s, u)
        }
        "/raw" => match get("u").and_then(|u| parse_url(&u)) {
            Some(u) => raw(&mut s, u),
            None => respond(&mut s, 400, "text/plain", b"no address"),
        },
        _ => respond(&mut s, 404, "text/plain", b"not found"),
    }
}

/// Accepts "example.com/x" too
fn parse_url(u: &str) -> Option<Url> {
    let u = u.trim();
    let u = if u.contains("://") { u.to_string() } else { format!("https://{u}") };
    Url::parse(&u).ok().filter(|u| u.scheme() == "http" || u.scheme() == "https")
}

fn respond(s: &mut TcpStream, code: u16, ctype: &str, body: &[u8]) -> io::Result<()> {
    let reason = match code {
        200 => "OK",
        400 => "Bad Request",
        404 => "Not Found",
        405 => "Method Not Allowed",
        _ => "Bad Gateway",
    };
    write!(
        s,
        "HTTP/1.1 {code} {reason}\r\nContent-Type: {ctype}\r\nContent-Length: {}\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
        body.len()
    )?;
    s.write_all(body)
}

fn esc(t: &str) -> String {
    t.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;")
}

fn enc(u: &str) -> String {
    url::form_urlencoded::byte_serialize(u.as_bytes()).collect()
}

fn page(title: &str, original: Option<&Url>, body: &str) -> String {
    let bar = match original {
        Some(u) => format!(
            "<p class=bar><a href=\"/\">Start</a> &middot; <a href=\"/r?u={0}\">Reload</a> &middot; <a href=\"{1}\">Original page</a></p><hr>",
            enc(u.as_str()),
            esc(u.as_str())
        ),
        None => String::new(),
    };
    format!(
        "<!DOCTYPE html><html><head><meta charset=utf-8><title>{}</title><style>\
         body{{font-family:sans-serif;margin:1em 2em;max-width:70em;line-height:1.4}}\
         pre{{background:#eee;padding:.5em;white-space:pre-wrap}}img{{max-width:100%}}\
         .bar{{font-size:.9em}}table{{border-collapse:collapse}}td,th{{border:1px solid #ccc;padding:.2em .4em}}\
         input[type=text],input[type=search]{{width:40em}}</style></head><body>{bar}{body}</body></html>",
        esc(title)
    )
}

fn start_page() -> String {
    page(
        "Reader",
        None,
        "<h1>Reader</h1>\
         <p>Pages open in a simple reader view that this browser can show. Files are saved to the Download folder.</p>\
         <form action=\"/r\" method=get><p><input type=text name=u placeholder=\"Address, e.g. github.com/JeyKul/gts7xl-mainline\"> <input type=submit value=Open></p></form>\
         <form action=\"/f\" method=get><input type=hidden name=__u value=\"https://html.duckduckgo.com/html/\"><p><input type=search name=q placeholder=\"Search the web\"> <input type=submit value=Search></p></form>\
         <h2>Links</h2><ul>\
         <li><a href=\"/r?u=https%3A%2F%2Fgithub.com%2FJeyKul%2Fgts7xl-mainline%2Freleases\">Tab S7+ mainline Linux: releases</a></li>\
         <li><a href=\"/r?u=https%3A%2F%2Fgithub.com%2FJeyKul%2Fgts7xl-mainline\">Tab S7+ mainline Linux: project</a></li>\
         <li><a href=\"/r?u=https%3A%2F%2Ftwrp.me%2Fsamsung%2Fsamsunggalaxytabs7plus.html\">TWRP for the Tab S7+</a></li>\
         <li><a href=\"/r?u=https%3A%2F%2Fgithub.com%2Ftopjohnwu%2FMagisk%2Freleases\">Magisk releases</a></li>\
         </ul><p>XDA, Google Drive and MEGA need JavaScript and don't open here.</p>",
    )
}

fn error_page(msg: &str) -> String {
    page("Error", None, &format!("<h1>Could not open the page</h1><p>{}</p><p><a href=\"/\">Start</a></p>", esc(msg)))
}

fn looks_like_file(u: &Url) -> bool {
    let p = u.path().to_ascii_lowercase();
    FILE_EXT.iter().any(|e| p.ends_with(e)) || p.contains("/releases/download/")
}

fn reader(s: &mut TcpStream, u: Url) -> io::Result<()> {
    if looks_like_file(&u) {
        return raw(s, u);
    }
    let resp = match agent().get(u.as_str()).call() {
        Ok(r) => r,
        Err(ureq::Error::Status(code, r)) if r.content_type().contains("html") => {
            let _ = code;
            r
        }
        Err(e) => return respond(s, 502, "text/html; charset=utf-8", error_page(&e.to_string()).as_bytes()),
    };
    let base = Url::parse(resp.get_url()).unwrap_or(u.clone());
    let ctype = resp.content_type().to_string();
    if !(ctype.contains("html") || ctype.contains("xml") && !ctype.contains("svg")) {
        // not a page after all: hand it over as a file
        return stream(s, resp, &u);
    }
    let mut body = String::new();
    resp.into_reader().take(MAX_PAGE).read_to_string(&mut body).ok();
    let html = simplify(&body, &base);
    respond(s, 200, "text/html; charset=utf-8", html.as_bytes())
}

fn raw(s: &mut TcpStream, u: Url) -> io::Result<()> {
    match agent().get(u.as_str()).call() {
        Ok(r) => stream(s, r, &u),
        Err(e) => respond(s, 502, "text/html; charset=utf-8", error_page(&e.to_string()).as_bytes()),
    }
}

/// The file name: the server's, else the requested address's last part (GitHub's
/// asset links redirect to storage addresses without a usable name)
fn file_name(resp: &ureq::Response, asked: &Url) -> String {
    if let Some(cd) = resp.header("content-disposition") {
        if let Some(i) = cd.find("filename=") {
            let n = cd[i + 9..].trim_matches(|c| c == '"' || c == ' ' || c == ';');
            let n = n.split(';').next().unwrap_or("").trim_matches('"');
            if !n.is_empty() {
                return sanitize(n);
            }
        }
    }
    let leaf = asked.path_segments().and_then(|mut s| s.next_back()).unwrap_or("download");
    let leaf = url::form_urlencoded::parse(format!("x={leaf}").as_bytes())
        .next()
        .map(|(_, v)| v.into_owned())
        .unwrap_or_default();
    sanitize(if leaf.is_empty() { "download" } else { &leaf })
}

fn sanitize(n: &str) -> String {
    n.chars().map(|c| if c == '/' || c == '\\' || c == '"' || c.is_control() { '_' } else { c }).collect()
}

fn stream(s: &mut TcpStream, resp: ureq::Response, asked: &Url) -> io::Result<()> {
    let name = file_name(&resp, asked);
    let ctype = resp.header("content-type").unwrap_or("application/octet-stream").to_string();
    let len = resp.header("content-length").map(|l| format!("Content-Length: {l}\r\n")).unwrap_or_default();
    write!(
        s,
        "HTTP/1.1 200 OK\r\nContent-Type: {ctype}\r\n{len}Content-Disposition: attachment; filename=\"{name}\"\r\nConnection: close\r\n\r\n"
    )?;
    io::copy(&mut resp.into_reader(), s)?;
    Ok(())
}

// ---- reader view ----

struct Ctx<'a> {
    base: &'a Url,
    out: String,
    fragments: usize,
}

const DROP: &[&str] = &[
    "script", "style", "noscript", "template", "svg", "iframe", "object", "embed", "canvas", "video", "audio",
    "nav", "header", "footer", "aside", "dialog", "head", "link", "meta",
];
const BLOCK: &[&str] = &[
    "h1", "h2", "h3", "h4", "h5", "h6", "p", "ul", "ol", "li", "pre", "blockquote", "table", "thead", "tbody",
    "tfoot", "tr", "td", "th", "dl", "dt", "dd", "details", "summary", "figure", "figcaption", "caption",
];
const INLINE: &[&str] = &["strong", "b", "em", "i", "code", "kbd", "samp", "sub", "sup", "small", "del", "s", "mark", "u", "abbr"];
const CONTAINER: &[&str] = &["div", "section", "article", "main", "center"];

fn simplify(html: &str, base: &Url) -> String {
    let doc = Html::parse_document(html);
    let title = Selector::parse("title")
        .ok()
        .and_then(|s| doc.select(&s).next())
        .map(|t| t.text().collect::<String>().trim().to_string())
        .unwrap_or_else(|| base.as_str().to_string());
    // the page's main part when it marks one, else everything in the body
    let root = ["main", "[role=main]", "#content", "body"]
        .iter()
        .filter_map(|s| Selector::parse(s).ok())
        .find_map(|s| doc.select(&s).next());
    let mut c = Ctx { base, out: String::new(), fragments: 0 };
    match root {
        Some(r) => walk(&mut c, r, root_is_body(r)),
        None => c.out.push_str(&esc(&doc.root_element().text().collect::<String>())),
    }
    page(&title, Some(base), &c.out)
}

fn root_is_body(e: ElementRef) -> bool {
    e.value().name() == "body"
}

fn hidden(e: &ElementRef) -> bool {
    let v = e.value();
    v.attr("hidden").is_some()
        || v.attr("aria-hidden") == Some("true")
        || v.attr("style").map(|s| s.replace(' ', "").contains("display:none")).unwrap_or(false)
        || v.attr("type") == Some("hidden") && v.name() != "input"
}

fn link(base: &Url, href: &str) -> Option<String> {
    let href = href.trim();
    if href.starts_with('#') {
        return Some(esc(href));
    }
    let u = base.join(href).ok()?;
    match u.scheme() {
        "http" | "https" if looks_like_file(&u) => Some(format!("/raw?u={}", enc(u.as_str()))),
        "http" | "https" => Some(format!("/r?u={}", enc(u.as_str()))),
        _ => None,
    }
}

fn walk(c: &mut Ctx, e: ElementRef, drop_furniture: bool) {
    for child in e.children() {
        match child.value() {
            Node::Text(t) => c.out.push_str(&esc(t)),
            Node::Element(_) => {
                if let Some(el) = ElementRef::wrap(child) {
                    element(c, el, drop_furniture);
                }
            }
            _ => {}
        }
    }
}

fn element(c: &mut Ctx, e: ElementRef, drop_furniture: bool) {
    let name = e.value().name();
    if hidden(&e) {
        return;
    }
    let furniture = matches!(name, "nav" | "header" | "footer" | "aside");
    if DROP.contains(&name) && (!furniture || drop_furniture) {
        return;
    }
    if furniture {
        walk(c, e, drop_furniture);
        return;
    }
    match name {
        "a" => match e.value().attr("href").and_then(|h| link(c.base, h)) {
            Some(h) => {
                c.out.push_str(&format!("<a href=\"{h}\">"));
                walk(c, e, drop_furniture);
                c.out.push_str("</a>");
            }
            None => walk(c, e, drop_furniture),
        },
        "img" => {
            let v = e.value();
            let src = v.attr("src").or_else(|| v.attr("data-src")).or_else(|| v.attr("data-canonical-src"));
            if let Some(u) = src.and_then(|s| c.base.join(s).ok()).filter(|u| u.scheme().starts_with("http")) {
                let alt = v.attr("alt").unwrap_or("");
                c.out.push_str(&format!("<img src=\"{}\" alt=\"{}\">", esc(u.as_str()), esc(alt)));
            }
        }
        "br" | "hr" => c.out.push_str(&format!("<{name}>")),
        "include-fragment" => {
            // GitHub fills these in with JavaScript, e.g. the assets of a release
            let src = e.value().attr("src").and_then(|s| c.base.join(s).ok());
            match src {
                Some(u) if c.fragments < MAX_FRAGMENTS => {
                    c.fragments += 1;
                    if let Ok(r) = agent().get(u.as_str()).set("Accept", "text/html").call() {
                        let mut b = String::new();
                        r.into_reader().take(MAX_PAGE).read_to_string(&mut b).ok();
                        let frag = Html::parse_fragment(&b);
                        walk(c, frag.root_element(), drop_furniture);
                    }
                }
                _ => walk(c, e, drop_furniture),
            }
        }
        "form" => form(c, e, drop_furniture),
        "input" | "select" | "textarea" | "button" | "option" => field(c, e, drop_furniture),
        n if BLOCK.contains(&n) || INLINE.contains(&n) => {
            let extra = match n {
                "td" | "th" => e.value().attr("colspan").map(|v| format!(" colspan=\"{}\"", esc(v))).unwrap_or_default(),
                "details" => " open".to_string(),
                _ => String::new(),
            };
            c.out.push_str(&format!("<{n}{extra}>"));
            walk(c, e, drop_furniture);
            c.out.push_str(&format!("</{n}>"));
        }
        n if CONTAINER.contains(&n) => {
            c.out.push_str("<div>");
            walk(c, e, drop_furniture);
            c.out.push_str("</div>");
        }
        _ => walk(c, e, drop_furniture),
    }
}

fn form(c: &mut Ctx, e: ElementRef, drop_furniture: bool) {
    let get = e.value().attr("method").map(|m| m.eq_ignore_ascii_case("get")).unwrap_or(true);
    let action = c.base.join(e.value().attr("action").unwrap_or("")).ok();
    match action.filter(|_| get) {
        Some(a) => {
            c.out.push_str(&format!(
                "<form action=\"/f\" method=get><input type=hidden name=__u value=\"{}\">",
                esc(a.as_str())
            ));
            walk(c, e, drop_furniture);
            c.out.push_str("</form>");
        }
        // POST forms (logins, uploads) can't go through here
        None => {}
    }
}

fn field(c: &mut Ctx, e: ElementRef, drop_furniture: bool) {
    let v = e.value();
    let attrs: HashMap<&str, &str> = v.attrs().collect();
    let a = |k: &str| attrs.get(k).map(|s| esc(s)).unwrap_or_default();
    match v.name() {
        "input" => {
            let t = attrs.get("type").copied().unwrap_or("text").to_ascii_lowercase();
            if matches!(t.as_str(), "text" | "search" | "hidden" | "checkbox" | "radio" | "submit" | "number" | "email" | "url") {
                let checked = if attrs.contains_key("checked") { " checked" } else { "" };
                c.out.push_str(&format!(
                    "<input type=\"{}\" name=\"{}\" value=\"{}\" placeholder=\"{}\"{checked}>",
                    esc(&t),
                    a("name"),
                    a("value"),
                    a("placeholder")
                ));
            }
        }
        "select" => {
            c.out.push_str(&format!("<select name=\"{}\">", a("name")));
            walk(c, e, drop_furniture);
            c.out.push_str("</select>");
        }
        "option" => {
            let sel = if attrs.contains_key("selected") { " selected" } else { "" };
            c.out.push_str(&format!("<option value=\"{}\"{sel}>", a("value")));
            walk(c, e, drop_furniture);
            c.out.push_str("</option>");
        }
        "textarea" => {
            c.out.push_str(&format!("<textarea name=\"{}\">", a("name")));
            walk(c, e, drop_furniture);
            c.out.push_str("</textarea>");
        }
        "button" => {
            let t = attrs.get("type").copied().unwrap_or("submit");
            if t.eq_ignore_ascii_case("submit") {
                let label: String = e.text().collect::<String>().trim().to_string();
                c.out.push_str(&format!(
                    "<input type=submit name=\"{}\" value=\"{}\">",
                    a("name"),
                    esc(if label.is_empty() { "Go" } else { &label })
                ));
            }
        }
        _ => {}
    }
}
