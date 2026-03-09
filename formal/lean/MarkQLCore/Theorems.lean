import MarkQLCore.Parser
import MarkQLCore.Examples

namespace MarkQLCore

theorem parse_accept_select_div_from_doc :
    parseQuery acceptSelectDivFromDoc =
      .ok (Query.core (.ident "div")) := by
  native_decide

theorem parse_accept_select_star_from_doc :
    parseQuery acceptSelectStarFromDoc =
      .ok (Query.core .star) := by
  native_decide

theorem parse_accept_select_div_from_doc_as_n :
    parseQuery acceptSelectDivFromDocAsN =
      .ok (Query.core (.ident "div") (some "n")) := by
  native_decide

theorem parse_accept_select_title_from_doc :
    parseQuery acceptSelectTitleFromDoc =
      .ok (Query.core (.ident "title")) := by
  native_decide

theorem parse_accept_select_star_from_doc_as_node :
    parseQuery acceptSelectStarFromDocAsNode =
      .ok (Query.core .star (some "node_row")) := by
  native_decide

theorem parse_reject_select_from_doc :
    parseQuery rejectSelectFromDoc =
      .error .expectedProjection := by
  native_decide

theorem parse_reject_select_div_doc :
    parseQuery rejectSelectDivDoc =
      .error .expectedFrom := by
  native_decide

theorem parse_reject_from_doc_select_div :
    parseQuery rejectFromDocSelectDiv =
      .error .expectedSelect := by
  native_decide

theorem parse_reject_select_div_from :
    parseQuery rejectSelectDivFrom =
      .error .expectedDocSource := by
  native_decide

theorem parse_reject_select_div_as_n :
    parseQuery rejectSelectDivAsN =
      .error .expectedFrom := by
  native_decide

theorem parse_reject_select_div_from_doc_as_self :
    parseQuery rejectSelectDivFromDocAsSelf =
      .error .reservedAlias := by
  native_decide

theorem accepted_example_count :
    acceptedExamples.length = 5 := rfl

theorem rejected_example_count :
    rejectedExamples.length = 6 := rfl

theorem accepted_examples_are_accepted :
    acceptedExamples.map isAccepted = [true, true, true, true, true] := by
  native_decide

theorem rejected_examples_are_rejected :
    rejectedExamples.map isRejected = [true, true, true, true, true, true] := by
  native_decide

end MarkQLCore
