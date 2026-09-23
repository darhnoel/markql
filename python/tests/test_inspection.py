import json

import markql
import pytest


CARDS = '<ul class="catalog">' + ''.join(
    f'<li class="card"><h3>{name}</h3><p class="price">{price}</p></li>'
    for name, price in [('Alpha', '$10'), ('Beta', '$20'), ('Gamma', '$30')]
) + '</ul>'


def test_inspection_discovers_records_and_values_using_execution_semantics():
    document = markql.load(CARDS)
    evidence = markql.inspect(document)
    payload = evidence.to_dict()
    assert payload['schema_version'] == 1
    assert payload['schema'] == 'markql.structural-evidence'
    family, = payload['record_candidates']
    assert family['count'] == 3
    assert family['recipe']['tag'] == 'li'
    title = next(f for f in family['field_candidates'] if f['recipe']['tag'] == 'h3')
    rows = markql.execute(
        "SELECT PROJECT(li) AS (name: TEXT(h3)) FROM doc "
        "WHERE attributes.class = 'card'", doc=document,
    ).rows
    assert title['samples'] == [row['name'] for row in rows]
    assert json.loads(evidence.to_json()) == payload


def test_structured_anchors_expose_row_attributes_and_common_classes():
    html = '<div class="tiles">' + ''.join(
        f'<a id="{name}" class="tile {size}" href="/{name}" style="width:10%">'
        f'<span class="symbol">{name}</span><span class="change">{change}</span></a>'
        for name, size, change in [('A', 'large', '+1%'), ('B', 'small', '-2%'), ('C', 'small', '0%')]
    ) + '</div>'
    family, = markql.inspect(html).to_dict()['record_candidates']
    assert family['count'] == 3
    assert family['recipe']['classes'] == ['tile']
    link = next(f for f in family['field_candidates'] if f['attribute'] == 'href')
    assert link['samples'] == ['/A', '/B', '/C']
    assert link['recipe']['classes'] == ['tile']
    assert any(f['attribute'] == 'style' for f in family['field_candidates'])


def test_table_columns_have_distinct_relative_paths_and_headers_are_separate():
    html = '<table><tbody><tr><th>Name</th><th>Value</th></tr>' + ''.join(
        f'<tr><td>{name}</td><td>{value}</td></tr>'
        for name, value in [('Alpha', '10'), ('Beta', '20'), ('Gamma', '30')]
    ) + '</tbody></table>'
    family, = markql.inspect(html).to_dict()['record_candidates']
    assert family['count'] == 3
    cells = [f for f in family['field_candidates'] if f['recipe']['tag'] == 'td']
    assert len(cells) == 2
    assert [f['path'] for f in cells] == [[1], [2]]
    assert [f['samples'] for f in cells] == [['Alpha', 'Beta', 'Gamma'], ['10', '20', '30']]


def test_evidence_is_bounded_and_reports_omitted_content():
    html = '<ul>' + ('<li><p>' + '猫' * 1000 + '</p></li>') * 3 + '</ul>'
    payload = markql.inspect(html).to_dict()
    assert payload['truncated'] is True
    assert all(len(sample.encode('utf-8')) <= 256
               for record in payload['record_candidates']
               for field in record['field_candidates'] for sample in field['samples'])
    assert markql.inspect(CARDS).to_dict()['truncated'] is False


def test_local_inputs_are_equivalent_detached_and_do_not_replace_loaded_document(tmp_path):
    original = markql.load('<p>Keep me</p>')
    path = tmp_path / 'cards.html'
    path.write_text(CARDS, encoding='utf-8')
    evidence = markql.inspect(path)
    expected = evidence.to_dict()
    assert markql.inspect(CARDS.encode()).to_dict() == expected
    assert markql.inspect(CARDS).to_dict() == expected
    evidence.to_dict()['record_candidates'].clear()
    assert evidence.to_dict() == expected
    assert markql.execute("SELECT text FROM doc WHERE tag = 'p'").rows == markql.execute(
        "SELECT text FROM doc WHERE tag = 'p'", doc=original,
    ).rows
    with pytest.raises(ValueError, match='Network access is disabled'):
        markql.inspect('https://example.com')
    with pytest.raises(ValueError, match='maximum allowed size'):
        markql.inspect(CARDS, max_bytes=10)
    with pytest.raises(ValueError, match='max_bytes must be a positive integer'):
        markql.inspect(CARDS, max_bytes=-1)
    assert markql.inspect(b'').to_dict()['record_candidates'] == []


def test_helper_uses_native_evidence_and_preserves_row_scope(tmp_path):
    from markql.helper.tool_adapters import LocalToolAdapters
    from markql.helper.model_adapter import HeuristicModelAdapter

    path = tmp_path / 'cards.html'
    path.write_text(CARDS, encoding='utf-8')
    adapters = LocalToolAdapters()
    compact = adapters.inspect_compact_families(str(path))
    assert compact['source'] == 'markql.inspect'
    assert json.loads(compact['content'])['record_candidates'][0]['recipe']['tag'] == 'li'
    full = adapters.inspect_families(str(path))
    assert json.loads(full['content']) == markql.inspect(path).to_dict()
    decision = HeuristicModelAdapter().interpret_and_suggest({
        'artifact': compact, 'constraints': [], 'goal_text': 'extract products',
    })
    assert decision['chosen_family'] == compact['family_hint']
    rows = markql.execute(decision['query'], doc=markql.load(path)).rows
    assert len(rows) == 3
    assert all(row['tag'] == 'li' for row in rows)
    skeleton = adapters.inspect_skeleton(str(path))
    assert skeleton['source'] == 'markql'
    assert 'li' in skeleton['content']


def test_field_recipes_do_not_invent_a_shared_parent():
    html = '<ul>' + ''.join(
        f'<li><div><{tag} class="wrapper"><em>{tag}</em></{tag}></div></li>'
        for tag in ['span', 'p', 'span']
    ) + '</ul>'
    family, = markql.inspect(html).to_dict()['record_candidates']
    field, = [f for f in family['field_candidates'] if f['recipe']['tag'] == 'em']
    assert field['recipe']['parent_tag'] is None
    assert field['recipe']['parent_classes'] == []


def test_helper_requests_structure_when_native_evidence_has_no_records(tmp_path):
    from markql.helper.tool_adapters import LocalToolAdapters
    from markql.helper.model_adapter import HeuristicModelAdapter

    path = tmp_path / 'single.html'
    path.write_text('<p>One item</p>', encoding='utf-8')
    artifact = LocalToolAdapters().inspect_families(str(path))
    decision = HeuristicModelAdapter().interpret_and_suggest({
        'artifact': artifact, 'constraints': [], 'goal_text': 'extract item',
    })
    assert decision['status'] == 'need_more_artifact'
    assert decision['requested_artifact'] == 'skeleton'
    assert decision['query'] == ''
