//===--- ASTContext.h - AST Context Object ----------------------*- C++ -*-===//
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
// This file defines the ASTContext interface.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_AST_ASTCONTEXT_H
#define SWIFT_AST_ASTCONTEXT_H

#include "llvm/Support/DataTypes.h"
#include "swift/AST/Type.h"
#include "swift/Basic/LangOptions.h"
#include "swift/Basic/Optional.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/DenseMap.h"
#include <vector>
#include <utility>

namespace llvm {
  class BumpPtrAllocator;
  class SourceMgr;
}

namespace swift {
  class ASTContext;
  class BoundGenericType;
  class SourceLoc;
  class Type;
  class TupleType;
  class FunctionType;
  class ArchetypeType;
  class ArrayType;
  class Identifier;
  class Module;
  class TupleTypeElt;
  class OneOfElementDecl;
  class ProtocolDecl;
  class SubstitutableType;
  class ValueDecl;
  class DiagnosticEngine;
  class Substitution;
  
/// \brief Type substitution mapping from substitutable types to their
/// replacements.
typedef llvm::DenseMap<SubstitutableType *, Type> TypeSubstitutionMap;

/// \brief Describes how a particular type conforms to a given protocol,
/// providing the mapping from the protocol members to the type (or extension)
/// members that provide the functionality for the concrete type.
class ProtocolConformance {
public:
  /// \brief The mapping of individual requirements in the protocol over to
  /// the declarations that satisfy those requirements.
  llvm::DenseMap<ValueDecl *, ValueDecl *> Mapping;
  
  /// \brief The mapping of individual archetypes in the protocol over to
  /// the types used to satisy the type requirements.
  TypeSubstitutionMap TypeMapping;
  
  /// \brief The mapping from any directly-inherited protocols over to the
  /// protocol conformance structures that indicate how the given type meets
  /// the requirements of those protocols.
  llvm::DenseMap<ProtocolDecl *, ProtocolConformance *> InheritedMapping;
};

/// \brief The arena in which a particular ASTContext allocation will go.
enum class AllocationArena {
  /// \brief The permanent arena, which is tied to the lifetime of
  /// the ASTContext.
  ///
  /// All global declarations and types need to be allocated into this arena.
  /// At present, everything that is not a type involving a type variable is
  /// allocated in this arena.
  Permanent,
  /// \brief The constraint solver's temporary arena, which is tied to the
  /// lifetime of a particular instance of the constraint solver.
  ///
  /// Any type involving a type variable is allocated in this arena.
  ConstraintSolver
};

/// \brief Introduces a new constraint checker arena, whose lifetime is
/// tied to the lifetime of this RAII object.
class ConstraintCheckerArenaRAII {
  ASTContext &Self;
  void *Data;

public:
  /// \brief Introduces a new constraint checker arena, supplanting any
  /// existing constraint checker arena.
  ///
  /// \param self The ASTContext into which this constraint checker arena
  /// will be installed.
  ///
  /// \param allocator The allocator used for allocating any data that
  /// goes into the constraint checker arena.
  ConstraintCheckerArenaRAII(ASTContext &self,
                             llvm::BumpPtrAllocator &allocator);

  ConstraintCheckerArenaRAII(const ConstraintCheckerArenaRAII &) = delete;
  ConstraintCheckerArenaRAII(ConstraintCheckerArenaRAII &&) = delete;

  ConstraintCheckerArenaRAII &
  operator=(const ConstraintCheckerArenaRAII &) = delete;

  ConstraintCheckerArenaRAII &
  operator=(ConstraintCheckerArenaRAII &&) = delete;

  ~ConstraintCheckerArenaRAII();
};

/// ASTContext - This object creates and owns the AST objects.
class ASTContext {
  ASTContext(const ASTContext&) = delete;
  void operator=(const ASTContext&) = delete;

public:
  // Members that should only be used by ASTContext.cpp.
  struct Implementation;
  Implementation &Impl;
  
  friend class ConstraintCheckerArenaRAII;
public:
  
  ASTContext(LangOptions &langOpts, llvm::SourceMgr &SourceMgr,
             DiagnosticEngine &Diags);
  ~ASTContext();

  /// \brief The language options used for translation.
  LangOptions &LangOpts;

  /// SourceMgr - The source manager object.
  llvm::SourceMgr &SourceMgr;

  /// Diags - The diagnostics engine.
  DiagnosticEngine &Diags;
  
  /// LoadedModules - The set of modules we have loaded.
  llvm::StringMap<Module*> LoadedModules;
  
  /// TheBuiltinModule - The builtin module.
  Module * const TheBuiltinModule;

  /// ImportSearchPaths - The paths to search for imports in.
  std::vector<std::string> ImportSearchPaths;

  // FIXME: Once DenseMap learns about move semantics, use std::unique_ptr
  // and remove the explicit delete loop in the destructor.
  typedef llvm::DenseMap<std::pair<CanType, ProtocolDecl *>, 
                         ProtocolConformance*> ConformsToMap;
  
  /// ConformsTo - Caches the results of checking whether a given (canonical)
  /// type conforms to a given protocol.
  ConformsToMap ConformsTo;
  
  /// \brief Retrieve the allocator for the given arena.
  llvm::BumpPtrAllocator &
  getAllocator(AllocationArena arena = AllocationArena::Permanent) const;

  /// Allocate - Allocate memory from the ASTContext bump pointer.
  void *Allocate(unsigned long bytes, unsigned alignment,
                 AllocationArena arena = AllocationArena::Permanent) {
    return getAllocator(arena).Allocate(bytes, alignment);
  }


  template <typename T>
  T *Allocate(unsigned numElts,
              AllocationArena arena = AllocationArena::Permanent) {
    T *res = (T*)Allocate(sizeof(T)*numElts, __alignof__(T), arena);
    for (unsigned i = 0; i != numElts; ++i)
      new (res+i) T();
    return res;
  }

  template <typename T, typename It>
  T *AllocateCopy(It start, It end,
                  AllocationArena arena = AllocationArena::Permanent) {
    T *res = (T*)Allocate(sizeof(T)*(end-start), __alignof__(T), arena);
    for (unsigned i = 0; start != end; ++start, ++i)
      new (res+i) T(*start);
    return res;
  }

  template<typename T>
  ArrayRef<T> AllocateCopy(ArrayRef<T> array,
                           AllocationArena arena = AllocationArena::Permanent) {
    return ArrayRef<T>(AllocateCopy<T>(array.begin(), array.end(), arena),
                       array.size());
  }
  
  template<typename T>
  MutableArrayRef<T>
  AllocateCopy(MutableArrayRef<T> array,
               AllocationArena arena = AllocationArena::Permanent) {
    return MutableArrayRef<T>(AllocateCopy<T>(array.begin(), array.end(),arena),
                       array.size());
  }


  template<typename T>
  ArrayRef<T> AllocateCopy(const SmallVectorImpl<T> &vec,
                           AllocationArena arena = AllocationArena::Permanent) {
    return AllocateCopy(ArrayRef<T>(vec), arena);
  }

  template<typename T>
  MutableArrayRef<T>
  AllocateCopy(SmallVectorImpl<T> &vec,
               AllocationArena arena = AllocationArena::Permanent) {
    return AllocateCopy(MutableArrayRef<T>(vec), arena);
  }

  

  /// getIdentifier - Return the uniqued and AST-Context-owned version of the
  /// specified string.
  Identifier getIdentifier(StringRef Str);

  //===--------------------------------------------------------------------===//
  // Diagnostics Helper functions
  //===--------------------------------------------------------------------===//

  bool hadError() const;
  
  //===--------------------------------------------------------------------===//
  // Type manipulation routines.
  //===--------------------------------------------------------------------===//

  // Builtin type and simple types that are used frequently.
  const Type TheErrorType;       /// TheErrorType - This is the error singleton.
  const Type TheEmptyTupleType;  /// TheEmptyTupleType - This is "()"
  const Type TheObjectPointerType; /// Builtin.ObjectPointer
  const Type TheObjCPointerType; /// Builtin.ObjCPointer
  const Type TheRawPointerType;  /// Builtin.RawPointer
  
  /// TheUnstructuredUnresolvedType - Unresolved on context.  This is given to an 
  /// anonymous closure argument (e.g. $4) and to UnresolvedMemberExprs 
  /// (e.g. .foo) during type checking until they are resolved to something with 
  /// concrete type.
  const Type TheUnstructuredUnresolvedType;
  const Type TheIEEE32Type;     /// TheIEEE32Type  - 32-bit IEEE floating point
  const Type TheIEEE64Type;     /// TheIEEE64Type  - 64-bit IEEE floating point
  
  // Target specific types.
  const Type TheIEEE16Type;     /// TheIEEE16Type  - 16-bit IEEE floating point
  const Type TheIEEE80Type;     /// TheIEEE80Type  - 80-bit IEEE floating point
  const Type TheIEEE128Type;    /// TheIEEE128Type - 128-bit IEEE floating point
  const Type ThePPC128Type;     /// ThePPC128Type  - 128-bit PowerPC 2xDouble

private:
  friend class BoundGenericType;

  /// \brief Retrieve the substitutions for a bound generic type, if known.
  Optional<ArrayRef<Substitution>> getSubstitutions(BoundGenericType* Bound);

  /// \brief Set the substitutions for the given bound generic type.
  void setSubstitutions(BoundGenericType* Bound, ArrayRef<Substitution> Subs);
};
  
} // end namespace swift

#endif
