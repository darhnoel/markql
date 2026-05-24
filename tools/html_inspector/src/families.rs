use std::collections::{BTreeMap, BTreeSet};

use crate::dom::{DomTree, ElementData, NodeId};
use crate::skeleton::{
    FoldedChild, MAX_DEPTH, child_elements, fold_sibling_runs, format_label, render_skeleton,
    skeleton_roots,
};

const COMPACT_FAMILY_LIMIT: usize = 8;

pub fn print_family_report(dom: &DomTree) {
    for line in render_family_report(dom, MAX_DEPTH) {
        println!("{line}");
    }
}

pub fn print_family_report_compact(dom: &DomTree) {
    for line in render_family_report_compact(dom, MAX_DEPTH) {
        println!("{line}");
    }
}

fn render_family_report(dom: &DomTree, max_depth: usize) -> Vec<String> {
    let mut lines = render_skeleton(dom, max_depth);
    lines.push(String::new());
    lines.push("Detected Row Families".to_string());
    lines.push(String::new());

    let families = analyze_families(dom, max_depth);

    if families.is_empty() {
        lines.push("(none)".to_string());
        return lines;
    }

    for (index, family) in families.iter().enumerate() {
        if index > 0 {
            lines.push(String::new());
        }
        lines.push(format!("Family {}", family.id));
        lines.push(format!("parent_tag: {}", family.parent_tag));
        lines.push(format!("repeated_tag: {}", family.repeated_tag));
        lines.push(format!("count: {}", family.count));
        lines.push(format!(
            "likely_data_family: {}",
            yes_no(family.likely_data_family)
        ));
        lines.push(format!(
            "likely_header_family: {}",
            yes_no(family.likely_header_family)
        ));
        lines.push(format!(
            "distinguishing_markers: {}",
            render_items(&family.distinguishing_markers)
        ));
        lines.push(format!(
            "required_children: {}",
            render_items(&family.required_children)
        ));
        lines.push(format!(
            "required_descendants: {}",
            render_items(&family.required_descendants)
        ));
        lines.push("slot_hints:".to_string());
        for slot in &family.slot_hints {
            lines.push(format!("- {slot}"));
        }
        lines.push(format!(
            "recommended_extraction_mode: {}",
            family.recommended_extraction_mode
        ));
        lines.push("exemplar_shape:".to_string());
        for shape_line in &family.exemplar_shape {
            lines.push(format!("  {shape_line}"));
        }
    }

    lines
}

fn render_family_report_compact(dom: &DomTree, max_depth: usize) -> Vec<String> {
    let families = analyze_families(dom, max_depth);
    let ranked = rank_families(&families);

    let mut lines = vec![
        "FAM".to_string(),
        "KIND: D=data, H=header, U=unknown".to_string(),
        "MODE: F=FLATTEN, P=PROJECT".to_string(),
        String::new(),
    ];

    for family in ranked.into_iter().take(COMPACT_FAMILY_LIMIT) {
        lines.push(format!(
            "{}|{}>{}|{}|{}|{}|slot:{}|sig:{}",
            family.id,
            family.parent_tag,
            family.repeated_tag,
            family.count,
            compact_kind_code(family),
            compact_mode_code(family),
            strongest_slot_hint(family),
            shallow_signature(dom, family.exemplar_id, 1),
        ));
    }

    if lines.len() == 4 {
        lines.push("(none)".to_string());
    }

    lines
}

#[derive(Debug, Clone)]
struct RowFamily {
    id: String,
    exemplar_id: NodeId,
    parent_tag: String,
    repeated_tag: String,
    count: usize,
    table_family: bool,
    likely_data_family: bool,
    likely_header_family: bool,
    distinguishing_markers: Vec<String>,
    required_children: Vec<String>,
    required_descendants: Vec<String>,
    slot_hints: Vec<String>,
    recommended_extraction_mode: &'static str,
    exemplar_shape: Vec<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
struct FamilyRankKey {
    table_data_priority: usize,
    data_priority: usize,
    repeat_count: usize,
    informative_descendant_count: usize,
    table_priority: usize,
    non_header_priority: usize,
}

fn collect_row_families(dom: &DomTree, node_id: NodeId, depth_left: usize, families: &mut Vec<RowFamily>) {
    if depth_left == 0 {
        return;
    }

    let children = child_elements(dom, node_id);
    if children.is_empty() {
        return;
    }

    let folded = fold_sibling_runs(dom, &children, depth_left.saturating_sub(1));
    let highest_repeat_count = folded
        .iter()
        .filter_map(|child| match child {
            FoldedChild::Run { count, .. } => Some(*count),
            FoldedChild::Single(_) => None,
        })
        .max()
        .unwrap_or(0);

    for child in &folded {
        match *child {
            FoldedChild::Single(child_id) => {
                collect_row_families(dom, child_id, depth_left.saturating_sub(1), families);
            }
            FoldedChild::Run { exemplar, count } => {
                if should_promote_row_family(dom, exemplar, count) {
                    families.push(build_row_family(dom, node_id, exemplar, count, highest_repeat_count, &folded));
                }
                collect_row_families(dom, exemplar, depth_left.saturating_sub(1), families);
            }
        }
    }
}

fn should_promote_row_family(dom: &DomTree, exemplar_id: NodeId, count: usize) -> bool {
    if count < 3 {
        return false;
    }

    let Some(exemplar) = dom.element(exemplar_id) else {
        return false;
    };
    if exemplar.tag == "tr"
        && dom
            .node(exemplar_id)
            .parent
            .and_then(|parent_id| dom.element(parent_id))
            .map(|element| matches!(element.tag.as_str(), "table" | "tbody" | "thead" | "tfoot"))
            .unwrap_or(false)
    {
        return false;
    }
    if matches!(exemplar.tag.as_str(), "td" | "th" | "span" | "a" | "img" | "br") {
        return false;
    }

    matches!(exemplar.tag.as_str(), "li" | "tr")
        || !child_elements(dom, exemplar_id).is_empty()
}

fn build_row_family(
    dom: &DomTree,
    parent_id: NodeId,
    exemplar_id: NodeId,
    count: usize,
    highest_repeat_count: usize,
    sibling_groups: &[FoldedChild],
) -> RowFamily {
    let exemplar = dom.element(exemplar_id).expect("row family exemplar must be an element");
    let parent_tag = dom
        .element(parent_id)
        .map(|element| element.tag.clone())
        .unwrap_or_else(|| "#document".to_string());
    let direct_children = child_elements(dom, exemplar_id);
    let direct_child_labels = count_labels(direct_children.iter().filter_map(|child_id| dom.element(*child_id)));
    let descendant_markers = collect_descendant_markers(dom, exemplar_id);
    let direct_child_tag_set: BTreeSet<String> = direct_children
        .iter()
        .filter_map(|child_id| dom.element(*child_id).map(|element| element.tag.clone()))
        .collect();
    let nearby_tag_set = nearby_child_tag_set(dom, exemplar_id, sibling_groups);

    let likely_header_family = has_descendant_tag(dom, exemplar_id, "th");
    let has_td_children = direct_child_tag_set.contains("td");
    let has_field_markers = descendant_markers
        .iter()
        .any(|(marker, _)| matches!(marker.as_str(), "a[href]" | "img[src]" | "span"));
    let likely_data_family =
        !likely_header_family && count == highest_repeat_count && (has_td_children || has_field_markers);

    let mut distinguishing_markers = Vec::new();
    for tag in direct_child_tag_set.iter().take(3) {
        distinguishing_markers.push(format!("{tag} present"));
    }
    for (marker, marker_count) in &descendant_markers {
        distinguishing_markers.push(render_count_item(marker, *marker_count));
    }
    for excluded in nearby_tag_set.difference(&direct_child_tag_set) {
        if excluded == "th" || excluded == "td" {
            distinguishing_markers.push(format!("no {excluded}"));
        }
    }

    let slot_hints = infer_slot_hints(dom, exemplar_id);
    let distinct_slot_count = slot_hints.iter().collect::<BTreeSet<_>>().len();
    let recommended_extraction_mode = if likely_data_family && has_field_markers && distinct_slot_count >= 2 {
        "PROJECT"
    } else {
        "FLATTEN"
    };

    RowFamily {
        id: String::new(),
        exemplar_id,
        parent_tag,
        repeated_tag: exemplar.tag.clone(),
        count,
        table_family: false,
        likely_data_family,
        likely_header_family,
        distinguishing_markers,
        required_children: render_counter_items(&direct_child_labels),
        required_descendants: render_counter_items(&descendant_markers),
        slot_hints,
        recommended_extraction_mode,
        exemplar_shape: render_exemplar_shape(dom, exemplar_id, 2),
    }
}

fn analyze_families(dom: &DomTree, max_depth: usize) -> Vec<RowFamily> {
    let mut families = Vec::new();
    for root_id in skeleton_roots(dom) {
        collect_row_families(dom, root_id, max_depth, &mut families);
        collect_table_families(dom, root_id, &mut families);
    }
    assign_family_ids(&mut families);
    families
}

fn collect_table_families(dom: &DomTree, node_id: NodeId, families: &mut Vec<RowFamily>) {
    let Some(element) = dom.element(node_id) else {
        return;
    };

    match element.tag.as_str() {
        "table" => {
            let children = child_elements(dom, node_id);
            let has_sections = children.iter().any(|child_id| {
                dom.element(*child_id)
                    .map(|child| matches!(child.tag.as_str(), "tbody" | "thead" | "tfoot"))
                    .unwrap_or(false)
            });
            if !has_sections {
                add_table_section_families(dom, node_id, node_id, families);
            }
            for child_id in children {
                collect_table_families(dom, child_id, families);
            }
        }
        "tbody" | "thead" | "tfoot" => {
            let table_id = nearest_ancestor_tag(dom, node_id, "table").unwrap_or(node_id);
            add_table_section_families(dom, node_id, table_id, families);
            for child_id in child_elements(dom, node_id) {
                collect_table_families(dom, child_id, families);
            }
        }
        _ => {
            for child_id in child_elements(dom, node_id) {
                collect_table_families(dom, child_id, families);
            }
        }
    }
}

fn add_table_section_families(dom: &DomTree, section_id: NodeId, table_id: NodeId, families: &mut Vec<RowFamily>) {
    let rows: Vec<NodeId> = child_elements(dom, section_id)
        .into_iter()
        .filter(|child_id| {
            dom.element(*child_id)
                .map(|element| element.tag == "tr")
                .unwrap_or(false)
        })
        .collect();
    if rows.is_empty() {
        return;
    }

    let mut header_rows = Vec::new();
    let mut data_rows = Vec::new();

    for row_id in rows {
        let cells = child_elements(dom, row_id);
        let has_th = cells.iter().any(|cell_id| {
            dom.element(*cell_id)
                .map(|cell| cell.tag == "th")
                .unwrap_or(false)
        });
        let has_td = cells.iter().any(|cell_id| {
            dom.element(*cell_id)
                .map(|cell| cell.tag == "td")
                .unwrap_or(false)
        });

        if has_th {
            header_rows.push(row_id);
        } else if has_td {
            data_rows.push(row_id);
        }
    }

    if !header_rows.is_empty() {
        families.push(build_table_family(dom, table_id, &header_rows, false));
    }
    if data_rows.len() >= 3 {
        families.push(build_table_family(dom, table_id, &data_rows, true));
    }
}

fn build_table_family(dom: &DomTree, table_id: NodeId, row_ids: &[NodeId], is_data_family: bool) -> RowFamily {
    let exemplar_id = select_table_exemplar(dom, row_ids);
    let exemplar = dom.element(exemplar_id).expect("table exemplar must be element");
    let direct_children = child_elements(dom, exemplar_id);
    let direct_child_labels = count_labels(direct_children.iter().filter_map(|child_id| dom.element(*child_id)));
    let descendant_markers = collect_descendant_markers(dom, exemplar_id);
    let direct_child_tag_set: BTreeSet<String> = direct_children
        .iter()
        .filter_map(|child_id| dom.element(*child_id).map(|element| element.tag.clone()))
        .collect();

    let mut distinguishing_markers = Vec::new();
    for tag in direct_child_tag_set.iter().take(3) {
        distinguishing_markers.push(format!("{tag} present"));
    }
    for (marker, marker_count) in &descendant_markers {
        distinguishing_markers.push(render_count_item(marker, *marker_count));
    }
    if is_data_family {
        distinguishing_markers.push("no th".to_string());
    } else {
        distinguishing_markers.push("no td".to_string());
    }

    let slot_hints = infer_slot_hints(dom, exemplar_id);
    let distinct_slot_count = slot_hints.iter().collect::<BTreeSet<_>>().len();
    let has_field_markers = descendant_markers
        .iter()
        .any(|(marker, _)| matches!(marker.as_str(), "a[href]" | "img[src]" | "span"));
    let recommended_extraction_mode = if is_data_family && has_field_markers && distinct_slot_count >= 2 {
        "PROJECT"
    } else {
        "FLATTEN"
    };

    RowFamily {
        id: String::new(),
        exemplar_id,
        parent_tag: dom
            .element(table_id)
            .map(|element| element.tag.clone())
            .unwrap_or_else(|| "table".to_string()),
        repeated_tag: exemplar.tag.clone(),
        count: row_ids.len(),
        table_family: true,
        likely_data_family: is_data_family,
        likely_header_family: !is_data_family,
        distinguishing_markers,
        required_children: render_counter_items(&direct_child_labels),
        required_descendants: render_counter_items(&descendant_markers),
        slot_hints,
        recommended_extraction_mode,
        exemplar_shape: render_exemplar_shape(dom, exemplar_id, 2),
    }
}

fn select_table_exemplar(dom: &DomTree, row_ids: &[NodeId]) -> NodeId {
    let mut counts: BTreeMap<String, (usize, NodeId)> = BTreeMap::new();

    for row_id in row_ids {
        let signature = normalized_table_row_signature(dom, *row_id);
        let entry = counts.entry(signature).or_insert((0, *row_id));
        entry.0 += 1;
    }

    counts
        .into_iter()
        .max_by(|left, right| left.1 .0.cmp(&right.1 .0).then_with(|| right.1 .1.cmp(&left.1 .1)))
        .map(|(_, (_, exemplar_id))| exemplar_id)
        .unwrap_or(row_ids[0])
}

fn normalized_table_row_signature(dom: &DomTree, row_id: NodeId) -> String {
    child_elements(dom, row_id)
        .into_iter()
        .filter_map(|cell_id| {
            let cell = dom.element(cell_id)?;
            Some(match first_descendant_hint(dom, cell_id) {
                Some(hint) => format!("{}>{hint}", cell.tag),
                None => cell.tag.clone(),
            })
        })
        .collect::<Vec<_>>()
        .join("|")
}

fn nearest_ancestor_tag(dom: &DomTree, node_id: NodeId, tag: &str) -> Option<NodeId> {
    let mut cursor = dom.node(node_id).parent;
    while let Some(parent_id) = cursor {
        if dom
            .element(parent_id)
            .map(|element| element.tag == tag)
            .unwrap_or(false)
        {
            return Some(parent_id);
        }
        cursor = dom.node(parent_id).parent;
    }
    None
}

fn assign_family_ids(families: &mut [RowFamily]) {
    let mut next_data = 1;
    let mut next_header = 1;
    let mut next_generic = 1;

    for family in families {
        family.id = if family.likely_header_family {
            let id = format!("H{next_header}");
            next_header += 1;
            id
        } else if family.likely_data_family {
            let id = format!("D{next_data}");
            next_data += 1;
            id
        } else {
            let id = format!("F{next_generic}");
            next_generic += 1;
            id
        };
    }
}

fn rank_families(families: &[RowFamily]) -> Vec<&RowFamily> {
    let mut ranked: Vec<&RowFamily> = families.iter().collect();
    ranked.sort_by(|left, right| {
        family_rank_key(right)
            .cmp(&family_rank_key(left))
            .then_with(|| left.id.cmp(&right.id))
    });
    ranked
}

fn family_rank_key(family: &RowFamily) -> FamilyRankKey {
    FamilyRankKey {
        table_data_priority: usize::from(family.table_family && family.likely_data_family),
        data_priority: usize::from(family.likely_data_family),
        repeat_count: family.count,
        informative_descendant_count: family
            .required_descendants
            .iter()
            .filter(|item| item.contains("a[href]") || item.contains("img[src]") || item.contains("span"))
            .count(),
        table_priority: usize::from(family.table_family),
        non_header_priority: usize::from(!family.likely_header_family),
    }
}

fn count_labels<'a>(elements: impl Iterator<Item = &'a ElementData>) -> BTreeMap<String, usize> {
    let mut counts = BTreeMap::new();
    for element in elements {
        *counts.entry(format_label(element)).or_insert(0) += 1;
    }
    counts
}

fn collect_descendant_markers(dom: &DomTree, node_id: NodeId) -> BTreeMap<String, usize> {
    let mut counts = BTreeMap::new();
    for child_id in child_elements(dom, node_id) {
        collect_descendant_markers_inner(dom, child_id, 0, &mut counts);
    }
    counts
}

fn collect_descendant_markers_inner(
    dom: &DomTree,
    node_id: NodeId,
    depth: usize,
    counts: &mut BTreeMap<String, usize>,
) {
    let Some(element) = dom.element(node_id) else {
        return;
    };

    if let Some(marker) = summary_marker(element) {
        let count_direct_marker = depth > 0 || !matches!(marker.as_str(), "td" | "th" | "div");
        if count_direct_marker {
            *counts.entry(marker).or_insert(0) += 1;
        }
    }

    for child_id in child_elements(dom, node_id) {
        collect_descendant_markers_inner(dom, child_id, depth + 1, counts);
    }
}

fn summary_marker(element: &ElementData) -> Option<String> {
    if element.tag == "a" && element.attrs.contains_key("href") {
        return Some("a[href]".to_string());
    }
    if element.tag == "img" && element.attrs.contains_key("src") {
        return Some("img[src]".to_string());
    }
    if matches!(element.tag.as_str(), "span" | "br" | "th" | "td" | "div") {
        return Some(element.tag.clone());
    }
    None
}

fn nearby_child_tag_set(dom: &DomTree, exemplar_id: NodeId, sibling_groups: &[FoldedChild]) -> BTreeSet<String> {
    let mut tags = BTreeSet::new();

    for group in sibling_groups {
        let sibling_id = match *group {
            FoldedChild::Single(node_id) => node_id,
            FoldedChild::Run { exemplar, .. } => exemplar,
        };
        if sibling_id == exemplar_id {
            continue;
        }

        for child_id in child_elements(dom, sibling_id) {
            if let Some(element) = dom.element(child_id) {
                tags.insert(element.tag.clone());
            }
        }
    }

    tags
}

fn has_descendant_tag(dom: &DomTree, node_id: NodeId, target_tag: &str) -> bool {
    child_elements(dom, node_id).into_iter().any(|child_id| {
        dom.element(child_id)
            .map(|element| element.tag == target_tag)
            .unwrap_or(false)
            || has_descendant_tag(dom, child_id, target_tag)
    })
}

fn infer_slot_hints(dom: &DomTree, node_id: NodeId) -> Vec<String> {
    child_elements(dom, node_id)
        .into_iter()
        .filter_map(|child_id| {
            let child = dom.element(child_id)?;
            let child_label = format_label(child);
            let descendant_hint = first_descendant_hint(dom, child_id);
            Some(match descendant_hint {
                Some(hint) => format!("{child_label} > {hint}"),
                None => child_label,
            })
        })
        .collect()
}

fn first_descendant_hint(dom: &DomTree, node_id: NodeId) -> Option<String> {
    for child_id in child_elements(dom, node_id) {
        let child = dom.element(child_id)?;
        if let Some(marker) = summary_marker(child) {
            return Some(marker);
        }
        if let Some(nested) = first_descendant_hint(dom, child_id) {
            return Some(nested);
        }
        return Some(child.tag.clone());
    }
    None
}

fn render_exemplar_shape(dom: &DomTree, node_id: NodeId, depth_left: usize) -> Vec<String> {
    let mut lines = Vec::new();
    render_exemplar_shape_inner(dom, node_id, "", true, 0, depth_left, &mut lines);
    lines
}

fn render_exemplar_shape_inner(
    dom: &DomTree,
    node_id: NodeId,
    prefix: &str,
    is_last: bool,
    depth: usize,
    depth_left: usize,
    lines: &mut Vec<String>,
) {
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
    lines.push(format!("{branch}{}", format_label(element)));

    let children = child_elements(dom, node_id);
    if children.is_empty() || depth_left == 0 {
        if !children.is_empty() {
            let child_prefix = if depth == 0 {
                String::new()
            } else if is_last {
                format!("{prefix}    ")
            } else {
                format!("{prefix}│   ")
            };
            lines.push(format!("{child_prefix}└── ..."));
        }
        return;
    }

    let child_prefix = if depth == 0 {
        String::new()
    } else if is_last {
        format!("{prefix}    ")
    } else {
        format!("{prefix}│   ")
    };
    for (index, child_id) in children.iter().enumerate() {
        render_exemplar_shape_inner(
            dom,
            *child_id,
            &child_prefix,
            index + 1 == children.len(),
            depth + 1,
            depth_left.saturating_sub(1),
            lines,
        );
    }
}

fn render_counter_items(counter: &BTreeMap<String, usize>) -> Vec<String> {
    counter
        .iter()
        .map(|(label, count)| render_count_item(label, *count))
        .collect()
}

fn render_count_item(label: &str, count: usize) -> String {
    if count == 1 {
        label.to_string()
    } else {
        format!("{label} × {count}")
    }
}

fn render_items(items: &[String]) -> String {
    if items.is_empty() {
        "(none)".to_string()
    } else {
        items.join(", ")
    }
}

fn yes_no(value: bool) -> &'static str {
    if value {
        "yes"
    } else {
        "no"
    }
}

fn compact_kind_code(family: &RowFamily) -> char {
    if family.likely_data_family {
        'D'
    } else if family.likely_header_family {
        'H'
    } else {
        'U'
    }
}

fn compact_mode_code(family: &RowFamily) -> char {
    match family.recommended_extraction_mode {
        "PROJECT" => 'P',
        _ => 'F',
    }
}

fn strongest_slot_hint(family: &RowFamily) -> String {
    family
        .slot_hints
        .iter()
        .max_by(|left, right| slot_hint_rank(left).cmp(&slot_hint_rank(right)).then_with(|| right.cmp(left)))
        .map(|slot| compact_slot_hint(slot))
        .unwrap_or_else(|| family.repeated_tag.clone())
}

fn slot_hint_rank(slot: &str) -> (usize, usize, usize) {
    (
        usize::from(slot.contains("a [href]") || slot.contains("img [src]") || slot.contains("span")),
        slot.matches('>').count(),
        slot.len(),
    )
}

fn compact_slot_hint(slot: &str) -> String {
    slot.replace(" > ", ">")
        .replace(" [href]", "[href]")
        .replace(" [src]", "[src]")
}

fn shallow_signature(dom: &DomTree, node_id: NodeId, depth: usize) -> String {
    let children = child_elements(dom, node_id);
    let Some(first_child) = children.first().copied() else {
        return dom
            .element(node_id)
            .map(compact_element_label)
            .unwrap_or_else(|| "node".to_string());
    };

    shallow_signature_from_child(dom, first_child, depth)
}

fn shallow_signature_from_child(dom: &DomTree, node_id: NodeId, depth: usize) -> String {
    let Some(element) = dom.element(node_id) else {
        return "node".to_string();
    };
    let here = compact_element_label(element);
    if depth == 0 {
        return here;
    }

    let children = child_elements(dom, node_id);
    let Some(first_child) = children.first().copied() else {
        return here;
    };
    format!(
        "{}>{}",
        here,
        shallow_signature_from_child(dom, first_child, depth.saturating_sub(1))
    )
}

fn compact_element_label(element: &ElementData) -> String {
    if element.tag == "a" && element.attrs.contains_key("href") {
        return "a[href]".to_string();
    }
    if element.tag == "img" && element.attrs.contains_key("src") {
        return "img[src]".to_string();
    }
    element.tag.clone()
}

#[cfg(test)]
mod tests {
    use super::{render_family_report, render_family_report_compact};
    use crate::dom::DomTree;

    #[test]
    fn detects_repeated_table_rows_as_data_family() {
        let dom = DomTree::parse(
            r#"
            <table>
              <tbody>
                <tr><th>Year</th><th>PDF</th></tr>
                <tr><td>2025</td><td><a href="/a.pdf">PDF</a></td></tr>
                <tr><td>2024</td><td><a href="/b.pdf">PDF</a></td></tr>
                <tr><td>2023</td><td><a href="/c.pdf">PDF</a></td></tr>
              </tbody>
            </table>
            "#,
        )
        .expect("valid html");

        let output = render_family_report(&dom, 10).join("\n");

        assert!(output.contains("Detected Row Families"));
        assert!(output.contains("Family D1"));
        assert!(output.contains("parent_tag: table"));
        assert!(output.contains("repeated_tag: tr"));
        assert!(output.contains("count: 3"));
        assert!(output.contains("likely_data_family: yes"));
        assert!(output.contains("likely_header_family: no"));
        assert!(output.contains("required_children: td × 2"));
        assert!(output.contains("required_descendants: a[href]"));
        assert!(output.contains("recommended_extraction_mode: PROJECT"));
    }

    #[test]
    fn renders_compact_family_lines() {
        let dom = DomTree::parse(
            r#"
            <ul>
              <li><div><a href="/a"><img src="/a.png" /></a></div></li>
              <li><div><a href="/b"><img src="/b.png" /></a></div></li>
              <li><div><a href="/c"><img src="/c.png" /></a></div></li>
            </ul>
            "#,
        )
        .expect("valid html");

        let output = render_family_report_compact(&dom, 10).join("\n");

        assert!(output.contains("FAM"));
        assert!(output.contains("KIND: D=data, H=header, U=unknown"));
        assert!(output.contains("MODE: F=FLATTEN, P=PROJECT"));
        assert!(output.contains("D1|ul>li|3|D|F|slot:div>a[href]|sig:div>a[href]"));
    }
}
