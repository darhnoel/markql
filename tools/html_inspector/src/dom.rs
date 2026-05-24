use std::collections::BTreeMap;

use anyhow::Result;

pub type NodeId = usize;

#[derive(Debug, Clone)]
pub struct DomTree {
    nodes: Vec<DomNode>,
    root_id: NodeId,
}

#[derive(Debug, Clone)]
pub struct DomNode {
    pub id: NodeId,
    pub parent: Option<NodeId>,
    pub children: Vec<NodeId>,
    pub kind: NodeKind,
}

#[derive(Debug, Clone)]
pub enum NodeKind {
    Document,
    Element(ElementData),
    Text(String),
}

#[derive(Debug, Clone)]
pub struct ElementData {
    pub tag: String,
    pub attrs: BTreeMap<String, String>,
}

impl DomTree {
    pub fn parse(html: &str) -> Result<Self> {
        let mut parser = Parser::new(html);
        parser.parse()?;
        Ok(Self {
            nodes: parser.nodes,
            root_id: parser.root_id,
        })
    }

    pub fn node(&self, id: NodeId) -> &DomNode {
        &self.nodes[id]
    }

    pub fn document_root(&self) -> NodeId {
        self.find_first_tag("body")
            .or_else(|| self.find_first_tag("html"))
            .unwrap_or(self.root_id)
    }

    pub fn find_first_tag(&self, tag: &str) -> Option<NodeId> {
        self.nodes.iter().find_map(|node| match &node.kind {
            NodeKind::Element(data) if data.tag == tag => Some(node.id),
            _ => None,
        })
    }

    pub fn nearest_element_ancestor(&self, id: NodeId) -> Option<NodeId> {
        let mut cursor = Some(id);
        while let Some(node_id) = cursor {
            if matches!(self.node(node_id).kind, NodeKind::Element(_)) {
                return Some(node_id);
            }
            cursor = self.node(node_id).parent;
        }
        None
    }

    pub fn element(&self, id: NodeId) -> Option<&ElementData> {
        match &self.node(id).kind {
            NodeKind::Element(data) => Some(data),
            _ => None,
        }
    }

    pub fn dom_path(&self, id: NodeId) -> Vec<NodeId> {
        let mut path = Vec::new();
        let mut cursor = Some(id);
        while let Some(node_id) = cursor {
            if matches!(self.node(node_id).kind, NodeKind::Element(_)) {
                path.push(node_id);
            }
            cursor = self.node(node_id).parent;
        }
        path
    }

    pub fn descendant_rows(&self, table_id: NodeId) -> Vec<NodeId> {
        let mut rows = Vec::new();
        self.collect_rows(table_id, &mut rows);
        rows
    }

    pub fn child_elements_with_tags(&self, parent_id: NodeId, tags: &[&str]) -> Vec<NodeId> {
        self.node(parent_id)
            .children
            .iter()
            .copied()
            .filter(|child_id| {
                self.element(*child_id)
                    .map(|data| tags.iter().any(|tag| data.tag == *tag))
                    .unwrap_or(false)
            })
            .collect()
    }

    pub fn collect_text(&self, id: NodeId) -> String {
        let mut buffer = String::new();
        self.collect_text_inner(id, &mut buffer);
        normalize_whitespace(&buffer)
    }

    pub fn format_inner_html(&self, id: NodeId) -> Vec<String> {
        let mut lines = Vec::new();
        for child_id in &self.node(id).children {
            self.format_node_html(*child_id, 0, &mut lines);
        }
        if lines.is_empty() {
            lines.push("(empty)".to_string());
        }
        lines
    }

    fn collect_rows(&self, node_id: NodeId, rows: &mut Vec<NodeId>) {
        if self
            .element(node_id)
            .map(|data| data.tag == "tr")
            .unwrap_or(false)
        {
            rows.push(node_id);
        }
        for child_id in &self.node(node_id).children {
            self.collect_rows(*child_id, rows);
        }
    }

    fn collect_text_inner(&self, node_id: NodeId, buffer: &mut String) {
        match &self.node(node_id).kind {
            NodeKind::Text(text) => {
                if !buffer.is_empty() {
                    buffer.push(' ');
                }
                buffer.push_str(text);
            }
            _ => {
                for child_id in &self.node(node_id).children {
                    self.collect_text_inner(*child_id, buffer);
                }
            }
        }
    }

    fn format_node_html(&self, node_id: NodeId, indent: usize, lines: &mut Vec<String>) {
        match &self.node(node_id).kind {
            NodeKind::Text(text) => {
                let normalized = normalize_whitespace(text);
                if !normalized.is_empty() {
                    lines.push(format!("{}{}", "  ".repeat(indent), escape_text(&normalized)));
                }
            }
            NodeKind::Element(element) => {
                let attrs = format_attrs(&element.attrs);
                let pad = "  ".repeat(indent);
                if self.node(node_id).children.is_empty() && is_void_tag(&element.tag) {
                    lines.push(format!("{pad}<{}{} />", element.tag, attrs));
                    return;
                }

                lines.push(format!("{pad}<{}{}>", element.tag, attrs));
                for child_id in &self.node(node_id).children {
                    self.format_node_html(*child_id, indent + 1, lines);
                }
                lines.push(format!("{pad}</{}>", element.tag));
            }
            NodeKind::Document => {
                for child_id in &self.node(node_id).children {
                    self.format_node_html(*child_id, indent, lines);
                }
            }
        }
    }
}

pub fn is_block_tag(tag: &str) -> bool {
    matches!(
        tag,
        "html"
            | "body"
            | "div"
            | "p"
            | "section"
            | "article"
            | "header"
            | "footer"
            | "main"
            | "aside"
            | "nav"
            | "ul"
            | "ol"
            | "li"
            | "table"
            | "tr"
            | "td"
            | "th"
            | "h1"
            | "h2"
            | "h3"
            | "h4"
            | "h5"
            | "h6"
    )
}

pub fn normalize_whitespace(input: &str) -> String {
    input.split_whitespace().collect::<Vec<_>>().join(" ")
}

struct Parser<'a> {
    input: &'a str,
    cursor: usize,
    nodes: Vec<DomNode>,
    stack: Vec<NodeId>,
    root_id: NodeId,
}

impl<'a> Parser<'a> {
    fn new(input: &'a str) -> Self {
        let root = DomNode {
            id: 0,
            parent: None,
            children: Vec::new(),
            kind: NodeKind::Document,
        };
        Self {
            input,
            cursor: 0,
            nodes: vec![root],
            stack: vec![0],
            root_id: 0,
        }
    }

    fn parse(&mut self) -> Result<()> {
        while self.cursor < self.input.len() {
            if self.starts_with("<!--") {
                self.skip_comment();
            } else if self.starts_with("</") {
                self.parse_end_tag()?;
            } else if self.starts_with("<!") || self.starts_with("<?") {
                self.skip_bang();
            } else if self.starts_with("<") {
                self.parse_start_tag()?;
            } else {
                self.parse_text();
            }
        }
        Ok(())
    }

    fn starts_with(&self, prefix: &str) -> bool {
        self.input[self.cursor..].starts_with(prefix)
    }

    fn current_parent(&self) -> NodeId {
        *self.stack.last().expect("parser always has a parent node")
    }

    fn parse_text(&mut self) {
        let next_tag = self.input[self.cursor..]
            .find('<')
            .map(|offset| self.cursor + offset)
            .unwrap_or(self.input.len());
        let text = decode_entities(&self.input[self.cursor..next_tag]);
        self.cursor = next_tag;
        if text.trim().is_empty() {
            return;
        }
        self.add_node(NodeKind::Text(text), self.current_parent());
    }

    fn parse_start_tag(&mut self) -> Result<()> {
        self.cursor += 1;
        self.skip_whitespace();
        let tag = self.parse_tag_name();
        if tag.is_empty() {
            self.parse_text_literal("<");
            return Ok(());
        }

        let mut attrs = BTreeMap::new();
        let mut self_closing = false;
        loop {
            self.skip_whitespace();
            if self.cursor >= self.input.len() {
                break;
            }
            if self.starts_with("/>") {
                self.cursor += 2;
                self_closing = true;
                break;
            }
            if self.starts_with(">") {
                self.cursor += 1;
                break;
            }

            let attr_name = self.parse_attr_name();
            if attr_name.is_empty() {
                self.cursor += 1;
                continue;
            }
            self.skip_whitespace();
            let attr_value = if self.starts_with("=") {
                self.cursor += 1;
                self.skip_whitespace();
                self.parse_attr_value()
            } else {
                String::new()
            };
            attrs.insert(attr_name, attr_value);
        }

        let node_id = self.add_node(
            NodeKind::Element(ElementData {
                tag: tag.clone(),
                attrs,
            }),
            self.current_parent(),
        );

        if tag == "script" || tag == "style" {
            if !self_closing {
                self.stack.push(node_id);
            }
            self.capture_raw_text_until(node_id, &tag);
            return Ok(());
        }

        if !self_closing && !is_void_tag(&tag) {
            self.stack.push(node_id);
        }
        Ok(())
    }

    fn parse_end_tag(&mut self) -> Result<()> {
        self.cursor += 2;
        self.skip_whitespace();
        let tag = self.parse_tag_name();
        while self.cursor < self.input.len() && !self.starts_with(">") {
            self.cursor += 1;
        }
        if self.starts_with(">") {
            self.cursor += 1;
        }

        if tag.is_empty() {
            return Ok(());
        }

        if let Some(position) = self.stack.iter().rposition(|node_id| {
            matches!(
                &self.nodes[*node_id].kind,
                NodeKind::Element(element) if element.tag == tag
            )
        }) {
            self.stack.truncate(position);
        }
        Ok(())
    }

    fn capture_raw_text_until(&mut self, parent_id: NodeId, tag: &str) {
        let close = format!("</{tag}");
        let next = self.input[self.cursor..]
            .find(&close)
            .map(|offset| self.cursor + offset)
            .unwrap_or(self.input.len());
        let text = decode_entities(&self.input[self.cursor..next]);
        if !text.trim().is_empty() {
            self.add_node(NodeKind::Text(text), parent_id);
        }
        self.cursor = next;
    }

    fn parse_text_literal(&mut self, text: &str) {
        self.add_node(NodeKind::Text(text.to_string()), self.current_parent());
    }

    fn skip_comment(&mut self) {
        if let Some(end) = self.input[self.cursor + 4..].find("-->") {
            self.cursor += 4 + end + 3;
        } else {
            self.cursor = self.input.len();
        }
    }

    fn skip_bang(&mut self) {
        if let Some(end) = self.input[self.cursor..].find('>') {
            self.cursor += end + 1;
        } else {
            self.cursor = self.input.len();
        }
    }

    fn skip_whitespace(&mut self) {
        while let Some(ch) = self.peek_char() {
            if !ch.is_whitespace() {
                break;
            }
            self.cursor += ch.len_utf8();
        }
    }

    fn peek_char(&self) -> Option<char> {
        self.input[self.cursor..].chars().next()
    }

    fn parse_tag_name(&mut self) -> String {
        self.take_while(|ch| ch.is_alphanumeric() || matches!(ch, '-' | '_' | ':'))
            .to_ascii_lowercase()
    }

    fn parse_attr_name(&mut self) -> String {
        self.take_while(|ch| {
            !ch.is_whitespace() && !matches!(ch, '=' | '>' | '/' | '"' | '\'')
        })
        .to_ascii_lowercase()
    }

    fn parse_attr_value(&mut self) -> String {
        let Some(ch) = self.peek_char() else {
            return String::new();
        };

        if ch == '"' || ch == '\'' {
            self.cursor += ch.len_utf8();
            let start = self.cursor;
            while let Some(next) = self.peek_char() {
                if next == ch {
                    let value = decode_entities(&self.input[start..self.cursor]);
                    self.cursor += ch.len_utf8();
                    return value;
                }
                self.cursor += next.len_utf8();
            }
            return decode_entities(&self.input[start..]);
        }

        let raw = self.take_while(|next| !next.is_whitespace() && !matches!(next, '>' | '/'));
        decode_entities(&raw)
    }

    fn take_while<F>(&mut self, predicate: F) -> String
    where
        F: Fn(char) -> bool,
    {
        let start = self.cursor;
        while let Some(ch) = self.peek_char() {
            if !predicate(ch) {
                break;
            }
            self.cursor += ch.len_utf8();
        }
        self.input[start..self.cursor].to_string()
    }

    fn add_node(&mut self, kind: NodeKind, parent: NodeId) -> NodeId {
        let node_id = self.nodes.len();
        self.nodes.push(DomNode {
            id: node_id,
            parent: Some(parent),
            children: Vec::new(),
            kind,
        });
        self.nodes[parent].children.push(node_id);
        node_id
    }
}

fn decode_entities(input: &str) -> String {
    input
        .replace("&nbsp;", " ")
        .replace("&amp;", "&")
        .replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&quot;", "\"")
        .replace("&apos;", "'")
}

fn escape_text(input: &str) -> String {
    input
        .replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
}

fn escape_attr(input: &str) -> String {
    input
        .replace('&', "&amp;")
        .replace('"', "&quot;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
}

fn format_attrs(attrs: &BTreeMap<String, String>) -> String {
    let mut out = String::new();
    for (key, value) in attrs {
        if value.is_empty() {
            out.push(' ');
            out.push_str(key);
        } else {
            out.push(' ');
            out.push_str(key);
            out.push_str("=\"");
            out.push_str(&escape_attr(value));
            out.push('"');
        }
    }
    out
}

fn is_void_tag(tag: &str) -> bool {
    matches!(
        tag,
        "area"
            | "base"
            | "br"
            | "col"
            | "embed"
            | "hr"
            | "img"
            | "input"
            | "link"
            | "meta"
            | "param"
            | "source"
            | "track"
            | "wbr"
    )
}

#[cfg(test)]
mod tests {
    use super::DomTree;

    #[test]
    fn script_end_tag_does_not_collapse_parent_stack() {
        let dom = DomTree::parse(
            r#"<body><div id="wrapper"><script type="text/javascript">header();</script><div class="pageTitle01"><h1>Title</h1></div></div></body>"#,
        )
        .expect("valid html");

        let body_id = dom.find_first_tag("body").expect("body");
        let wrapper_id = dom
            .node(body_id)
            .children
            .iter()
            .copied()
            .find(|node_id| dom.element(*node_id).is_some())
            .expect("wrapper");

        let wrapper = dom.element(wrapper_id).expect("wrapper element");
        assert_eq!(wrapper.tag, "div");
        assert_eq!(wrapper.attrs.get("id").map(String::as_str), Some("wrapper"));

        let child_tags: Vec<&str> = dom
            .node(wrapper_id)
            .children
            .iter()
            .filter_map(|node_id| dom.element(*node_id).map(|element| element.tag.as_str()))
            .collect();
        assert_eq!(child_tags, vec!["script", "div"]);
    }
}
