import MarkQLCore.Syntax

namespace MarkQLCore

inductive Token where
  | kwSelect
  | kwFrom
  | kwAs
  | kwDoc
  | star
  | ident (name : String)
  | invalid (raw : String)
  deriving Repr, DecidableEq

private def isIdentStart (c : Char) : Bool :=
  c.isAlpha || c == '_'

private def isIdentChar (c : Char) : Bool :=
  c.isAlphanum || c == '_'

def isIdentifier (word : String) : Bool :=
  match word.data with
  | [] => false
  | c :: cs => isIdentStart c && cs.all isIdentChar

private def splitWords (input : String) : List String :=
  (input.split (fun c => c.isWhitespace)).filter (fun word => !word.isEmpty)

def lexWord (word : String) : Token :=
  if word == "SELECT" then
    Token.kwSelect
  else if word == "FROM" then
    Token.kwFrom
  else if word == "AS" then
    Token.kwAs
  else if word == "doc" then
    Token.kwDoc
  else if word == "*" then
    Token.star
  else if isIdentifier word then
    Token.ident word
  else
    Token.invalid word

def lex (input : String) : List Token :=
  (splitWords input).map lexWord

end MarkQLCore
