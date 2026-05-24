use std::collections::HashMap;

use crate::dom::{DomTree, ElementData, NodeId, NodeKind};

pub(crate) const MAX_DEPTH: usize = 10;
const MIN_FOLD_RUN: usize = 3;
const PRIORITY_FLAG_ATTRS: &[&str] = &["href", "src", "role", "type", "name"];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum FoldedChild {
    Single(NodeId),
    Run { exemplar: NodeId, count: usize },
}

pub fn print_skeleton(dom: &DomTree) {
    for line in render_skeleton(dom, MAX_DEPTH) {
        println!("{line}");
    }
}

pub(crate) fn render_skeleton(dom: &DomTree, max_depth: usize) -> Vec<String> {
    let mut lines = Vec::new();
    let mut signature_cache = HashMap::new();
    let roots = skeleton_roots(dom);

    for (index, node_id) in roots.iter().enumerate() {
        if index > 0 {
            lines.push(String::new());
        }
        render_node(
            dom,
            FoldedChild::Single(*node_id),
            "",
            true,
            0,
            max_depth,
            &mut signature_cache,
            &mut lines,
        );
    }

    lines
}

pub(crate) fn skeleton_roots(dom: &DomTree) -> Vec<NodeId> {
    if let Some(body_id) = dom.find_first_tag("body") {
        let body_children = child_elements(dom, body_id);
        if !body_children.is_empty() {
            return body_children;
        }
    }

    let root_id = dom.document_root();
    if let Some(element) = dom.element(root_id) {
        let children = child_elements(dom, root_id);
        if matches!(element.tag.as_str(), "html" | "body") && !children.is_empty() {
            return children;
        }
        return vec![root_id];
    }

    child_elements(dom, root_id)
}

pub(crate) fn child_elements(dom: &DomTree, node_id: NodeId) -> Vec<NodeId> {
    dom.node(node_id)
        .children
        .iter()
        .copied()
        .filter(|child_id| matches!(dom.node(*child_id).kind, NodeKind::Element(_)))
        .collect()
}

pub(crate) fn format_label(element: &ElementData) -> String {
    let mut parts = vec![element.tag.clone()];

    if let Some(id) = element
        .attrs
        .get("id")
        .map(|value| value.trim())
        .filter(|value| !value.is_empty())
    {
        parts.push(format!("#{id}"));
    }

    if let Some(class_attr) = element.attrs.get("class") {
        let classes: Vec<String> = class_attr
            .split_whitespace()
            .map(|class_name| format!(".{class_name}"))
            .collect();
        if !classes.is_empty() {
            parts.push(format!("[{}]", classes.join(" ")));
        }
    }

    for key in PRIORITY_FLAG_ATTRS {
        if element.attrs.contains_key(*key) {
            parts.push(format!("[{key}]"));
        }
    }

    for key in element.attrs.keys() {
        if key.starts_with("data-") || key.starts_with("aria-") {
            parts.push(format!("[{key}]"));
        }
    }

    parts.join(" ")
}

pub(crate) fn fold_sibling_runs(
    dom: &DomTree,
    children: &[NodeId],
    depth_left: usize,
) -> Vec<FoldedChild> {
    let mut signature_cache = HashMap::new();
    fold_sibling_runs_with_cache(dom, children, depth_left, &mut signature_cache)
}

fn render_node(
    dom: &DomTree,
    node: FoldedChild,
    prefix: &str,
    is_last: bool,
    depth: usize,
    max_depth: usize,
    signature_cache: &mut HashMap<(NodeId, usize), String>,
    lines: &mut Vec<String>,
) {
    let (node_id, multiplier) = match node {
        FoldedChild::Single(node_id) => (node_id, None),
        FoldedChild::Run { exemplar, count } => (exemplar, Some(count)),
    };

    let Some(element) = dom.element(node_id) else {
        return;
    };

    let branch = if depth == 0 {
        String::new()
    } else if is_last {
        format!("{prefix}└── ")
    } else {
        format!("{prefix}├── ")
    };
    let mut label = format_label(element);
    if let Some(count) = multiplier {
        label.push_str(&format!(" × {count}"));
    }
    lines.push(format!("{branch}{label}"));

    let child_prefix = if depth == 0 {
        String::new()
    } else if is_last {
        format!("{prefix}    ")
    } else {
        format!("{prefix}│   ")
    };
    let children = child_elements(dom, node_id);
    if children.is_empty() {
        return;
    }

    if depth >= max_depth {
        lines.push(format!("{child_prefix}└── ..."));
        return;
    }

    let folded_children = fold_sibling_runs_with_cache(
        dom,
        &children,
        max_depth.saturating_sub(depth + 1),
        signature_cache,
    );
    for (index, child) in folded_children.iter().enumerate() {
        render_node(
            dom,
            *child,
            &child_prefix,
            index + 1 == folded_children.len(),
            depth + 1,
            max_depth,
            signature_cache,
            lines,
        );
    }
}

fn fold_sibling_runs_with_cache(
    dom: &DomTree,
    children: &[NodeId],
    depth_left: usize,
    signature_cache: &mut HashMap<(NodeId, usize), String>,
) -> Vec<FoldedChild> {
    let mut folded = Vec::new();
    let mut index = 0;

    while index < children.len() {
        let node_id = children[index];
        let signature = shape_signature_with_cache(dom, node_id, depth_left, signature_cache);
        let mut end = index + 1;
        while end < children.len() {
            let next_signature =
                shape_signature_with_cache(dom, children[end], depth_left, signature_cache);
            if next_signature != signature {
                break;
            }
            end += 1;
        }

        let count = end - index;
        if count >= MIN_FOLD_RUN {
            folded.push(FoldedChild::Run {
                exemplar: node_id,
                count,
            });
        } else {
            for child_id in &children[index..end] {
                folded.push(FoldedChild::Single(*child_id));
            }
        }
        index = end;
    }

    folded
}

fn shape_signature_with_cache(
    dom: &DomTree,
    node_id: NodeId,
    depth_left: usize,
    signature_cache: &mut HashMap<(NodeId, usize), String>,
) -> String {
    if let Some(signature) = signature_cache.get(&(node_id, depth_left)) {
        return signature.clone();
    }

    let signature = shape_signature(dom, node_id, depth_left, signature_cache);
    signature_cache.insert((node_id, depth_left), signature.clone());
    signature
}

fn shape_signature(
    dom: &DomTree,
    node_id: NodeId,
    depth_left: usize,
    signature_cache: &mut HashMap<(NodeId, usize), String>,
) -> String {
    let Some(element) = dom.element(node_id) else {
        return String::new();
    };

    let label = format_label(element);
    let children = child_elements(dom, node_id);
    if children.is_empty() {
        return label;
    }

    if depth_left == 0 {
        return format!("{label}(...)");
    }

    let child_signatures: Vec<String> = children
        .iter()
        .map(|child_id| {
            shape_signature_with_cache(dom, *child_id, depth_left.saturating_sub(1), signature_cache)
        })
        .collect();
    format!("{label}({})", child_signatures.join("|"))
}

#[cfg(test)]
mod tests {
    use super::{FoldedChild, fold_sibling_runs, format_label, render_skeleton};
    use crate::dom::DomTree;

    #[test]
    fn renders_filtered_attributes_without_text() {
        let dom = DomTree::parse(
            r#"<div class="result" data-kind="flight"><h3>Tokyo</h3><span class="stop">1 stop</span><span role="text">¥12,300</span></div>"#,
        )
        .expect("valid html");

        let lines = render_skeleton(&dom, 10);

        assert_eq!(
            lines,
            vec![
                "div [.result] [data-kind]",
                "├── h3",
                "├── span [.stop]",
                "└── span [role]",
            ]
        );
    }

    #[test]
    fn formats_id_class_and_structural_flags() {
        let dom = DomTree::parse(
            r#"<a id="main" class="a b" href="/x" aria-label="Close" title="ignored"></a>"#,
        )
        .expect("valid html");
        let lines = render_skeleton(&dom, 10);
        assert_eq!(lines, vec!["a #main [.a .b] [href] [aria-label]"]);

        let node_id = dom.node(0).children[0];
        let element = dom.element(node_id).expect("element root");
        assert_eq!(format_label(element), "a #main [.a .b] [href] [aria-label]");
    }

    #[test]
    fn folds_repeated_sibling_runs() {
        let dom = DomTree::parse(
            r#"<ul><li><a href="/a"></a></li><li><a href="/a"></a></li><li><a href="/a"></a></li></ul>"#,
        )
        .expect("valid html");
        let ul_id = dom.find_first_tag("ul").expect("ul");
        let children = super::child_elements(&dom, ul_id);

        let folded = fold_sibling_runs(&dom, &children, 10);

        assert_eq!(
            folded,
            vec![FoldedChild::Run {
                exemplar: children[0],
                count: 3,
            }]
        );
        assert_eq!(render_skeleton(&dom, 10), vec!["ul", "└── li × 3", "    └── a [href]"]);
    }
}
