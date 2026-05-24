use ratatui::layout::Rect;
use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{
    Block, Borders, Clear, Paragraph, Scrollbar, ScrollbarOrientation, ScrollbarState, Wrap,
};
use ratatui::Frame;
use unicode_width::UnicodeWidthStr;

use crate::app::{App, InspectorMode};
use crate::inspector::build_inspector_lines;

pub fn draw(frame: &mut Frame<'_>, app: &mut App) {
    let area = frame.area();
    let inner = pane_inner(area);
    app.ensure_layout(inner.width as usize, inner.height as usize);

    draw_page(frame, app, area);
    if app.is_locked() {
        draw_inspector_popup(frame, app, area);
    }
}

fn draw_page(frame: &mut Frame<'_>, app: &App, area: Rect) {
    let title = fit_title(&format!(" Page: {} ", app.input_label()), area.width);
    let block = Block::default().title(title).borders(Borders::ALL);

    let focused = app.focused_span().cloned();
    let lines: Vec<Line<'static>> = app
        .rendered()
        .lines
        .iter()
        .enumerate()
        .skip(app.scroll())
        .take(area.height.saturating_sub(2) as usize)
        .map(|(line_index, line)| render_line(line_index, line, focused.as_ref()))
        .collect();

    let paragraph = Paragraph::new(lines).block(block).wrap(Wrap { trim: false });
    frame.render_widget(paragraph, area);
}

fn draw_inspector_popup(frame: &mut Frame<'_>, app: &mut App, area: Rect) {
    let content_width = area.width.saturating_sub(2) as usize;
    let popup_width = content_width.clamp(28, 42) as u16;
    let lines = match app.inspector_mode() {
        InspectorMode::Details => build_inspector_lines(app.dom(), app.focused_node_id(), true),
        InspectorMode::InnerHtml => app
            .focused_node_id()
            .map(|node_id| app.dom().format_inner_html(node_id))
            .unwrap_or_else(|| vec!["(empty)".to_string()]),
    };
    let popup_height = (lines.len() as u16 + 4).clamp(10, area.height.saturating_sub(2).max(10));
    let popup_area = anchored_popup_rect(app, area, popup_width, popup_height);
    let content_max_width = lines
        .iter()
        .map(|line| UnicodeWidthStr::width(line.as_str()))
        .max()
        .unwrap_or(0);
    let content_height = lines.len();
    let raw_view_width = popup_area.width.saturating_sub(4) as usize;
    let raw_view_height = popup_area.height.saturating_sub(4) as usize;
    let (show_v_scroll, show_h_scroll, view_width, view_height) =
        inspector_viewport(content_max_width, content_height, raw_view_width, raw_view_height);
    app.set_inspector_view(content_max_width, content_height, view_width, view_height);

    let rendered_lines: Vec<Line<'static>> = lines
        .into_iter()
        .map(|line| Line::from(Span::raw(line)))
        .collect();

    frame.render_widget(Clear, popup_area);
    let title = match app.inspector_mode() {
        InspectorMode::Details => " Inspector ",
        InspectorMode::InnerHtml => " Inner HTML ",
    };
    let block = Block::default()
        .title(title)
        .borders(Borders::ALL)
        .style(Style::default().bg(Color::Black));
    frame.render_widget(block, popup_area);

    let content_area = inspector_content_rect(popup_area, show_v_scroll, show_h_scroll);
    let paragraph = Paragraph::new(rendered_lines)
        .scroll((app.inspector_scroll_y() as u16, app.inspector_scroll_x() as u16))
        .style(Style::default().bg(Color::Black).fg(Color::White));
    frame.render_widget(paragraph, content_area);

    if show_v_scroll {
        let scrollbar_area = Rect {
            x: content_area.x.saturating_add(content_area.width),
            y: content_area.y,
            width: 1,
            height: content_area.height,
        };
        let scroll_range = app
            .inspector_content_height()
            .saturating_sub(app.inspector_view_height())
            .saturating_add(1);
        let mut scrollbar_state = ScrollbarState::new(scroll_range)
            .position(app.inspector_scroll_y())
            .viewport_content_length(1);
        frame.render_stateful_widget(
            Scrollbar::new(ScrollbarOrientation::VerticalRight)
                .thumb_symbol("█")
                .track_symbol(Some("│"))
                .begin_symbol(None)
                .end_symbol(None)
                .thumb_style(Style::default().fg(Color::White).bg(Color::Black))
                .track_style(Style::default().fg(Color::DarkGray).bg(Color::Black)),
            scrollbar_area,
            &mut scrollbar_state,
        );
    }

    if show_h_scroll {
        let scrollbar_area = Rect {
            x: content_area.x,
            y: content_area.y.saturating_add(content_area.height),
            width: content_area.width,
            height: 1,
        };
        let scroll_range = app
            .inspector_content_width()
            .saturating_sub(app.inspector_view_width())
            .saturating_add(1);
        let mut scrollbar_state = ScrollbarState::new(scroll_range)
            .position(app.inspector_scroll_x())
            .viewport_content_length(1);
        frame.render_stateful_widget(
            Scrollbar::new(ScrollbarOrientation::HorizontalBottom)
                .thumb_symbol("█")
                .track_symbol(Some("─"))
                .begin_symbol(None)
                .end_symbol(None)
                .thumb_style(Style::default().fg(Color::White).bg(Color::Black))
                .track_style(Style::default().fg(Color::DarkGray).bg(Color::Black)),
            scrollbar_area,
            &mut scrollbar_state,
        );
    }
}

fn anchored_popup_rect(app: &App, area: Rect, width: u16, height: u16) -> Rect {
    let inner = pane_inner(area);
    let Some(focused) = app.focused_span() else {
        return Rect {
            x: area.x.saturating_add(1),
            y: area.y.saturating_add(1),
            width: width.min(area.width.saturating_sub(2)).max(10),
            height: height.min(area.height.saturating_sub(2)).max(6),
        };
    };

    let visible_line = focused.line.saturating_sub(app.scroll()) as u16;
    let span_start = focused.start_col as u16;
    let span_end = focused.end_col as u16;

    let min_x = area.x.saturating_add(1);
    let max_x = area
        .x
        .saturating_add(area.width.saturating_sub(width).saturating_sub(1));
    let max_y = area
        .y
        .saturating_add(area.height.saturating_sub(height).saturating_sub(1));

    let right_anchor = inner.x.saturating_add(span_end.saturating_add(1));
    let left_anchor = inner
        .x
        .saturating_add(span_start.saturating_sub(width.saturating_sub(1)));
    let fits_right = right_anchor.saturating_add(width) <= area.x.saturating_add(area.width);
    let x = if fits_right {
        right_anchor.min(max_x)
    } else {
        left_anchor.max(min_x).min(max_x)
    };

    let line_y = inner.y.saturating_add(visible_line);
    let below_y = line_y.saturating_add(1);
    let y = if below_y.saturating_add(height) <= area.y.saturating_add(area.height).saturating_sub(1)
    {
        below_y
    } else {
        line_y
            .saturating_sub(height.saturating_sub(1))
            .max(area.y.saturating_add(1))
            .min(max_y)
    };

    Rect {
        x,
        y,
        width: width.min(area.width.saturating_sub(2)).max(10),
        height: height.min(area.height.saturating_sub(2)).max(6),
    }
}

fn render_line(
    line_index: usize,
    text: &str,
    focused: Option<&crate::renderer::RenderSpan>,
) -> Line<'static> {
    let Some(span) = focused.filter(|span| span.line == line_index) else {
        return Line::from(Span::raw(text.to_string()));
    };

    let start_byte = char_index_to_byte(text, span.start_col);
    let end_byte = char_index_to_byte(text, span.end_col);
    let mut segments = Vec::new();

    if start_byte > 0 {
        segments.push(Span::raw(text[..start_byte].to_string()));
    }
    segments.push(Span::styled(
        text[start_byte..end_byte].to_string(),
        Style::default()
            .bg(Color::Yellow)
            .fg(Color::Black)
            .add_modifier(Modifier::BOLD),
    ));
    if end_byte < text.len() {
        segments.push(Span::raw(text[end_byte..].to_string()));
    }

    Line::from(segments)
}

fn pane_inner(area: Rect) -> Rect {
    Rect {
        x: area.x + 1,
        y: area.y + 1,
        width: area.width.saturating_sub(2),
        height: area.height.saturating_sub(2),
    }
}

fn char_index_to_byte(text: &str, char_index: usize) -> usize {
    if char_index == 0 {
        return 0;
    }
    text.char_indices()
        .nth(char_index)
        .map(|(index, _)| index)
        .unwrap_or(text.len())
}

fn fit_title(title: &str, width: u16) -> String {
    let inner_width = width.saturating_sub(2) as usize;
    if inner_width == 0 {
        return String::new();
    }
    if title.chars().count() <= inner_width {
        return title.to_string();
    }

    let mut out = String::new();
    for ch in title.chars().take(inner_width.saturating_sub(1)) {
        out.push(ch);
    }
    out.push('…');
    out
}

fn inspector_viewport(
    content_width: usize,
    content_height: usize,
    raw_view_width: usize,
    raw_view_height: usize,
) -> (bool, bool, usize, usize) {
    let mut show_v = content_height > raw_view_height;
    let mut show_h = content_width > raw_view_width;

    loop {
        let view_width = raw_view_width.saturating_sub(if show_v { 1 } else { 0 });
        let view_height = raw_view_height.saturating_sub(if show_h { 1 } else { 0 });
        let next_show_v = content_height > view_height;
        let next_show_h = content_width > view_width;
        if next_show_v == show_v && next_show_h == show_h {
            return (
                show_v,
                show_h,
                view_width.max(1),
                view_height.max(1),
            );
        }
        show_v = next_show_v;
        show_h = next_show_h;
    }
}

fn inspector_content_rect(area: Rect, show_v_scroll: bool, show_h_scroll: bool) -> Rect {
    Rect {
        x: area.x.saturating_add(2),
        y: area.y.saturating_add(2),
        width: area
            .width
            .saturating_sub(4)
            .saturating_sub(if show_v_scroll { 1 } else { 0 }),
        height: area
            .height
            .saturating_sub(4)
            .saturating_sub(if show_h_scroll { 1 } else { 0 }),
    }
}
