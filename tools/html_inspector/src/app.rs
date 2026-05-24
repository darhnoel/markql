use crossterm::event::{KeyCode, KeyEvent, KeyModifiers, MouseButton, MouseEvent, MouseEventKind};

use crate::dom::{DomTree, NodeId};
use crate::renderer::{render_page, RenderSpan, RenderedPage};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum InspectorMode {
    Details,
    InnerHtml,
}

#[derive(Debug)]
pub struct App {
    input_label: String,
    dom: DomTree,
    rendered: RenderedPage,
    page_width: usize,
    cursor_line: usize,
    cursor_col: usize,
    preferred_col: usize,
    scroll: usize,
    locked_span: Option<usize>,
    inspector_scroll_x: usize,
    inspector_scroll_y: usize,
    inspector_content_width: usize,
    inspector_content_height: usize,
    inspector_view_width: usize,
    inspector_view_height: usize,
    inspector_mode: InspectorMode,
}

impl App {
    pub fn new(input_label: String, dom: DomTree) -> Self {
        let rendered = render_page(&dom, 80);
        let mut app = Self {
            input_label,
            dom,
            rendered,
            page_width: 80,
            cursor_line: 0,
            cursor_col: 0,
            preferred_col: 0,
            scroll: 0,
            locked_span: None,
            inspector_scroll_x: 0,
            inspector_scroll_y: 0,
            inspector_content_width: 0,
            inspector_content_height: 0,
            inspector_view_width: 0,
            inspector_view_height: 0,
            inspector_mode: InspectorMode::Details,
        };
        app.jump_to_span(0);
        app
    }

    pub fn input_label(&self) -> &str {
        &self.input_label
    }

    pub fn dom(&self) -> &DomTree {
        &self.dom
    }

    pub fn rendered(&self) -> &RenderedPage {
        &self.rendered
    }

    pub fn scroll(&self) -> usize {
        self.scroll
    }

    pub fn is_locked(&self) -> bool {
        self.locked_span.is_some()
    }

    pub fn focused_node_id(&self) -> Option<NodeId> {
        self.focused_span().map(|span| span.node_id)
    }

    pub fn inspector_mode(&self) -> InspectorMode {
        self.inspector_mode
    }

    pub fn inspector_scroll_x(&self) -> usize {
        self.inspector_scroll_x
    }

    pub fn inspector_scroll_y(&self) -> usize {
        self.inspector_scroll_y
    }

    pub fn inspector_content_width(&self) -> usize {
        self.inspector_content_width
    }

    pub fn inspector_content_height(&self) -> usize {
        self.inspector_content_height
    }

    pub fn inspector_view_width(&self) -> usize {
        self.inspector_view_width
    }

    pub fn inspector_view_height(&self) -> usize {
        self.inspector_view_height
    }

    pub fn focused_span(&self) -> Option<&RenderSpan> {
        let index = self
            .locked_span
            .or_else(|| self.find_span_at_or_near(self.cursor_line, self.cursor_col))?;
        self.rendered.spans.get(index)
    }

    pub fn ensure_layout(&mut self, page_width: usize, page_height: usize) {
        let page_width = page_width.max(8) as usize;
        if self.page_width != page_width {
            self.page_width = page_width;
            self.rendered = render_page(&self.dom, self.page_width);
            self.reanchor_focus();
        }
        self.ensure_scroll(page_height as usize);
    }

    pub fn invalidate_layout(&mut self) {
        self.page_width = 0;
    }

    pub fn handle_key(&mut self, key: KeyEvent) -> bool {
        if self.is_locked() {
            return self.handle_locked_key(key);
        }

        match key.code {
            KeyCode::Char('q') => true,
            KeyCode::Esc => false,
            KeyCode::Up => {
                self.move_vertical(-1);
                false
            }
            KeyCode::Down => {
                self.move_vertical(1);
                false
            }
            KeyCode::Left => {
                self.move_horizontal(-1);
                false
            }
            KeyCode::Right => {
                self.move_horizontal(1);
                false
            }
            KeyCode::Tab => {
                let backwards = key.modifiers.contains(KeyModifiers::SHIFT);
                self.jump_to_adjacent_span(backwards);
                false
            }
            KeyCode::Enter => {
                self.toggle_lock();
                false
            }
            _ => false,
        }
    }

    pub fn set_inspector_view(&mut self, content_width: usize, content_height: usize, view_width: usize, view_height: usize) {
        self.inspector_content_width = content_width;
        self.inspector_content_height = content_height;
        self.inspector_view_width = view_width;
        self.inspector_view_height = view_height;
        self.clamp_inspector_scroll();
    }

    fn handle_locked_key(&mut self, key: KeyEvent) -> bool {
        match key.code {
            KeyCode::Char('q') => true,
            KeyCode::Esc => {
                self.locked_span = None;
                self.inspector_scroll_x = 0;
                self.inspector_scroll_y = 0;
                self.inspector_mode = InspectorMode::Details;
                false
            }
            KeyCode::Char('i') => {
                self.inspector_mode = match self.inspector_mode {
                    InspectorMode::Details => InspectorMode::InnerHtml,
                    InspectorMode::InnerHtml => InspectorMode::Details,
                };
                self.inspector_scroll_x = 0;
                self.inspector_scroll_y = 0;
                false
            }
            KeyCode::Up => {
                self.inspector_scroll_y = self.inspector_scroll_y.saturating_sub(1);
                false
            }
            KeyCode::Down => {
                self.inspector_scroll_y = self.inspector_scroll_y.saturating_add(1);
                self.clamp_inspector_scroll();
                false
            }
            KeyCode::Left => {
                self.inspector_scroll_x = self.inspector_scroll_x.saturating_sub(1);
                false
            }
            KeyCode::Right => {
                self.inspector_scroll_x = self.inspector_scroll_x.saturating_add(2);
                self.clamp_inspector_scroll();
                false
            }
            KeyCode::Tab => {
                false
            }
            KeyCode::Enter => {
                self.locked_span = None;
                self.inspector_scroll_x = 0;
                self.inspector_scroll_y = 0;
                self.inspector_mode = InspectorMode::Details;
                false
            }
            KeyCode::PageDown => {
                self.inspector_scroll_y = self
                    .inspector_scroll_y
                    .saturating_add(self.inspector_view_height.saturating_sub(1).max(1));
                self.clamp_inspector_scroll();
                false
            }
            KeyCode::PageUp => {
                self.inspector_scroll_y = self
                    .inspector_scroll_y
                    .saturating_sub(self.inspector_view_height.saturating_sub(1).max(1));
                false
            }
            _ => false,
        }
    }

    pub fn handle_mouse(&mut self, event: MouseEvent, width: usize, height: usize) {
        match event.kind {
            MouseEventKind::Down(MouseButton::Left) => {
                let inner_width = width.saturating_sub(2);
                let inner_height = height.saturating_sub(2);
                if inner_width == 0 || inner_height == 0 {
                    return;
                }

                if event.column == 0
                    || event.row == 0
                    || event.column as usize > inner_width
                    || event.row as usize > inner_height
                {
                    self.locked_span = None;
                    return;
                }

                let local_col = event.column.saturating_sub(1) as usize;
                let local_line = event.row.saturating_sub(1) as usize;
                self.cursor_line = (self.scroll + local_line)
                    .min(self.rendered.lines.len().saturating_sub(1));
                self.cursor_col = local_col.min(self.line_char_len(self.cursor_line).saturating_sub(1));
                self.preferred_col = self.cursor_col;
                self.locked_span = self.find_span_at_or_near(self.cursor_line, self.cursor_col);
                self.inspector_scroll_x = 0;
                self.inspector_scroll_y = 0;
            }
            MouseEventKind::ScrollDown => {
                if self.is_locked() {
                    if event.modifiers.contains(KeyModifiers::SHIFT) {
                        self.inspector_scroll_x = self.inspector_scroll_x.saturating_add(2);
                        self.clamp_inspector_scroll();
                    } else {
                        self.inspector_scroll_y = self.inspector_scroll_y.saturating_add(1);
                        self.clamp_inspector_scroll();
                    }
                } else if self.scroll + 1 < self.rendered.lines.len() {
                    self.scroll += 1;
                    self.cursor_line = self.cursor_line.saturating_add(1);
                }
            }
            MouseEventKind::ScrollUp => {
                if self.is_locked() {
                    if event.modifiers.contains(KeyModifiers::SHIFT) {
                        self.inspector_scroll_x = self.inspector_scroll_x.saturating_sub(2);
                    } else {
                        self.inspector_scroll_y = self.inspector_scroll_y.saturating_sub(1);
                    }
                } else {
                    self.scroll = self.scroll.saturating_sub(1);
                    self.cursor_line = self.cursor_line.saturating_sub(1);
                }
            }
            MouseEventKind::ScrollLeft => {
                if self.is_locked() {
                    self.inspector_scroll_x = self.inspector_scroll_x.saturating_sub(2);
                }
            }
            MouseEventKind::ScrollRight => {
                if self.is_locked() {
                    self.inspector_scroll_x = self.inspector_scroll_x.saturating_add(2);
                    self.clamp_inspector_scroll();
                }
            }
            _ => {}
        }
    }

    fn move_vertical(&mut self, delta: isize) {
        let max_line = self.rendered.lines.len().saturating_sub(1) as isize;
        let next_line = (self.cursor_line as isize + delta).clamp(0, max_line) as usize;
        self.cursor_line = next_line;
        let line_len = self.line_char_len(next_line);
        self.cursor_col = self.preferred_col.min(line_len.saturating_sub(1));
        self.locked_span = None;
    }

    fn move_horizontal(&mut self, delta: isize) {
        let line_len = self.line_char_len(self.cursor_line);
        if line_len == 0 {
            self.cursor_col = 0;
            self.preferred_col = 0;
            self.locked_span = None;
            return;
        }
        let max_col = line_len.saturating_sub(1) as isize;
        let next_col = (self.cursor_col as isize + delta).clamp(0, max_col) as usize;
        self.cursor_col = next_col;
        self.preferred_col = next_col;
        self.locked_span = None;
    }

    fn jump_to_adjacent_span(&mut self, backwards: bool) {
        if self.rendered.spans.is_empty() {
            return;
        }
        let current_index = self
            .locked_span
            .or_else(|| self.find_span_at_or_near(self.cursor_line, self.cursor_col))
            .unwrap_or(0);
        let next_index = if backwards {
            current_index.saturating_sub(1)
        } else {
            (current_index + 1).min(self.rendered.spans.len() - 1)
        };
        self.locked_span = None;
        self.jump_to_span(next_index);
    }

    fn toggle_lock(&mut self) {
        if self.locked_span.is_some() {
            self.locked_span = None;
            self.inspector_scroll_x = 0;
            self.inspector_scroll_y = 0;
            self.inspector_mode = InspectorMode::Details;
            return;
        }
        self.locked_span = self.find_span_at_or_near(self.cursor_line, self.cursor_col);
        self.inspector_scroll_x = 0;
        self.inspector_scroll_y = 0;
        self.inspector_mode = InspectorMode::Details;
    }

    fn jump_to_span(&mut self, span_index: usize) {
        if let Some(span) = self.rendered.spans.get(span_index) {
            self.cursor_line = span.line;
            self.cursor_col = span.start_col;
            self.preferred_col = span.start_col;
        }
    }

    fn line_char_len(&self, line_index: usize) -> usize {
        self.rendered
            .lines
            .get(line_index)
            .map(|line| line.chars().count())
            .unwrap_or(0)
    }

    fn find_span_at_or_near(&self, line: usize, col: usize) -> Option<usize> {
        if self.rendered.spans.is_empty() {
            return None;
        }

        let mut best: Option<(usize, usize)> = None;
        for (index, span) in self.rendered.spans.iter().enumerate() {
            let distance = if span.line == line && col >= span.start_col && col < span.end_col {
                0
            } else if span.line == line {
                if col < span.start_col {
                    span.start_col - col
                } else {
                    col.saturating_sub(span.end_col)
                }
            } else {
                line.abs_diff(span.line) * 10 + col.abs_diff(span.start_col)
            };

            match best {
                Some((_, best_distance)) if distance >= best_distance => {}
                _ => best = Some((index, distance)),
            }
        }

        best.map(|(index, _)| index)
    }

    fn reanchor_focus(&mut self) {
        let locked_node = self
            .locked_span
            .and_then(|index| self.rendered.spans.get(index))
            .map(|span| span.node_id);
        self.cursor_line = self.cursor_line.min(self.rendered.lines.len().saturating_sub(1));
        let line_len = self.line_char_len(self.cursor_line);
        if line_len == 0 {
            self.cursor_col = 0;
            self.preferred_col = 0;
        } else {
            self.cursor_col = self.cursor_col.min(line_len.saturating_sub(1));
            self.preferred_col = self.preferred_col.min(line_len.saturating_sub(1));
        }

        self.locked_span = locked_node.and_then(|node_id| {
            self.rendered
                .spans
                .iter()
                .position(|span| span.node_id == node_id)
        });

        if self.rendered.spans.is_empty() {
            self.cursor_line = 0;
            self.cursor_col = 0;
            self.preferred_col = 0;
        } else if self.find_span_at_or_near(self.cursor_line, self.cursor_col).is_none() {
            self.jump_to_span(0);
        }
    }

    fn ensure_scroll(&mut self, page_height: usize) {
        if page_height == 0 {
            self.scroll = 0;
            return;
        }
        if self.cursor_line < self.scroll {
            self.scroll = self.cursor_line;
        } else if self.cursor_line >= self.scroll + page_height {
            self.scroll = self.cursor_line + 1 - page_height;
        }
    }

    fn clamp_inspector_scroll(&mut self) {
        let max_x = self
            .inspector_content_width
            .saturating_sub(self.inspector_view_width);
        let max_y = self
            .inspector_content_height
            .saturating_sub(self.inspector_view_height);
        self.inspector_scroll_x = self.inspector_scroll_x.min(max_x);
        self.inspector_scroll_y = self.inspector_scroll_y.min(max_y);
    }
}
