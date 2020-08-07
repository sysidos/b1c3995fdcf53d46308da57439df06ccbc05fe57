//===-- TypeVisitor.h - IR-gen TypeVisitor specialization -------*- C++ -*-===//
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
// This file defines swift::irgen::TypeVisitor.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_IRGEN_TYPEVISITOR_H
#define SWIFT_IRGEN_TYPEVISITOR_H

#include "swift/AST/TypeVisitor.h"

namespace swift {
namespace irgen {

/// irgen::TypeVisitor - This is a specialization of
/// swift::TypeVisitor which works only on canonical types and
/// which automatically ignores certain AST node kinds.
template<typename ImplClass, typename RetTy = void, typename... Args>
class TypeVisitor : public swift::TypeVisitor<ImplClass, RetTy, Args...> {
public:

  template <class... As> RetTy visit(CanType type, Args... args) {
    return swift::TypeVisitor<ImplClass, RetTy, Args...>
                            ::visit(Type(type), std::forward<Args>(args)...);
  }

#define TYPE(Id, Parent)
#define UNCHECKED_TYPE(Id, Parent) \
  template <class... As> RetTy visit##Id##Type(Id##Type *T, Args... args) { \
    llvm_unreachable(#Id "Type should not survive to IR-gen"); \
  }
#define SUGARED_TYPE(Id, Parent) \
  template <class... As> RetTy visit##Id##Type(Id##Type *T, Args... args) { \
    llvm_unreachable(#Id "Type should not survive canonicalization"); \
  }
#include "swift/AST/TypeNodes.def"

  RetTy visitDeducibleGenericParamType(DeducibleGenericParamType *T,
                                       Args... args) {
    llvm_unreachable("DeducibleGenericParamType should not survive Sema");
  }

  template <class... As>
  RetTy visitUnboundGenericType(UnboundGenericType *T, Args... args) {
    llvm_unreachable("UnboundGenericType should not survive Sema");
  }
};

/// SubstTypeVisitor - This is a specialized type visitor for visiting
/// both a type and the result of substituting it.  The original type
/// drives the selection, not the substitution result.
///
/// For most type kinds, the substitution type preserves the same
/// structure as the original, and so the methods you declare
/// should look like:
///   visitFunctionType(FunctionType *origTy, FunctionType *substTy);
///
/// Archetypes are an exception, and the second parameter should just
/// be a CanType:
///   visitArchetypeType(ArchetypeType *origTy, CanType substTy);
///
/// In addition, all the leaf type kinds map to the same function:
///   visitLeafType(CanType origTy, CanType substTy);
template<typename Impl, typename RetTy = void> class SubstTypeVisitor {
protected:
  typedef SubstTypeVisitor super;

public:
  RetTy visit(CanType origTy, CanType substTy) {
    switch (origTy->getKind()) {
#define TYPE(Id, Parent)
#define UNCHECKED_TYPE(Id, Parent) \
    case TypeKind::Id: \
      llvm_unreachable(#Id "Type should not survive to IR-gen");
#define SUGARED_TYPE(Id, Parent) \
    case TypeKind::Id: \
      llvm_unreachable(#Id "Type should not survive canonicalization");
#include "swift/AST/TypeNodes.def"

    case TypeKind::DeducibleGenericParam:
      llvm_unreachable("DeducibleGenericParamType should not survive Sema");

    case TypeKind::UnboundGeneric:
      llvm_unreachable("UnboundGeneric should not survive Sema");

    case TypeKind::BuiltinFloat:
    case TypeKind::BuiltinInteger:
    case TypeKind::BuiltinObjectPointer:
    case TypeKind::BuiltinObjCPointer:
    case TypeKind::BuiltinRawPointer:
    case TypeKind::Class:
    case TypeKind::Module:
    case TypeKind::OneOf:
    case TypeKind::Protocol:
    case TypeKind::ProtocolComposition:
    case TypeKind::Struct:
      return static_cast<Impl*>(this)->visitLeafType(origTy, substTy);

    case TypeKind::Archetype:
      return static_cast<Impl*>(this)
        ->visitArchetypeType(cast<ArchetypeType>(origTy),
                             substTy);

#define DISPATCH(Concrete)                                      \
    case TypeKind::Concrete:                                    \
      return static_cast<Impl*>(this)                           \
        ->visit##Concrete##Type(cast<Concrete##Type>(origTy),   \
                                cast<Concrete##Type>(substTy));
    DISPATCH(Array)
    DISPATCH(BoundGenericClass)
    DISPATCH(BoundGenericOneOf)
    DISPATCH(BoundGenericStruct)
    DISPATCH(Function)
    DISPATCH(PolymorphicFunction)
    DISPATCH(LValue)
    DISPATCH(MetaType)
    DISPATCH(Tuple)
#undef DISPATCH
    }
    llvm_unreachable("bad type kind");
  }

#define DEFER_TO_SUPERTYPE(Concrete, Abstract) \
  RetTy visit##Concrete##Type(Concrete##Type *origTy, Concrete##Type *substTy) { \
    return static_cast<Impl*>(this)->visit##Abstract##Type(origTy, substTy); \
  }
  DEFER_TO_SUPERTYPE(Function, AnyFunction)
  DEFER_TO_SUPERTYPE(PolymorphicFunction, AnyFunction)
  DEFER_TO_SUPERTYPE(BoundGenericClass, BoundGeneric)
  DEFER_TO_SUPERTYPE(BoundGenericOneOf, BoundGeneric)
  DEFER_TO_SUPERTYPE(BoundGenericStruct, BoundGeneric)
#undef DEFER_TO_SUPERTYPE
};
  
} // end namespace irgen
} // end namespace swift
  
#endif
