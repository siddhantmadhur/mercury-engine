// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! USS stylesheet parser backed by the `cssparser` crate.
//!
//! cssparser does the tokenizing; this module walks the token stream and
//! returns flat structs over cxx: token lists and rules made of selector
//! parts plus raw declaration names, values and tokens. Tokens cross as a
//! DFS preorder flat array with a depth per token, so the C++ side can
//! rebuild the nested function arguments; `kind` and `depth` are bytes, and
//! `text`, `number` and `unit` carry the value. Everything CSS-specific
//! stays here; the C++ side (mercury/ui/uss_parser.cc) builds its
//! StyleRule/ComplexSelector types, filters declarations against the style
//! property whitelist and converts values to engine types.

use cssparser::{Delimiter, Delimiters, Parser, SourcePosition, Token};

use self::ffi::*;

#[cxx::bridge(namespace = "Mercury::uss")]
mod ffi {
    // Token kinds, in StyleToken::Type order (mercury/ui/style_sheet.h):
    // 0 ident, 1 number, 2 dimension, 3 percentage, 4 hash, 5 string,
    // 6 function, 7 comma. Selector combinators: 0 descendant, 1 child.

    struct UssToken {
        kind: u8,
        text: String,
        number: f32,
        unit: String,
        depth: u8,
    }

    struct UssPart {
        type_name: String,
        name: String,
        classes: Vec<String>,
        pseudo: u32,
        not_pseudo: u32,
    }

    struct UssSelector {
        text: String,
        parts: Vec<UssPart>,
        combinators: Vec<u8>,
    }

    struct UssDeclaration {
        name: String,
        value: String,
        tokens: Vec<UssToken>,
        line: u32,
    }

    struct UssRule {
        line: u32,
        selectors: Vec<UssSelector>,
        declarations: Vec<UssDeclaration>,
    }

    struct UssImport {
        text: String,
        line: u32,
    }

    struct UssError {
        line: u32,
        message: String,
    }

    struct UssSheet {
        imports: Vec<UssImport>,
        rules: Vec<UssRule>,
        errors: Vec<UssError>,
    }

    // cxx cannot return a Result of a shared struct, so ParseSelectorText
    // carries the error message next to the selector.
    struct UssSelectorResult {
        ok: bool,
        selector: UssSelector,
        error: String,
    }

    // The text entry points take byte slices, not &str: a sheet saved as
    // Latin-1 must not throw when cxx would validate UTF-8.
    extern "Rust" {
        #[cxx_name = "ParseSheet"]
        fn uss_parse_sheet(bytes: &[u8]) -> UssSheet;
        #[cxx_name = "TokenizeValue"]
        fn uss_tokenize_value(bytes: &[u8]) -> Vec<UssToken>;
        #[cxx_name = "ParseSelectorText"]
        fn uss_parse_selector(bytes: &[u8]) -> UssSelectorResult;
        #[cxx_name = "ParseDeclarations"]
        fn uss_parse_declarations(bytes: &[u8]) -> Vec<UssDeclaration>;
    }
}

const KIND_IDENT: u8 = 0;
const KIND_NUMBER: u8 = 1;
const KIND_DIMENSION: u8 = 2;
const KIND_PERCENTAGE: u8 = 3;
const KIND_HASH: u8 = 4;
const KIND_STRING: u8 = 5;
const KIND_FUNCTION: u8 = 6;
const KIND_COMMA: u8 = 7;

const COMBINATOR_DESCENDANT: u8 = 0;
const COMBINATOR_CHILD: u8 = 1;

// Pseudo-state bits, mirroring PseudoState in mercury/ui/style_sheet.h.
const PSEUDO_HOVER: u32 = 1 << 0;
const PSEUDO_ACTIVE: u32 = 1 << 1;
const PSEUDO_FOCUS: u32 = 1 << 2;
const PSEUDO_CHECKED: u32 = 1 << 3;
const PSEUDO_DISABLED: u32 = 1 << 4;
const PSEUDO_ROOT: u32 = 1 << 5;

// A real value never nests this far; beyond it the rest of the value is
// skipped instead of trying to recover.
const MAX_NESTING: u32 = 32;
// A selector longer than this is not something a real sheet needs.
const MAX_PARTS: usize = 32;

fn token(kind: u8, text: String, depth: u32) -> UssToken {
    UssToken {
        kind,
        text,
        number: 0.0,
        unit: String::new(),
        depth: depth as u8,
    }
}

/// The text a number was written with, as close to the source as the parsed
/// value allows (cssparser keeps the integer part when it can).
fn number_text(value: f32, int_value: Option<i32>) -> String {
    match int_value {
        Some(int) => int.to_string(),
        None => value.to_string(),
    }
}

fn tokenize_value_text(text: &str) -> Vec<UssToken> {
    let mut parser = Parser::new(text);
    let mut out = Vec::new();
    tokenize_value(&mut parser, 0, &mut out);
    out
}

fn uss_tokenize_value(bytes: &[u8]) -> Vec<UssToken> {
    tokenize_value_text(&String::from_utf8_lossy(bytes))
}

fn tokenize_value(parser: &mut Parser, depth: u32, out: &mut Vec<UssToken>) {
    if depth >= MAX_NESTING {
        return;
    }
    loop {
        let value = match parser.next() {
            Ok(value) => value,
            Err(_) => return,  // end of input or of the enclosing block
        };
        match value {
            Token::Ident(value) => out.push(token(KIND_IDENT, value.to_string(), depth)),
            Token::Number { value, int_value, .. } => out.push(UssToken {
                kind: KIND_NUMBER,
                text: number_text(*value, *int_value),
                number: *value,
                unit: String::new(),
                depth: depth as u8,
            }),
            Token::Percentage {
                unit_value,
                int_value,
                ..
            } => {
                let number = match int_value {
                    Some(int) => *int as f32,
                    None => unit_value * 100.0,
                };
                let mut text = number_text(number, *int_value);
                text.push('%');
                out.push(UssToken {
                    kind: KIND_PERCENTAGE,
                    text,
                    number,
                    unit: String::new(),
                    depth: depth as u8,
                })
            }
            Token::Dimension {
                value,
                unit,
                int_value,
                ..
            } => {
                let unit = unit.to_ascii_lowercase();
                let mut text = number_text(*value, *int_value);
                text.push_str(&unit);
                out.push(UssToken {
                    kind: KIND_DIMENSION,
                    text,
                    number: *value,
                    unit,
                    depth: depth as u8,
                })
            }
            Token::Hash(value) | Token::IDHash(value) => {
                out.push(token(KIND_HASH, value.to_string(), depth))
            }
            Token::QuotedString(value) => {
                out.push(token(KIND_STRING, value.to_string(), depth))
            }
            Token::Comma => out.push(token(KIND_COMMA, ",".to_string(), depth)),
            Token::Function(name) => {
                // Function names are matched case-insensitively on both
                // sides of the bridge.
                let name = name.to_ascii_lowercase();
                out.push(token(KIND_FUNCTION, name.clone(), depth));
                let mut args = Vec::new();
                let _ = parser.parse_nested_block::<_, (), ()>(|inner| {
                    if name == "url" || name == "resource" {
                        // A quoted string is tokenized normally; an unquoted
                        // path — url(a/b.png), resource(Fonts/Inter) — is one
                        // raw string argument up to ')', the way the
                        // hand-written tokenizer read it.
                        let entry = inner.position();
                        let mut first: Option<(SourcePosition, Token)> = None;
                        loop {
                            let at = inner.position();
                            match inner.next_including_whitespace_and_comments() {
                                Err(_) => break,
                                Ok(Token::WhiteSpace(_))
                                | Ok(Token::Comment(_)) => continue,
                                Ok(value) => {
                                    first = Some((at, value.clone()));
                                    break;
                                }
                            }
                        }
                        match first {
                            Some((_, Token::QuotedString(value))) => {
                                args.push(UssToken {
                                    kind: KIND_STRING,
                                    text: value.to_string(),
                                    number: 0.0,
                                    unit: String::new(),
                                    depth: depth as u8 + 1,
                                });
                                tokenize_value(inner, depth + 1, &mut args);
                            }
                            _ => {
                                let from =
                                    first.map_or(entry, |(at, _)| at);
                                while inner.next().is_ok() {}
                                let raw = inner
                                    .slice(from..inner.position())
                                    .trim()
                                    .to_string();
                                args.push(UssToken {
                                    kind: KIND_STRING,
                                    text: raw,
                                    number: 0.0,
                                    unit: String::new(),
                                    depth: depth as u8 + 1,
                                });
                            }
                        }
                    } else {
                        tokenize_value(inner, depth + 1, &mut args);
                    }
                    Ok(())
                });
                out.extend(args);
            }
            Token::UnquotedUrl(value) => {
                // url(foo.png) without quotes: the function carries the path
                // as its single string argument.
                out.push(token(KIND_FUNCTION, "url".to_string(), depth));
                out.push(token(KIND_STRING, value.to_string(), depth + 1));
            }
            // Whitespace and comments are skipped by `next()`; the rest
            // (delimiters such as '!', stray closing brackets, bad tokens)
            // is ignored like the hand-written tokenizer did.
            _ => {}
        }
    }
}

/// The next token that is not whitespace or a comment, with its 1-based line
/// and the byte offset it starts at.
fn next_significant<'a>(parser: &mut Parser<'a>) -> Option<(u32, SourcePosition, Token<'a>)> {
    loop {
        let line = parser.current_source_location().line;
        let start = parser.position();
        match parser.next_including_whitespace_and_comments() {
            Err(_) => return None,
            Ok(Token::WhiteSpace(_)) | Ok(Token::Comment(_)) => continue,
            Ok(value) => return Some((line + 1, start, value.clone())),
        }
    }
}

/// Consumes every token up to (but not past) one of `delimiters`. The tokens
/// are gone from the stream, but they stay in the source slice, so raw text
/// for a value can be taken with `Parser::slice_from` afterwards.
fn consume_until(parser: &mut Parser, delimiters: Delimiters) {
    let _ = parser.parse_until_before::<_, (), ()>(delimiters, |limited| {
        while limited.next().is_ok() {}
        Ok(())
    });
}

fn uss_parse_selector(bytes: &[u8]) -> UssSelectorResult {
    parse_selector_text(&String::from_utf8_lossy(bytes))
}

fn parse_selector_text(text: &str) -> UssSelectorResult {
    match parse_one_selector(text) {
        Ok(selector) => UssSelectorResult {
            ok: true,
            selector,
            error: String::new(),
        },
        Err(message) => UssSelectorResult {
            ok: false,
            selector: UssSelector {
                text: String::new(),
                parts: Vec::new(),
                combinators: Vec::new(),
            },
            error: message,
        },
    }
}

/// Parses one selector (no comma lists); selectors in a sheet are split on
/// commas before calling this.
fn parse_one_selector(text: &str) -> Result<UssSelector, String> {
    let mut parser = Parser::new(text);
    let mut selector = UssSelector {
        text: text.trim().to_string(),
        parts: Vec::new(),
        combinators: Vec::new(),
    };
    let mut pending_child = false;
    // Whitespace consumed at the end of the previous part counts as the
    // space before the next one.
    let mut trailing_space = false;
    loop {
        // Whitespace and '>' between parts.
        let mut had_space = trailing_space;
        trailing_space = false;
        let first = loop {
            match parser.next_including_whitespace() {
                Err(_) => break None,
                Ok(Token::WhiteSpace(_)) => had_space = true,
                Ok(Token::Delim('>')) => pending_child = true,
                Ok(value) => break Some(value.clone()),
            }
        };
        let Some(first) = first else {
            break;
        };
        if !selector.parts.is_empty() {
            if !had_space && !pending_child {
                return Err("unexpected character".to_string());
            }
            selector.combinators.push(if pending_child {
                COMBINATOR_CHILD
            } else {
                COMBINATOR_DESCENDANT
            });
        }
        pending_child = false;

        let mut part = UssPart {
            type_name: String::new(),
            name: String::new(),
            classes: Vec::new(),
            pseudo: 0,
            not_pseudo: 0,
        };
        let mut token = first;
        loop {
            match &token {
                Token::Ident(value) => part.type_name = value.to_string(),
                Token::Hash(value) | Token::IDHash(value) => part.name = value.to_string(),
                Token::Delim('*') => part.type_name = "*".to_string(),
                Token::Delim('.') => match parser.next_including_whitespace() {
                    Ok(Token::Ident(value)) => part.classes.push(value.to_string()),
                    _ => return Err("expected a class name after '.'".to_string()),
                },
                Token::Delim('#') => {
                    return Err("expected a name after '#'".to_string());
                }
                Token::Colon => match parser.next_including_whitespace() {
                    Ok(Token::Colon) => {
                        return Err("pseudo-elements are not supported".to_string())
                    }
                    Ok(Token::Ident(pseudo)) => {
                        let pseudo = pseudo.to_ascii_lowercase();
                        match pseudo.as_str() {
                            "hover" => part.pseudo |= PSEUDO_HOVER,
                            "active" => part.pseudo |= PSEUDO_ACTIVE,
                            // The negations of the stateful classes.
                            "inactive" => part.not_pseudo |= PSEUDO_ACTIVE,
                            "focus" => part.pseudo |= PSEUDO_FOCUS,
                            "checked" => part.pseudo |= PSEUDO_CHECKED,
                            "disabled" => part.pseudo |= PSEUDO_DISABLED,
                            "enabled" => part.not_pseudo |= PSEUDO_DISABLED,
                            "root" => part.pseudo |= PSEUDO_ROOT,
                            other => {
                                return Err(format!("unknown pseudo-class :{}", other))
                            }
                        }
                    }
                    Ok(other) => {
                        return Err(format!(
                            "unknown pseudo-class :{}",
                            describe_token(other)
                        ))
                    }
                    Err(_) => return Err("unknown pseudo-class :".to_string()),
                },
                Token::SquareBracketBlock => {
                    return Err("attribute selectors are not supported".to_string());
                }
                other => {
                    return Err(format!(
                        "unexpected '{}' in selector",
                        describe_token(other)
                    ))
                }
            }
            match parser.next_including_whitespace() {
                Err(_) => break,
                Ok(Token::WhiteSpace(_)) => {
                    trailing_space = true;
                    break;
                }
                Ok(Token::Delim('>')) => {
                    pending_child = true;
                    break;
                }
                Ok(next) => token = next.clone(),
            }
        }
        selector.parts.push(part);
        if selector.parts.len() > MAX_PARTS {
            return Err("selector has too many parts".to_string());
        }
    }
    if selector.parts.is_empty() {
        return Err("empty selector".to_string());
    }
    if selector.combinators.len() + 1 != selector.parts.len() {
        return Err("selector ends with a combinator".to_string());
    }
    Ok(selector)
}

fn describe_token(value: &Token) -> String {
    match value {
        Token::Ident(name) => name.to_string(),
        Token::Number { value, int_value, .. } => number_text(*value, *int_value),
        Token::Percentage { unit_value, .. } => format!("{}", unit_value * 100.0),
        Token::Dimension { value, unit, .. } => format!("{}{}", value, unit),
        Token::Hash(name) | Token::IDHash(name) => format!("#{}", name),
        Token::QuotedString(name) => format!("\"{}\"", name),
        Token::Delim(char) => char.to_string(),
        Token::Colon => ":".to_string(),
        Token::Comma => ",".to_string(),
        Token::Semicolon => ";".to_string(),
        Token::Function(name) => format!("{}(", name),
        Token::CurlyBracketBlock => "{".to_string(),
        Token::SquareBracketBlock => "[".to_string(),
        Token::ParenthesisBlock => "(".to_string(),
        Token::UnquotedUrl(url) => format!("url({})", url),
        _ => "token".to_string(),
    }
}

fn parse_declarations_text(text: &str) -> Vec<UssDeclaration> {
    let mut parser = Parser::new(text);
    let mut declarations = Vec::new();
    parse_declaration_list(&mut parser, &mut declarations);
    declarations
}

fn uss_parse_declarations(bytes: &[u8]) -> Vec<UssDeclaration> {
    parse_declarations_text(&String::from_utf8_lossy(bytes))
}

fn uss_parse_sheet(bytes: &[u8]) -> UssSheet {
    let text = String::from_utf8_lossy(bytes);
    let mut sheet = parse_sheet(&text);
    if let std::borrow::Cow::Owned(_) = text {
        // Invalid bytes were replaced with the replacement character; tell
        // the user once, before any other error.
        sheet.errors.insert(
            0,
            UssError {
                line: 1,
                message: "file is not valid UTF-8; invalid bytes were replaced"
                    .to_string(),
            },
        );
    }
    sheet
}

fn parse_sheet(text: &str) -> UssSheet {
    let mut parser = Parser::new(text);
    let mut sheet = UssSheet {
        imports: Vec::new(),
        rules: Vec::new(),
        errors: Vec::new(),
    };
    loop {
        // Whitespace and comments do not start a rule; remember where the
        // real token starts so line numbers and selector text stay exact.
        let Some((line, start, token)) = next_significant(&mut parser) else {
            return sheet;
        };
        match token {
            Token::AtKeyword(name) => {
                parse_at_rule(&mut parser, &mut sheet, &name.to_ascii_lowercase(), line)
            }
            Token::CurlyBracketBlock => {
                sheet.errors.push(UssError {
                    line,
                    message: "expected a selector before '{'".to_string(),
                });
                let _ = parser.parse_nested_block::<_, (), ()>(|_| Ok(()));
            }
            Token::Semicolon => {}  // stray
            _ => parse_rule(&mut parser, &mut sheet, start, line),
        }
    }
}

fn parse_at_rule(parser: &mut Parser, sheet: &mut UssSheet, name: &str, line: u32) {
    // The import target or at-rule arguments, up to ';' or '{'.
    let value_start = parser.position();
    consume_until(parser, Delimiter::Semicolon | Delimiter::CurlyBracketBlock);
    let value = parser.slice_from(value_start).trim().to_string();
    if name == "import" {
        sheet.imports.push(UssImport { text: value, line });
    } else {
        sheet.errors.push(UssError {
            line,
            message: format!("unsupported at-rule @{}", name),
        });
    }
    // Skip the rest of the at-rule. A '{...}' block (@media and friends) is
    // skipped whole and ends the at-rule; '(' and '[' blocks are its
    // arguments, so parsing continues after them.
    loop {
        match parser.next() {
            Err(_) | Ok(Token::Semicolon) => break,
            Ok(Token::CurlyBracketBlock) => {
                let _ = parser.parse_nested_block::<_, (), ()>(|_| Ok(()));
                break;
            }
            Ok(Token::ParenthesisBlock) | Ok(Token::SquareBracketBlock) => {
                let _ = parser.parse_nested_block::<_, (), ()>(|_| Ok(()));
            }
            Ok(_) => {}
        }
    }
}

fn parse_rule(parser: &mut Parser, sheet: &mut UssSheet, start: SourcePosition, line: u32) {
    // The raw selector text, up to '{'.
    consume_until(parser, Delimiter::CurlyBracketBlock);
    let selector_text = parser.slice_from(start).to_string();
    match parser.next() {
        Ok(_) => {}  // the '{' block; its contents are parsed next
        Err(_) => {
            sheet.errors.push(UssError {
                line,
                message: "expected '{'".to_string(),
            });
            return;
        }
    }
    let declarations = parse_block_declarations(parser);

    // em and rem units are not supported: record the error and skip the rule.
    let is_unsupported = |token: &UssToken| {
        token.kind == KIND_DIMENSION && matches!(token.unit.as_str(), "em" | "rem")
    };
    if let Some(declaration) = declarations
        .iter()
        .find(|declaration| declaration.tokens.iter().any(|token| is_unsupported(token)))
    {
        let unit = declaration
            .tokens
            .iter()
            .find(|token| is_unsupported(token))
            .map(|token| token.unit.clone())
            .unwrap_or_default();
        sheet.errors.push(UssError {
            line: declaration.line,
            message: format!("unsupported unit \"{}\"", unit),
        });
        return;
    }

    let mut rule = UssRule {
        line,
        selectors: Vec::new(),
        declarations,
    };
    for part in selector_text.split(',') {
        match parse_one_selector(part) {
            Ok(selector) => rule.selectors.push(selector),
            Err(message) => sheet.errors.push(UssError {
                line,
                message: format!("bad selector \"{}\": {}", part.trim(), message),
            }),
        }
    }
    if !rule.selectors.is_empty() {
        sheet.rules.push(rule);
    }
}

fn parse_block_declarations(parser: &mut Parser) -> Vec<UssDeclaration> {
    let mut declarations = Vec::new();
    let _ = parser.parse_nested_block::<_, (), ()>(|body| {
        parse_declaration_list(body, &mut declarations);
        Ok(())
    });
    declarations
}

/// Parses a bare `name: value; name: value` list (used for a rule body and
/// for an inline style="" attribute).
fn parse_declaration_list(body: &mut Parser, declarations: &mut Vec<UssDeclaration>) {
    loop {
        // The property name; stray tokens before it are skipped.
        let mut name: Option<(u32, String)> = None;
        while name.is_none() {
            let line = body.current_source_location().line + 1;
            let read = body
                .next_including_whitespace_and_comments()
                .ok()
                .map(|token| token.clone());
            name = match read {
                None => return,
                Some(Token::Ident(ident)) => Some((line, ident.to_string())),
                Some(Token::WhiteSpace(_))
                | Some(Token::Comment(_))
                | Some(Token::Semicolon) => None,
                Some(_) => {
                    // Garbage where a property name belongs.
                    skip_to_semicolon(body);
                    None
                }
            };
        }
        let Some((line, name)) = name else {
            return;
        };
        let read = body
            .next_including_whitespace_and_comments()
            .ok()
            .map(|token| token.clone());
        match read {
            Some(Token::Colon) => {}
            Some(_) => {
                // No colon: not a declaration.
                skip_to_semicolon(body);
                continue;
            }
            None => return,
        }
        let value_start = body.position();
        consume_until(body, Delimiter::Semicolon);
        let mut value = body.slice_from(value_start).trim().to_string();
        strip_important(&mut value);
        let tokens = tokenize_value_text(&value);
        declarations.push(UssDeclaration {
            name,
            value,
            tokens,
            line,
        });
    }
}

/// Strips a trailing `!important` from a declaration's value: USS accepts
/// and ignores it, whatever the casing and the spacing after the bang.
fn strip_important(value: &mut String) {
    let trimmed = value.trim_end();
    if let Some(bang) = trimmed.rfind('!') {
        if trimmed[bang + 1..].trim().eq_ignore_ascii_case("important") {
            *value = trimmed[..bang].trim_end().to_string();
        }
    }
}

fn skip_to_semicolon(body: &mut Parser) {
    loop {
        match body.next_including_whitespace_and_comments() {
            Ok(Token::Semicolon) | Err(_) => return,
            Ok(_) => {}
        }
    }
}
