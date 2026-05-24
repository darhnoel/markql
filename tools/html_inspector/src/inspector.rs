use crate::dom::{DomTree, NodeId};

const PRIORITY_ATTRS: &[&str] = &["id", "class", "href", "src", "role", "name", "type"];

pub fn build_inspector_lines(
    dom: &DomTree,
    node_id: Option<NodeId>,
    locked: bool,
) -> Vec<String> {
    let Some(node_id) = node_id else {
        return vec![
            "No visible element selected.".to_string(),
            String::new(),
            "Move with arrows or Tab.".to_string(),
            "Press q to quit.".to_string(),
        ];
    };

    let Some(element) = dom.element(node_id) else {
        return vec!["Focused content is not an element.".to_string()];
    };

    let mut lines = Vec::new();
    lines.push(if locked {
        "Selection: locked".to_string()
    } else {
        "Selection: live".to_string()
    });
    lines.push(String::new());
    lines.push(format!("tag: {}", element.tag));
    lines.push(String::new());
    lines.push("attributes:".to_string());

    let mut wrote_attr = false;
    for key in PRIORITY_ATTRS {
        if let Some(value) = element.attrs.get(*key) {
            let rendered = render_attribute_line(key, value);
            lines.extend(rendered);
            wrote_attr = true;
        }
    }

    let mut extra_attrs: Vec<_> = element
        .attrs
        .iter()
        .filter(|(key, _)| {
            !PRIORITY_ATTRS.contains(&key.as_str())
                && (key.starts_with("data-") || key.starts_with("aria-"))
        })
        .collect();
    extra_attrs.sort_by(|left, right| left.0.cmp(right.0));
    for (key, value) in extra_attrs {
        let rendered = render_attribute_line(key, value);
        lines.extend(rendered);
        wrote_attr = true;
    }

    if !wrote_attr {
        lines.push("  (none)".to_string());
    }

    lines.push(String::new());
    lines.push("DOM path:".to_string());
    let mut path = dom.dom_path(node_id);
    path.reverse();
    lines.push(".".to_string());
    for (index, path_id) in path.iter().enumerate() {
        let prefix = format!("{}└── ", "    ".repeat(index));
        lines.push(format!("{prefix}{}", compact_dom_label(dom, *path_id)));
    }

    lines
}

fn render_attribute_line(key: &str, value: &str) -> Vec<String> {
    let prefix = format!("  {key}: ");
    let summarized = summarize_attr_value(key, value);
    wrap_with_prefix(&prefix, &summarized)
}

fn summarize_attr_value(key: &str, value: &str) -> String {
    let normalized = value.split_whitespace().collect::<Vec<_>>().join(" ");
    if normalized.is_empty() {
        return "(empty)".to_string();
    }

    if key == "class" {
        let classes: Vec<_> = normalized.split_whitespace().collect();
        if classes.len() > 2 {
            let preview = classes[..2].join(" ");
            return format!("{preview} (+{} more)", classes.len() - 2);
        }
    }

    normalized
}

fn compact_dom_label(dom: &DomTree, node_id: NodeId) -> String {
    let Some(element) = dom.element(node_id) else {
        return "#text".to_string();
    };

    if let Some(id) = element.attrs.get("id") {
        return format!("{}#{}", element.tag, id);
    }

    if let Some(class_attr) = element.attrs.get("class") {
        if let Some(first_class) = class_attr.split_whitespace().next() {
            return format!("{}.{}", element.tag, first_class);
        }
    }

    element.tag.clone()
}

fn wrap_with_prefix(prefix: &str, value: &str) -> Vec<String> {
    let indent = " ".repeat(prefix.len());
    let mut lines = Vec::new();
    for (index, part) in value.split('\n').enumerate() {
        if index == 0 {
            lines.push(format!("{prefix}{part}"));
        } else {
            lines.push(format!("{indent}{part}"));
        }
    }
    if lines.is_empty() {
        lines.push(format!("{prefix}{value}"));
    }
    lines
}
