//===--- Parser.cpp - Swift Language Parser -------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
//  This file implements the Swift parser.
//
//===----------------------------------------------------------------------===//

#include "swift/Subsystems.h"
#include "swift/AST/Diagnostics.h"
#include "swift/AST/PrettyStackTrace.h"
#include "Parser.h"
#include "swift/Parse/Lexer.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/Twine.h"
using namespace swift;

namespace {
  /// To assist debugging parser crashes, tell us the location of the
  /// current token.
  class PrettyStackTraceParser : public llvm::PrettyStackTraceEntry {
    Parser &P;
  public:
    PrettyStackTraceParser(Parser &P) : P(P) {}
    void print(llvm::raw_ostream &out) const {
      out << "With parser at source location: ";
      printSourceLoc(out, P.Tok.getLoc(), P.Context);
      out << '\n';
    }
  };
}
  
/// parseTranslationUnit - Entrypoint for the parser.
bool swift::parseIntoTranslationUnit(TranslationUnit *TU,
                                     unsigned BufferID,
                                     unsigned *BufferOffset,
                                     unsigned BufferEndOffset) {
  Parser P(BufferID, TU->getComponent(), TU->Ctx,
           BufferOffset ? *BufferOffset : 0, BufferEndOffset,
           TU->Kind == TranslationUnit::Main ||
           TU->Kind == TranslationUnit::Repl);
  PrettyStackTraceParser stackTrace(P);
  P.parseTranslationUnit(TU);
  if (BufferOffset)
    *BufferOffset = P.Tok.getLoc().Value.getPointer() -
                    P.Buffer->getBuffer().begin();

  return P.FoundSideEffects;
}
  
//===----------------------------------------------------------------------===//
// Setup and Helper Methods
//===----------------------------------------------------------------------===//

/// ComputeLexStart - Compute the start of lexing; if there's an offset, take
/// that into account.  If there's a #! line in a main module, ignore it.
static StringRef ComputeLexStart(StringRef File, unsigned Offset,
                                 unsigned EndOffset, bool IsMainModule) {
  if (EndOffset)
    return File.slice(Offset, EndOffset);
  else if (Offset)
    return File.substr(Offset);

  if (IsMainModule && File.startswith("#!")) {
    StringRef::size_type Pos = File.find_first_of("\n\r");
    if (Pos != StringRef::npos)
      return File.substr(Pos);
  }

  return File;
}


Parser::Parser(unsigned BufferID, swift::Component *Comp, ASTContext &Context,
               unsigned Offset, unsigned EndOffset, bool IsMainModule)
  : SourceMgr(Context.SourceMgr),
    Diags(Context.Diags),
    Buffer(SourceMgr.getMemoryBuffer(BufferID)),
    L(new Lexer(ComputeLexStart(Buffer->getBuffer(), Offset, EndOffset,
                                 IsMainModule),
                 SourceMgr, &Diags)),
    Component(Comp),
    Context(Context),
    ScopeInfo(*this),
    IsMainModule(IsMainModule),
    FoundSideEffects(false) {
}

Parser::~Parser() {
  delete L;
}

/// peekToken - Return the next token that will be installed by consumeToken.
const Token &Parser::peekToken() {
  return L->peekNextToken();
}

SourceLoc Parser::consumeToken() {
  SourceLoc Loc = Tok.getLoc();
  assert(Tok.isNot(tok::eof) && "Lexing past eof!");
  L->lex(Tok);
  return Loc;
}

SourceLoc Parser::consumeStartingLess() {
  assert(startsWithLess(Tok) && "Token does not start with '<'");
  
  if (Tok.getLength() == 1)
    return consumeToken();
  
  // Skip the starting '<' in the existing token.
  SourceLoc Loc = Tok.getLoc();  
  StringRef Remaining =Tok.getText().substr(1);
  Tok.setToken(L->getTokenKind(Remaining), Remaining);
  return Loc;
}

SourceLoc Parser::consumeStartingGreater() {
  assert(startsWithGreater(Tok) && "Token does not start with '>'");
  
  if (Tok.getLength() == 1)
    return consumeToken();
  
  // Skip the starting '>' in the existing token.
  SourceLoc Loc = Tok.getLoc();
  StringRef Remaining =Tok.getText().substr(1);
  Tok.setToken(L->getTokenKind(Remaining), Remaining);
  return Loc;
}

/// skipUntil - Read tokens until we get to the specified token, then return.
/// Because we cannot guarantee that the token will ever occur, this skips to
/// some likely good stopping point.
///
void Parser::skipUntil(tok T1, tok T2) {
  // tok::unknown is a sentinel that means "don't skip".
  if (T1 == tok::unknown && T2 == tok::unknown) return;
  
  while (Tok.isNot(tok::eof) && Tok.isNot(T1) && Tok.isNot(T2)) {
    switch (Tok.getKind()) {
    default: consumeToken(); break;
    // TODO: Handle paren/brace/bracket recovery.
    }
  }
}

void Parser::skipUntilAnyOperator() {
  while (Tok.isNot(tok::eof) && Tok.isNotAnyOperator()) {
    switch (Tok.getKind()) {
    default: consumeToken(); break;
    // TODO: Handle paren/brace/bracket recovery.
    }
  }
}

/// skipUntilDeclRBrace - Skip to the next decl or '}'.
void Parser::skipUntilDeclRBrace() {
  while (1) {
    // Found the start of a decl, we're done.
    if (Tok.is(tok::eof) ||
        isStartOfDecl(Tok, peekToken()))
      return;
    
    // FIXME: Should match nested paren/brace/bracket's.
    
    // Otherwise, if it is a random token that we don't know about, keep
    // eating.
    consumeToken();
  }
}

/// skipUntilDeclStmtRBrace - Skip to the next decl, statement or '}'.
void Parser::skipUntilDeclStmtRBrace() {
  while (1) {
    // Found the start of a statement or decl, we're done.
    if (Tok.is(tok::eof) ||
        isStartOfStmtOtherThanAssignment(Tok) ||
        isStartOfDecl(Tok, peekToken()))
      return;
    
    // FIXME: Should match nested paren/brace/bracket's.

    // Otherwise, if it is a random token that we don't know about, keep
    // eating.
    consumeToken();
  }
}


//===----------------------------------------------------------------------===//
// Primitive Parsing
//===----------------------------------------------------------------------===//

/// parseIdentifier - Consume an identifier (but not an operator) if
/// present and return its name in Result.  Otherwise, emit an error and
/// return true.
bool Parser::parseIdentifier(Identifier &Result, const Diagnostic &D) {
  if (Tok.is(tok::identifier)) {
    Result = Context.getIdentifier(Tok.getText());
    consumeToken(tok::identifier);
    return false;
  }
  
  Diags.diagnose(Tok.getLoc(), D);
  return true;
}

/// parseAnyIdentifier - Consume an identifier or operator if present and return
/// its name in Result.  Otherwise, emit an error and return true.
bool Parser::parseAnyIdentifier(Identifier &Result, const Diagnostic &D) {
  if (Tok.is(tok::identifier) || Tok.isAnyOperator()) {
    Result = Context.getIdentifier(Tok.getText());
    consumeToken();
    return false;
  }
  
  Diags.diagnose(Tok.getLoc(), D);
  return true;
}

/// parseToken - The parser expects that 'K' is next in the input.  If so, it is
/// consumed and false is returned.
///
/// If the input is malformed, this emits the specified error diagnostic.
/// Next, if SkipToTok is specified, it calls skipUntil(SkipToTok).  Finally,
/// true is returned.
bool Parser::parseToken(tok K, SourceLoc &TokLoc, Diag<> ID, tok SkipToTok) {
  if (Tok.is(K)) {
    TokLoc = consumeToken(K);
    return false;
  }
  
  diagnose(Tok.getLoc(), ID);
  skipUntil(SkipToTok);
  
  // If we skipped ahead to the missing token and found it, consume it as if
  // there were no error.
  if (K == SkipToTok && Tok.is(SkipToTok))
    consumeToken();
  return true;
}

/// parseMatchingToken - Parse the specified expected token and return its
/// location on success.  On failure, emit the specified error diagnostic, and a
/// note at the specified note location.
bool Parser::parseMatchingToken(tok K, SourceLoc &TokLoc, Diag<> ErrorDiag,
                                SourceLoc OtherLoc, Diag<> OtherNote,
                                tok SkipToTok) {
  if (parseToken(K, TokLoc, ErrorDiag, SkipToTok)) {
    diagnose(OtherLoc, OtherNote);
    return true;
  }

  return false;
}



/// value-specifier:
///   ':' type-annotation
///   ':' type-annotation '=' expr
///   '=' expr
bool Parser::parseValueSpecifier(TypeLoc &Ty,
                                 NullablePtr<Expr> &Init) {
  // Diagnose when we don't have a type or an expression.
  if (Tok.isNot(tok::colon) && Tok.isNot(tok::equal)) {
    diagnose(Tok, diag::expected_type_or_init);
    // TODO: Recover better by still creating var, but making it have
    // 'invalid' type so that uses of the identifier are not errors.
    return true;
  }
  
  // Parse the type if present.
  if (consumeIf(tok::colon) &&
      parseTypeAnnotation(Ty, diag::expected_type))
    return true;
  
  // Parse the initializer, if present.
  if (consumeIf(tok::equal)) {
    NullablePtr<Expr> Tmp = parseExpr(diag::expected_initializer_expr);
    if (Tmp.isNull())
      return true;
    
    Init = Tmp.get();
  }
  
  return false;
}

/// diagnoseRedefinition - Diagnose a redefinition error, with a note
/// referring back to the original definition.

void Parser::diagnoseRedefinition(ValueDecl *Prev, ValueDecl *New) {
  assert(New != Prev && "Cannot conflict with self");
  diagnose(New->getLoc(), diag::decl_redefinition, New->isDefinition());
  diagnose(Prev->getLoc(), diag::previous_decldef, Prev->isDefinition(),
             Prev->getName());
}
