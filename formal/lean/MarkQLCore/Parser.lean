import MarkQLCore.Lexer

namespace MarkQLCore

private def parseProjection : Token -> Except ParseError Projection
  | Token.star => .ok Projection.star
  | Token.ident name => .ok (Projection.ident name)
  | Token.invalid raw => .error (ParseError.invalidToken raw)
  | _ => .error ParseError.expectedProjection

def parseTokens : List Token -> ParseResult
  | Token.kwSelect :: rest =>
      match rest with
      | [] => .error ParseError.expectedProjection
      | projectionTok :: afterProjection =>
          match parseProjection projectionTok with
          | .error err => .error err
          | .ok projection =>
              match afterProjection with
              | [] => .error ParseError.expectedFrom
              | Token.kwFrom :: [] => .error ParseError.expectedDocSource
              | Token.kwFrom :: Token.kwDoc :: [] =>
                  .ok (Query.core projection)
              | Token.kwFrom :: Token.kwDoc :: Token.kwAs :: [] =>
                  .error ParseError.expectedAlias
              | Token.kwFrom :: Token.kwDoc :: Token.kwAs :: Token.ident alias :: [] =>
                  if alias == "self" then
                    .error ParseError.reservedAlias
                  else
                    .ok (Query.core projection (some alias))
              | Token.kwFrom :: Token.kwDoc :: Token.kwAs :: Token.invalid raw :: _ =>
                  .error (ParseError.invalidToken raw)
              | Token.kwFrom :: Token.kwDoc :: Token.kwAs :: _ =>
                  .error ParseError.expectedAlias
              | Token.kwFrom :: Token.invalid raw :: _ =>
                  .error (ParseError.invalidToken raw)
              | Token.kwFrom :: _ =>
                  .error ParseError.expectedDocSource
              | _ =>
                  .error ParseError.expectedFrom
  | Token.invalid raw :: _ => .error (ParseError.invalidToken raw)
  | _ => .error ParseError.expectedSelect

def parseQuery (input : String) : ParseResult :=
  parseTokens (lex input)

def isAccepted (input : String) : Bool :=
  match parseQuery input with
  | .ok _ => true
  | .error _ => false

def isRejected (input : String) : Bool :=
  !isAccepted input

end MarkQLCore
