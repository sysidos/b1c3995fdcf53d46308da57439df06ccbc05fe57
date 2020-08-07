//===--- ParseType.cpp - Swift Language Parser for Types ------------------===//
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
// Type Parsing and AST Building
//
//===----------------------------------------------------------------------===//

#include "Parser.h"
#include "swift/AST/Attr.h"
#include "swift/AST/ExprHandle.h"
#include "swift/AST/TypeLoc.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
using namespace swift;

bool Parser::parseTypeAnnotation(TypeLoc &result) {
  return parseTypeAnnotation(result, diag::expected_type);
}

/// parseTypeAnnotation
///   type-annotation:
///     attribute-list type
bool Parser::parseTypeAnnotation(TypeLoc &result, Diag<> message) {
  // Parse attributes.
  DeclAttributes attrs;
  parseAttributeList(attrs);

  // Parse the type.
  if (parseType(result, message))
    return true;

  // Apply those attributes that do apply.
  if (attrs.empty()) return false;

  if (attrs.isByref()) {
    LValueType::Qual quals;
    if (!attrs.isByrefHeap()) quals |= LValueType::Qual::NonHeap;
    Type resultType = LValueType::get(result.getType(), quals, Context);
    SourceRange resultRange = { attrs.LSquareLoc,
                                result.getSourceRange().End };
    result = { resultType, resultRange };
    attrs.Byref = false; // so that the empty() check below works
  }

  // Handle the auto_closure attribute.
  if (attrs.isAutoClosure()) {
    FunctionType *FT = dyn_cast<FunctionType>(result.getType().getPointer());
    TupleType *InputTy = 0;
    if (FT) InputTy = dyn_cast<TupleType>(FT->getInput().getPointer());
    if (FT == 0) {
      // Autoclosure's require a syntactic function type.
      diagnose(attrs.LSquareLoc, diag::autoclosure_requires_function_type);
    } else if (InputTy == 0 || !InputTy->getFields().empty()) {
      // Function must take () syntactically.
      diagnose(attrs.LSquareLoc, diag::autoclosure_function_input_nonunit,
               FT->getInput());
    } else {
      // Otherwise, we're ok, rebuild type, adding the AutoClosure bit.
      Type resultType = FunctionType::get(FT->getInput(), FT->getResult(),
                                          true, Context);
      SourceRange resultRange = { attrs.LSquareLoc,
                                  result.getSourceRange().End };
      result = { resultType, resultRange };
    }
    attrs.AutoClosure = false;
  }

  // FIXME: this is lame.
  if (!attrs.empty())
    diagnose(attrs.LSquareLoc, diag::attribute_does_not_apply_to_type);

  return false;
}

bool Parser::parseType(TypeLoc &Result) {
  return parseType(Result, diag::expected_type);
}

/// parseType
///   type:
///     type-simple
///     type-function
///     type-array
///
///   type-function:
///     type-tuple '->' type 
///
///   type-simple:
///     type-identifier
///     type-tuple
///     type-composition
///
bool Parser::parseType(TypeLoc &Result, Diag<> MessageID) {
  // Parse type-simple first.
  SourceLoc StartLoc = Tok.getLoc();
  bool isTupleType = false;
  switch (Tok.getKind()) {
  case tok::identifier:
    if (parseTypeIdentifier(Result))
      return true;
    break;
  case tok::kw_protocol:
    if (parseTypeComposition(Result))
      return true;
    break;
  case tok::l_paren:
  case tok::l_paren_space: {
    isTupleType = true;
    SourceLoc LPLoc = consumeToken(), RPLoc;
    if (parseTypeTupleBody(LPLoc, Result) ||
        parseMatchingToken(tok::r_paren, RPLoc,
                           diag::expected_rparen_tuple_type_list,
                           LPLoc, diag::opening_paren))
      return true;
    break;
  }
  default:
    diagnose(Tok.getLoc(), MessageID);
    return true;
  }

  // '.metatype' still leaves us with type-simple.
  while (Tok.is(tok::period) && peekToken().is(tok::kw_metatype)) {
    consumeToken(tok::period);
    SourceLoc metatypeLoc = consumeToken(tok::kw_metatype);

    Type metatypeType = MetaTypeType::get(Result.getType(), Context);

    SourceRange metatypeTypeRange{ Result.getSourceRange().Start,
                                   metatypeLoc };
    Result = { metatypeType, metatypeTypeRange };
  }

  // Handle type-function if we have an arrow.
  if (consumeIf(tok::arrow)) {
    // If the argument was not syntactically a tuple type, report an error.
    if (!isTupleType) {
      diagnose(StartLoc, diag::expected_function_argument_must_be_paren);
    }
    
    TypeLoc SecondHalf;
    if (parseType(SecondHalf,
                  diag::expected_type_function_result))
      return true;
    Type FnType = FunctionType::get(Result.getType(), SecondHalf.getType(),
                                    Context);
    SourceRange FnTypeRange{ Result.getSourceRange().Start,
                             SecondHalf.getSourceRange().End };
    Result = { FnType, FnTypeRange };
    return false;
  }

  // If there is a square bracket without a space, we have an array.
  if (Tok.is(tok::l_square))
    return parseTypeArray(Result);
  
  return false;
}

bool Parser::parseGenericArguments(ArrayRef<TypeLoc> &Args) {
  // Parse the opening '<'.
  assert(startsWithLess(Tok) && "Generic parameter list must start with '<'");
  SourceLoc LAngleLoc = consumeStartingLess();

  SmallVector<TypeLoc, 4> GenericArgs;
  
  while (true) {
    TypeLoc Result;
    if (parseType(Result, diag::expected_type)) {
      // Skip until we hit the '>'.
      skipUntilAnyOperator();
      if (startsWithGreater(Tok))
        consumeStartingGreater();
      return true;
    }

    GenericArgs.push_back(Result);

    // Parse the comma, if the list continues.
    if (Tok.is(tok::comma)) {
      consumeToken();
      continue;
    }

    break;
  }

  if (!startsWithGreater(Tok)) {
    diagnose(Tok.getLoc(), diag::expected_rangle_generic_arg_list);
    diagnose(LAngleLoc, diag::opening_angle);

    // Skip until we hit the '>'.
    skipUntilAnyOperator();
    if (startsWithGreater(Tok))
      consumeStartingGreater();
    return true;
  } else {
    consumeStartingGreater();
  }

  Args = Context.AllocateCopy(GenericArgs);
  return false;
}

/// parseTypeIdentifier
///   
///   type-identifier:
///     identifier generic-args? ('.' identifier generic-args?)*
///
bool Parser::parseTypeIdentifier(TypeLoc &Result) {
  SourceLoc StartLoc = Tok.getLoc();
  if (Tok.isNot(tok::identifier)) {
    diagnose(Tok.getLoc(), diag::expected_identifier_for_type);
    return true;
  }

  SmallVector<IdentifierType::Component, 4> Components;
  SourceLoc EndLoc;
  while (true) {
    SourceLoc Loc = Tok.getLoc();
    Identifier Name;
    if (parseIdentifier(Name, diag::expected_identifier_in_dotted_type))
      return true;
    ArrayRef<TypeLoc> GenericArgs;
    if (startsWithLess(Tok)) {
      // FIXME: There's an ambiguity here because code could be trying to
      // refer to a unary '<' operator.
      if (parseGenericArguments(GenericArgs))
        return true;
    }
    Components.push_back(IdentifierType::Component(Loc, Name, GenericArgs));
    EndLoc = Loc;

    // Treat 'Foo.<anything>' as an attempt to write a dotted type
    // unless <anything> is 'metatype'.
    if (Tok.is(tok::period) && peekToken().isNot(tok::kw_metatype)) {
      consumeToken(tok::period);
    } else {
      break;
    }
  }

  // Lookup element #0 through our current scope chains in case it is some thing
  // local (this returns null if nothing is found).
  Components[0].Value = ScopeInfo.lookupValueName(Components[0].Id);

  auto Ty = IdentifierType::getNew(Context, Components);
  UnresolvedIdentifierTypes.emplace_back(Ty, CurDeclContext);
  Result = { Ty, SourceRange(StartLoc, EndLoc) };
  return false;
}

/// parseTypeComposition
///   
///   type-composition:
///     'protocol' '<' type-composition-list? '>'
///
///   type-composition-list:
///     type-identifier (',' type-identifier)*
///
bool Parser::parseTypeComposition(TypeLoc &Result) {
  SourceLoc ProtocolLoc = consumeToken(tok::kw_protocol);
 
  // Check for the starting '<'.
  if (!startsWithLess(Tok)) {
    diagnose(Tok.getLoc(), diag::expected_langle_protocol);
    return true;
  }
  SourceLoc LAngleLoc = consumeStartingLess();
  
  // Check for empty protocol composition.
  if (startsWithGreater(Tok)) {
    SourceLoc RAngleLoc = consumeStartingGreater();
    Type ResultType = ProtocolCompositionType::get(Context, ArrayRef<Type>());
    Result = { ResultType, SourceRange(ProtocolLoc, RAngleLoc) };
    return false;
  }
  
  // Parse the type-composition-list.
  bool Invalid = false;
  SmallVector<TypeLoc, 4> Protocols;
  do {
    // Parse the type-identifier.
    TypeLoc Protocol;
    if (parseTypeIdentifier(Protocol)) {
      Invalid = true;
      break;
    }
    
    Protocols.push_back(Protocol);
    
    if (Tok.is(tok::comma)) {
      consumeToken();
      continue;
    }
    
    break;
  } while (true);
  
  // Check for the terminating '>'.
  SourceLoc EndLoc = Tok.getLoc();
  if (!startsWithGreater(Tok)) {
    if (!Invalid) {
      diagnose(Tok.getLoc(), diag::expected_rangle_protocol);
      diagnose(LAngleLoc, diag::opening_angle);
    }

    // Skip until we hit the '>'.
    skipUntilAnyOperator();
    if (startsWithGreater(Tok))
      EndLoc = consumeStartingGreater();    
  } else {
    EndLoc = consumeStartingGreater();
  }

  SmallVector<Type, 4> ProtocolTypes;
  for (TypeLoc T : Protocols)
    ProtocolTypes.push_back(T.getType());
  Result =  { ProtocolCompositionType::get(Context, ProtocolTypes),
              SourceRange(ProtocolLoc, EndLoc) };
  return false;
}

/// parseTypeTupleBody
///   type-tuple:
///     lparen-any type-tuple-body? ')'
///   type-tuple-body:
///     type-tuple-element (',' type-tuple-element)* '...'?
///   type-tuple-element:
///     identifier value-specifier
///     type-annotation
bool Parser::parseTypeTupleBody(SourceLoc LPLoc, TypeLoc &Result) {
  SmallVector<TupleTypeElt, 8> Elements;
  bool HadExpr = false;

  if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::r_brace) &&
      Tok.isNot(tok::ellipsis) && !isStartOfDecl(Tok, peekToken())) {
    bool HadError = false;
    do {
      // If the tuple element starts with "ident :" or "ident =", then
      // the identifier is an element tag, and it is followed by a
      // value-specifier.
      if (Tok.is(tok::identifier) &&
          (peekToken().is(tok::colon) || peekToken().is(tok::equal))) {
        Identifier name = Context.getIdentifier(Tok.getText());
        consumeToken(tok::identifier);

        NullablePtr<Expr> init;
        TypeLoc type;
        if ((HadError = parseValueSpecifier(type, init)))
          break;

        ExprHandle *initHandle = nullptr;
        if (init.isNonNull()) {
          HadExpr = true;
          initHandle = ExprHandle::get(Context, init.get());
        }
        HadExpr |= init.isNonNull();
        Elements.push_back(TupleTypeElt(type.getType(), name, initHandle));
        continue;
      }

      // Otherwise, this has to be a type.
      TypeLoc type;
      if ((HadError = parseTypeAnnotation(type)))
        break;

      Elements.push_back(TupleTypeElt(type.getType(), Identifier(), nullptr));
    } while (consumeIf(tok::comma));
    
    if (HadError) {
      skipUntil(tok::r_paren);
      if (Tok.is(tok::r_paren))
        consumeToken(tok::r_paren);
      return true;
    }
  }

  bool HadEllipsis = false;
  SourceLoc EllipsisLoc;
  if (Tok.is(tok::ellipsis)) {
    EllipsisLoc = consumeToken();
    HadEllipsis = true;
  }

  // A "tuple" with one anonymous element is actually not a tuple.
  if (Elements.size() == 1 && !Elements.back().hasName() && !HadEllipsis) {
    assert(!HadExpr && "Only TupleTypes have default values");
    Result = { ParenType::get(Context, Elements.back().getType()),
               SourceRange(LPLoc, Tok.getLoc()) };
    return false;
  }

  if (HadEllipsis) {
    if (Elements.empty()) {
      diagnose(EllipsisLoc, diag::empty_tuple_ellipsis);
      return true;
    }
    if (Elements.back().getInit()) {
      diagnose(EllipsisLoc, diag::tuple_ellipsis_init);
      return true;
    }
    Type BaseTy = Elements.back().getType();
    Type FullTy = ArraySliceType::get(BaseTy, Context);
    Identifier Name = Elements.back().getName();
    ExprHandle *Init = Elements.back().getInit();
    Elements.back() = TupleTypeElt(FullTy, Name, Init, BaseTy);
  }

  TupleType *TT = TupleType::get(Elements, Context)->castTo<TupleType>();
  if (HadExpr)
    TypesWithDefaultValues.emplace_back(TT, CurDeclContext);
  Result = { TT, SourceRange(LPLoc, Tok.getLoc()) };
  return false;
}


/// parseTypeArray - Parse the type-array production, given that we
/// are looking at the initial l_square.  Note that this index
/// clause is actually the outermost (first-indexed) clause.
///
///   type-array:
///     type-simple
///     type-array '[' ']'
///     type-array '[' expr ']'
///
bool Parser::parseTypeArray(TypeLoc &result) {
  SourceLoc lsquareLoc = Tok.getLoc();
  consumeToken(tok::l_square);

  // Handle the [] production, meaning an array slice.
  if (Tok.is(tok::r_square)) {
    SourceLoc rsquareLoc = consumeToken(tok::r_square);

    // If we're starting another square-bracket clause, recurse.
    if (Tok.is(tok::l_square) && parseTypeArray(result))
      return true;

    // Just build a normal array slice type.
    SourceRange arrayRange{ result.getSourceRange().Start, rsquareLoc };
    result = { ArraySliceType::get(result.getType(), Context),
               arrayRange };
    return false;
  }
  
  NullablePtr<Expr> sizeEx = parseExpr(diag::expected_expr_array_type);
  if (sizeEx.isNull()) return true;

  SourceLoc rsquareLoc;
  if (parseMatchingToken(tok::r_square, rsquareLoc,
                         diag::expected_rbracket_array_type,
                         lsquareLoc, diag::opening_bracket))
    return true;

  // If we're starting another square-bracket clause, recurse.
  if (Tok.is(tok::l_square) && parseTypeArray(result))
    return true;
  
  // FIXME: We don't supported fixed-length arrays yet.
  diagnose(lsquareLoc, diag::unsupported_fixed_length_array)
    << sizeEx.get()->getSourceRange();
  
  return true;
}

