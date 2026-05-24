use unicode_width::UnicodeWidthStr;

use crate::dom::{is_block_tag, normalize_whitespace, DomTree, NodeId, NodeKind};

#[derive(Debug, Clone)]
pub struct RenderedPage {
    pub lines: Vec<String>,
    pub spans: Vec<RenderSpan>,
}

#[derive(Debug, Clone)]
pub struct RenderSpan {
    pub line: usize,
    pub start_col: usize,
    pub end_col: usize,
    pub node_id: NodeId,
}

pub fn render_page(dom: &DomTree, width: usize) -> RenderedPage {
    let mut builder = RenderBuilder::new(width.max(8));
    render_node(dom, dom.document_root(), &mut builder, 0);
    builder.finish()
}

fn render_node(dom: &DomTree, node_id: NodeId, builder: &mut RenderBuilder, list_depth: usize) {
    match &dom.node(node_id).kind {
        NodeKind::Document => {
            for child_id in &dom.node(node_id).children {
                render_node(dom, *child_id, builder, list_depth);
            }
        }
        NodeKind::Text(text) => {
            let normalized = normalize_whitespace(text);
            if normalized.is_empty() {
                builder.mark_space();
            } else if let Some(element_id) = dom.nearest_element_ancestor(node_id) {
                builder.push_text(element_id, &normalized);
            }
        }
        NodeKind::Element(element) => match element.tag.as_str() {
            "ul" | "ol" => {
                builder.ensure_block_break();
                for child_id in &dom.node(node_id).children {
                    render_node(dom, *child_id, builder, list_depth + 1);
                }
                builder.ensure_block_break();
            }
            "li" => {
                builder.ensure_block_break();
                let bullet = if list_depth == 0 {
                    "- "
                } else if list_depth % 2 == 0 {
                    "* "
                } else {
                    "- "
                };
                builder.push_text(node_id, bullet.trim_end());
                builder.push_raw(" ");
                for child_id in &dom.node(node_id).children {
                    render_node(dom, *child_id, builder, list_depth);
                }
                builder.ensure_block_break();
            }
            "table" => {
                builder.ensure_block_break();
                render_table(dom, node_id, builder);
                builder.ensure_block_break();
            }
            tag if heading_level(tag).is_some() => {
                builder.ensure_block_break();
                for child_id in &dom.node(node_id).children {
                    render_node(dom, *child_id, builder, list_depth);
                }
                builder.ensure_block_break();
            }
            tag if is_block_tag(tag) => {
                builder.ensure_block_break();
                for child_id in &dom.node(node_id).children {
                    render_node(dom, *child_id, builder, list_depth);
                }
                builder.ensure_block_break();
            }
            _ => {
                for child_id in &dom.node(node_id).children {
                    render_node(dom, *child_id, builder, list_depth);
                }
            }
        },
    }
}

fn render_table(dom: &DomTree, table_id: NodeId, builder: &mut RenderBuilder) {
    for row_id in dom.descendant_rows(table_id) {
        let cells = dom.child_elements_with_tags(row_id, &["th", "td"]);
        if cells.is_empty() {
            continue;
        }
        builder.ensure_block_break();
        let mut first = true;
        for cell_id in cells {
            if !first {
                builder.push_separator(" | ");
            }
            first = false;
            let text = dom.collect_text(cell_id);
            if text.is_empty() {
                builder.push_text(cell_id, " ");
            } else {
                builder.push_text(cell_id, &text);
            }
        }
        builder.ensure_block_break();
    }
}

fn heading_level(tag: &str) -> Option<usize> {
    match tag {
        "h1" => Some(1),
        "h2" => Some(2),
        "h3" => Some(3),
        "h4" => Some(4),
        "h5" => Some(5),
        "h6" => Some(6),
        _ => None,
    }
}

struct RenderBuilder {
    width: usize,
    lines: Vec<String>,
    spans: Vec<RenderSpan>,
    pending_space: bool,
}

impl RenderBuilder {
    fn new(width: usize) -> Self {
        Self {
            width,
            lines: vec![String::new()],
            spans: Vec::new(),
            pending_space: false,
        }
    }

    fn finish(mut self) -> RenderedPage {
        while self.lines.len() > 1 && self.lines.last().is_some_and(|line| line.is_empty()) {
            self.lines.pop();
        }
        if self.lines.is_empty() {
            self.lines.push(String::new());
        }
        RenderedPage {
            lines: self.lines,
            spans: self.spans,
        }
    }

    fn current_line(&self) -> &String {
        self.lines.last().expect("render buffer always has a line")
    }

    fn current_line_mut(&mut self) -> &mut String {
        self.lines.last_mut().expect("render buffer always has a line")
    }

    fn current_width(&self) -> usize {
        UnicodeWidthStr::width(self.current_line().as_str())
    }

    fn ensure_block_break(&mut self) {
        self.pending_space = false;
        if self.current_line().is_empty() {
            return;
        }
        self.lines.push(String::new());
    }

    fn newline(&mut self) {
        self.pending_space = false;
        self.lines.push(String::new());
    }

    fn mark_space(&mut self) {
        self.pending_space = true;
    }

    fn push_separator(&mut self, text: &str) {
        self.pending_space = false;
        self.push_raw(text);
    }

    fn push_raw(&mut self, text: &str) {
        let width = UnicodeWidthStr::width(text);
        if width == 0 {
            return;
        }
        if self.current_width() + width > self.width && !self.current_line().is_empty() {
            self.newline();
        }
        self.current_line_mut().push_str(text);
    }

    fn push_text(&mut self, node_id: NodeId, text: &str) {
        for word in text.split_whitespace() {
            if self.pending_space && !self.current_line().is_empty() {
                self.push_raw(" ");
            }
            self.pending_space = false;
            self.push_wrapped_word(node_id, word);
            self.pending_space = true;
        }
    }

    fn push_wrapped_word(&mut self, node_id: NodeId, word: &str) {
        let word_width = UnicodeWidthStr::width(word);
        if word_width <= self.width {
            if self.current_width() + word_width > self.width && !self.current_line().is_empty() {
                self.newline();
            }
            self.push_span(node_id, word);
            return;
        }

        let mut chunk = String::new();
        let mut chunk_width = 0;
        for ch in word.chars() {
            let ch_str = ch.to_string();
            let ch_width = UnicodeWidthStr::width(ch_str.as_str()).max(1);
            if chunk_width + ch_width > self.width && !chunk.is_empty() {
                if self.current_width() + chunk_width > self.width && !self.current_line().is_empty()
                {
                    self.newline();
                }
                self.push_span(node_id, &chunk);
                self.newline();
                chunk.clear();
                chunk_width = 0;
            }
            chunk.push(ch);
            chunk_width += ch_width;
        }

        if !chunk.is_empty() {
            if self.current_width() + chunk_width > self.width && !self.current_line().is_empty() {
                self.newline();
            }
            self.push_span(node_id, &chunk);
        }
    }

    fn push_span(&mut self, node_id: NodeId, text: &str) {
        if text.is_empty() {
            return;
        }
        let line_index = self.lines.len() - 1;
        let start_col = self.current_line().chars().count();
        self.current_line_mut().push_str(text);
        let end_col = self.current_line().chars().count();
        self.spans.push(RenderSpan {
            line: line_index,
            start_col,
            end_col,
            node_id,
        });
    }
}
