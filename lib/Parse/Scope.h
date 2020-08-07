//===--- Scope.h - Scope Abstraction ----------------------------*- C++ -*-===//
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
// This file defines the Sema interface which implement hooks invoked by the 
// parser to build the AST.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SEMA_SCOPE_H
#define SWIFT_SEMA_SCOPE_H

#include "swift/AST/Identifier.h"
#include "llvm/ADT/ScopedHashTable.h"
#include "llvm/ADT/SmallVector.h"

namespace swift {
  class SourceLoc;
  class ValueDecl;
  class TypeAliasDecl;
  class Parser;
  class Scope;
  class Type;

/// ScopeInfo - A single instance of this class is maintained by the Parser to
/// track the current scope.
class ScopeInfo {
  friend class Scope;
public:
  typedef std::pair<unsigned, ValueDecl*> ValueScopeEntry;
  
  typedef llvm::ScopedHashTable<Identifier, ValueScopeEntry> ValueScopeHTTy;
private:
  Parser &TheParser;
  ValueScopeHTTy ValueScopeHT;
  Scope *CurScope;
  unsigned ResolvableDepth;

public:
  ScopeInfo(Parser &TheParser) : TheParser(TheParser), CurScope(0) {}
  ~ScopeInfo() {}

  ValueDecl *lookupValueName(Identifier Name) {
    // If we found nothing, or we found a decl at the top-level, return nothing.
    // We ignore results at the top-level because we may have overloading that
    // will be resolved properly by name binding.
    std::pair<unsigned, ValueDecl*> Res = ValueScopeHT.lookup(Name);
    if (Res.first < ResolvableDepth) return 0;
    return Res.second;
  }

  /// addToScope - Register the specified decl as being in the current lexical
  /// scope.
  void addToScope(ValueDecl *D);  
};


/// Scope - This class represents lexical scopes.  These objects are created
/// and destroyed as the parser is running, and name lookup happens relative
/// to them.
///
class Scope {
  Scope(const Scope&) = delete;
  void operator=(const Scope&) = delete;
  ScopeInfo &SI;
  llvm::ScopedHashTableScope<Identifier, 
                             ScopeInfo::ValueScopeEntry> ValueHTScope;

  Scope *PrevScope;
  unsigned PrevResolvableDepth;
  unsigned Depth;
public:
  explicit Scope(Parser *P, bool ResolvableScope);
  ~Scope() {
    assert(SI.CurScope == this && "Scope mismatch");
    SI.CurScope = PrevScope;
    SI.ResolvableDepth = PrevResolvableDepth;
  }

  unsigned getDepth() const {
    return Depth;
  }
};

} // end namespace swift

#endif
