namespace MarkQLCore

inductive Projection where
  | star
  | ident (name : String)
  deriving Repr, DecidableEq

inductive Source where
  | doc
  deriving Repr, DecidableEq

structure Query where
  projection : Projection
  source : Source
  alias : Option String
  deriving Repr, DecidableEq

inductive ParseError where
  | expectedSelect
  | expectedProjection
  | expectedFrom
  | expectedDocSource
  | expectedAlias
  | reservedAlias
  | unexpectedTrailing
  | invalidToken (raw : String)
  deriving Repr, DecidableEq

abbrev ParseResult := Except ParseError Query

instance : DecidableEq (Except ParseError Query)
  | .ok q1, .ok q2 =>
      match decEq q1 q2 with
      | isTrue h => isTrue (by cases h; rfl)
      | isFalse h => isFalse (by
          intro hEq
          cases hEq
          exact h rfl)
  | .error e1, .error e2 =>
      match decEq e1 e2 with
      | isTrue h => isTrue (by cases h; rfl)
      | isFalse h => isFalse (by
          intro hEq
          cases hEq
          exact h rfl)
  | .ok _, .error _ => isFalse (by intro hEq; cases hEq)
  | .error _, .ok _ => isFalse (by intro hEq; cases hEq)

instance : DecidableEq ParseResult := inferInstance

def Query.core (projection : Projection) (alias : Option String := none) : Query :=
  { projection := projection, source := Source.doc, alias := alias }

end MarkQLCore
