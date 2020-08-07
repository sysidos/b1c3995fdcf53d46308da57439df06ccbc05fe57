//===--- GenTuple.cpp - Swift IR Generation For Tuple Types ---------------===//
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
//  This file implements IR generation for tuple types in Swift.  This
//  includes creating the IR type as  well as emitting the primitive access
//  operations.
//
//  It is assumed in several places in IR-generation that the
//  explosion schema of a tuple type is always equal to the appended
//  explosion schemas of the component types.
//
//===----------------------------------------------------------------------===//

#include "swift/AST/Types.h"
#include "swift/AST/Decl.h"
#include "swift/AST/Expr.h"
#include "swift/AST/ExprHandle.h"
#include "swift/AST/Pattern.h"
#include "swift/Basic/Optional.h"
#include "llvm/DerivedTypes.h"

#include "ASTVisitor.h"
#include "GenArray.h"
#include "GenHeap.h"
#include "GenInit.h"
#include "GenSequential.h"
#include "GenType.h"
#include "IRGenFunction.h"
#include "IRGenModule.h"
#include "LValue.h"
#include "Explosion.h"

#include "GenTuple.h"

using namespace swift;
using namespace irgen;

namespace {
  class TupleFieldInfo : public SequentialField<TupleFieldInfo> {
  public:
    TupleFieldInfo(const TupleTypeElt &field, const TypeInfo &type)
      : SequentialField(type), Field(field) {}

    /// The field.
    const TupleTypeElt &Field;

    StringRef getFieldName() const {
      if (Field.hasName())
        return Field.getName().str();
      return "elt";
    }
  };

  /// Layout information for tuple types.
  class TupleTypeInfo :
    public SequentialTypeInfo<TupleTypeInfo, TupleFieldInfo> {
  public:
    TupleTypeInfo(llvm::Type *T, unsigned numFields)
      : SequentialTypeInfo(T, numFields) {
    }
  };

  class TupleTypeBuilder :
    public SequentialTypeBuilder<TupleTypeBuilder, TupleTypeInfo, TupleTypeElt>{

  public:
    TupleTypeBuilder(IRGenModule &IGM) : SequentialTypeBuilder(IGM) {}

    TupleTypeInfo *construct(void *buffer, ArrayRef<TupleTypeElt> fields) {
      return ::new(buffer) TupleTypeInfo(IGM.Int8Ty, fields.size());
    }

    TupleFieldInfo getFieldInfo(const TupleTypeElt &field,
                                const TypeInfo &fieldTI) {
      return TupleFieldInfo(field, fieldTI);
    }

    Type getType(const TupleTypeElt &field) { return field.getType(); }

    void performLayout(ArrayRef<const TypeInfo *> fieldTypes) {
      StructLayout layout(IGM, LayoutKind::NonHeapObject,
                          LayoutStrategy::Universal, fieldTypes);
      recordLayout(layout, layout.getType());
    }
  };
}

static const TupleTypeInfo &getAsTupleTypeInfo(const TypeInfo &typeInfo) {
  // It'd be nice to get some better verification than this.
#ifdef __GXX_RTTI
  assert(dynamic_cast<const TupleTypeInfo*>(&typeInfo));
#endif

  return typeInfo.as<TupleTypeInfo>();
}

static const TupleTypeInfo &getAsTupleTypeInfo(IRGenFunction &IGF, Type type) {
  assert(type->is<TupleType>());
  return getAsTupleTypeInfo(IGF.getFragileTypeInfo(type));
}

const TypeInfo *TypeConverter::convertTupleType(TupleType *T) {
  TupleTypeBuilder builder(IGM);
  builder.create(T->getFields());
  return builder.complete(T->getFields());
}

void swift::irgen::emitTupleLiteral(IRGenFunction &IGF, TupleExpr *E,
                                    Explosion &explosion) {
 for (Expr *elt : E->getElements())
   if (!elt) {
     IGF.unimplemented(E->getLoc(), "tuple default element");
     IGF.emitFakeExplosion(IGF.getFragileTypeInfo(E->getType()),
                           explosion);
     return;
   }

  // Emit all the sub-expressions.
  for (Expr *elt : E->getElements())
    IGF.emitRValue(elt, explosion);
}

namespace {
  class TupleElement : public PhysicalPathComponent {
    const TupleFieldInfo &Field;

  public:
    TupleElement(const TupleFieldInfo &field) : Field(field) {}

    OwnedAddress offset(IRGenFunction &IGF, OwnedAddress addr) const {
      Address project = Field.projectAddress(IGF, addr);
      return OwnedAddress(project, addr.getOwner());
    }
  };
}

void swift::irgen::emitTupleElement(IRGenFunction &IGF, TupleElementExpr *E,
                                    Explosion &explosion) {
  // If we're doing an l-value projection, this is straightforward.
  if (LValueType *lv = E->getType()->getAs<LValueType>())
    return IGF.emitLValueAsScalar(emitTupleElementLValue(IGF, E),
                                  lv->isHeap() ? OnHeap : NotOnHeap,
                                  explosion);

  Expr *tuple = E->getBase();
  const TupleTypeInfo &tupleType = getAsTupleTypeInfo(IGF, tuple->getType());

  const TupleFieldInfo &field =
    tupleType.getFields()[E->getFieldNumber()];

  // If the field requires no storage, there's nothing to do.
  if (field.isEmpty()) {
    // Emit the base in case it has side-effects.
    IGF.emitIgnored(tuple);
    return IGF.emitFakeExplosion(field.getTypeInfo(), explosion);
  }

  // If we can emit the base as an l-value, we can avoid a lot
  // of unnecessary work.
  if (Optional<Address> tupleAddr = IGF.tryEmitAsAddress(tuple, tupleType)) {
    Address addr = field.projectAddress(IGF, tupleAddr.getValue());
    return field.getTypeInfo().load(IGF, addr, explosion);
  }

  // Otherwise, emit the base as an r-value and project.
  Explosion tupleExplosion(explosion.getKind());
  IGF.emitRValue(tuple, tupleExplosion);

  auto fieldRange = field.getProjectionRange(explosion.getKind());

  // Ignore up to the start of the range.
  tupleExplosion.ignoreAndDestroy(IGF, fieldRange.first);

  // Transfer the correct range.
  tupleExplosion.transferInto(explosion, fieldRange.second - fieldRange.first);

  // Ignore everything else.
  tupleExplosion.ignoreAndDestroy(IGF, tupleExplosion.size());
}

/// Try to emit a tuple-element reference expression as an address.
Optional<Address>
swift::irgen::tryEmitTupleElementAsAddress(IRGenFunction &IGF,
                                           TupleElementExpr *E) {
  Expr *tuple = E->getBase();

  // There are two kinds of TupleElementExprs; ones where the input is an
  // lvalue, and ones where the input is an rvalue.  Either way, we just
  // want to tryEmitAsAddress on the operand and GEP into it.
  CanType TT = tuple->getType()->getCanonicalType();
  if (!isa<TupleType>(TT))
    TT = cast<LValueType>(TT)->getObjectType()->getCanonicalType();

  const TupleTypeInfo &tupleType = getAsTupleTypeInfo(IGF, TT);

  // This is contigent exclusively on whether we can emit an address
  // for the tuple.
  Optional<Address> tupleAddr = IGF.tryEmitAsAddress(tuple, tupleType);
  if (!tupleAddr) return Nothing;

  // We succeeded;  now just GEP down.
  const TupleFieldInfo &field =
    tupleType.getFields()[E->getFieldNumber()];
  if (field.isEmpty()) return Address();

  return field.projectAddress(IGF, tupleAddr.getValue());
}

LValue swift::irgen::emitTupleElementLValue(IRGenFunction &IGF,
                                            TupleElementExpr *E) {
  assert(E->getType()->is<LValueType>());

  // Emit the base l-value.
  Expr *tuple = E->getBase();
  LValue tupleLV = IGF.emitLValue(tuple);

  Type tupleType = tuple->getType()->castTo<LValueType>()->getObjectType();
  const TupleTypeInfo &tupleTI = getAsTupleTypeInfo(IGF, tupleType);
  const TupleFieldInfo &field =
    tupleTI.getFields()[E->getFieldNumber()];

  // If the field requires no storage, there's nothing to do.
  if (field.isEmpty()) {
    return tupleLV; // as good as anything
  }

  // Project.
  tupleLV.add<TupleElement>(field);
  return tupleLV;
}

void swift::irgen::emitScalarToTuple(IRGenFunction &IGF, ScalarToTupleExpr *E,
                                     Explosion &outerTupleExplosion) {
  Expr *innerExpr = E->getSubExpr();
  const TypeInfo &innerType = IGF.getFragileTypeInfo(innerExpr->getType());

  // Emit the inner tuple.  We prefer to emit it as an address.
  Explosion innerExplosion(outerTupleExplosion.getKind());
  Address innerAddr;
#if 0
  // FIXME: This currently explodes because of a bug in GenFunc.
  if (auto addr = IGF.tryEmitAsAddress(innerExpr, innerType)) {
    innerAddr = addr.getValue();
  } else {
    IGF.emitRValue(innerExpr, innerExplosion);
  }
#else
  IGF.emitRValue(innerExpr, innerExplosion);
#endif

  ArrayRef<TupleTypeElt> outerFields =
    cast<TupleType>(E->getType()->getCanonicalType())->getFields();

  unsigned destIndex = 0;
  for (const TupleTypeElt &outerField : outerFields) {
    // If we have a field with a default value, emit that value.
    if (destIndex++ != E->getScalarField()) {
      assert(outerField.hasInit() && "no default initializer for field!");
      IGF.emitRValue(outerField.getInit()->getExpr(), outerTupleExplosion);
      continue;
    }

    // If we have a varargs injection function, use it.
    // FIXME: This code is duplicated; refactor with emitTupleShuffle once it
    // stabilizes.
    if (E->getVarargsInjectionFunction()) {
      llvm::Value *length = IGF.Builder.getInt64(1);
      const TypeInfo &elementTI =
        IGF.getFragileTypeInfo(CanType(outerField.getVarargBaseTy()));

      Expr *init = nullptr;
      ArrayHeapLayout layout(IGF, CanType(outerField.getVarargBaseTy()));

      // Allocate the array.
      // FIXME: This includes an unnecessary memset.
      // FIXME: It would be nice if we could eventually avoid heap-allocating
      // this array.
      Address begin;
      ManagedValue alloc =
        layout.emitAlloc(IGF, length, begin, init, "new-array");

      // Perform the call which generates the slice value.
      emitArrayInjectionCall(IGF, alloc, begin,
                             CanType(outerField.getType()),
                             E->getVarargsInjectionFunction(), length,
                             outerTupleExplosion);

      if (innerAddr.isValid()) {
        // If we have an l-value, copy from that.
        elementTI.initializeWithCopy(IGF, begin, innerAddr);
      } else {
        // Otherwise, store the r-value down.
        elementTI.initialize(IGF, innerExplosion, begin);
      }
      break;
    }

    if (innerAddr.isValid()) {
      // If we're loading from an l-value, project from that.
      innerType.load(IGF, innerAddr, outerTupleExplosion);
    } else {
      // Otherwise, project the r-value down.
      outerTupleExplosion.add(innerExplosion.claimAll());
    }
  }
}

/// emitTupleShuffle - Emit a tuple-shuffle expression
/// as an exploded r-value.
void swift::irgen::emitTupleShuffle(IRGenFunction &IGF, TupleShuffleExpr *E,
                                    Explosion &outerTupleExplosion) {
  Expr *innerTuple = E->getSubExpr();
  const TupleTypeInfo &innerTupleType =
    getAsTupleTypeInfo(IGF, innerTuple->getType());

  // Emit the inner tuple.  We prefer to emit it as an address.
  Explosion innerTupleExplosion(outerTupleExplosion.getKind());
  Address innerTupleAddr;
  if (Optional<Address> addr
        = IGF.tryEmitAsAddress(innerTuple, innerTupleType)) {
    innerTupleAddr = addr.getValue();
  } else {
    IGF.emitRValue(innerTuple, innerTupleExplosion);
  }

  llvm::ArrayRef<TupleTypeElt> outerFields =
    cast<TupleType>(E->getType()->getCanonicalType())->getFields();

  auto shuffleIndexIterator = E->getElementMapping().begin();
  for (const TupleTypeElt &outerField : outerFields) {
    int shuffleIndex = *shuffleIndexIterator++;

    // If the shuffle index is -1, we're supposed to use the default value.
    if (shuffleIndex == -1) {
      assert(outerField.hasInit() && "no default initializer for field!");
      IGF.emitRValue(outerField.getInit()->getExpr(), outerTupleExplosion);
      continue;
    }

    // If the shuffle index is -2, it is the beginning of the list of
    // varargs inputs.
    if (shuffleIndex == -2) {
      auto shuffleIndexIteratorEnd = E->getElementMapping().end();
      unsigned numElems = shuffleIndexIteratorEnd - shuffleIndexIterator;
      llvm::Value *length = IGF.Builder.getInt64(numElems);

      const TypeInfo &elementTI =
        IGF.getFragileTypeInfo(CanType(outerField.getVarargBaseTy()));

      Expr *init = nullptr;
      ArrayHeapLayout layout(IGF, CanType(outerField.getVarargBaseTy()));

      // Allocate the array.
      // FIXME: This includes an unnecessary memset.
      // FIXME: It would be nice if we could eventually avoid heap-allocating
      // this array.
      Address begin;
      ManagedValue alloc =
        layout.emitAlloc(IGF, length, begin, init, "new-array");

      // Perform the call which generates the slice value.
      emitArrayInjectionCall(IGF, alloc, begin,
                             outerField.getType()->getCanonicalType(),
                             E->getVarargsInjectionFunction(), length,
                             outerTupleExplosion);

      // Emit all the elements into the allocated array.
      unsigned curElem = 0;
      while (shuffleIndexIterator != shuffleIndexIteratorEnd) {
        int shuffleIndex = *shuffleIndexIterator++;
        const TupleFieldInfo &innerField
          = innerTupleType.getFields()[(unsigned) shuffleIndex];
        Explosion varargTupleExplosion(ExplosionKind::Maximal);
        llvm::Value *curElemVal = IGF.Builder.getInt64(curElem++);
        llvm::Value *destValue = IGF.Builder.CreateGEP(begin.getAddress(),
                                                       curElemVal);
        Address destAddr(destValue, begin.getAlignment());

        // If we're loading from an l-value, project from that.
        if (innerTupleAddr.isValid()) {
          Address elementAddr = innerField.projectAddress(IGF, innerTupleAddr);
          elementTI.initializeWithCopy(IGF, destAddr, elementAddr);

        // Otherwise, project the r-value down.
        } else {
          // Get the range of elements and project those down.
          auto fieldRange =
            innerField.getProjectionRange(innerTupleExplosion.getKind());
          varargTupleExplosion.add(
            innerTupleExplosion.getRange(fieldRange.first, fieldRange.second));
          elementTI.initialize(IGF, varargTupleExplosion, destAddr);
        }
      }
      break;
    }

    // Otherwise, we need to map from a different tuple.
    assert(shuffleIndex >= 0 &&
           (unsigned) shuffleIndex < outerFields.size());

    const TupleFieldInfo &innerField
      = innerTupleType.getFields()[(unsigned) shuffleIndex];

    // If we're loading from an l-value, project from that.
    if (innerTupleAddr.isValid()) {
      Address elementAddr = innerField.projectAddress(IGF, innerTupleAddr);
      innerField.getTypeInfo().load(IGF, elementAddr, outerTupleExplosion);

    // Otherwise, project the r-value down.
    } else {
      // Get the range of elements and project those down.
      auto fieldRange =
        innerField.getProjectionRange(innerTupleExplosion.getKind());
      outerTupleExplosion.add(innerTupleExplosion.getRange(fieldRange.first,
                                                           fieldRange.second));
    }
  }

  // Tuple shuffles always use everything from the inner tuple.
  innerTupleExplosion.markClaimed(innerTupleExplosion.size());
}

namespace {
  /// A visitor for initializing a pattern from an address.
  struct InitPatternFromAddress
      : irgen::PatternVisitor<InitPatternFromAddress> {
    IRGenFunction &IGF;
    Initialization &I;
    Address SrcAddr;

    InitPatternFromAddress(IRGenFunction &IGF, Initialization &I, Address addr)
      : IGF(IGF), I(I), SrcAddr(addr) {}

    void visitAnyPattern(AnyPattern *P) {
      // No need to copy anything out.
    }

    void visitNamedPattern(NamedPattern *P) {
      VarDecl *var = P->getDecl();

      const TypeInfo &fieldTI = IGF.getFragileTypeInfo(var->getType());
      Address destAddr = I.emitVariable(IGF, var, fieldTI);
      fieldTI.initializeWithCopy(IGF, destAddr, SrcAddr);

      // The validity of marking this after the initialization comes from
      // the assumption that initializeWithCopy is atomic w.r.t.
      // exceptions and control flow.
      I.markInitialized(IGF, I.getObjectForDecl(var));
    }

    void visitTuplePattern(TuplePattern *P) {
      visitTuplePattern(P, getAsTupleTypeInfo(IGF, P->getType()));
    }

    void visitTuplePattern(TuplePattern *P, const TupleTypeInfo &tupleTI) {
      Address srcTupleAddr = SrcAddr;
      for (unsigned i = 0, e = P->getNumFields(); i != e; ++i) {
        auto &field = tupleTI.getFields()[i];
        if (field.isEmpty()) continue;

        // Get the element pattern, skipping obviously ignored ones.
        Pattern *fieldP =
          P->getFields()[i].getPattern()->getSemanticsProvidingPattern();
        if (isa<AnyPattern>(fieldP)) continue;

        // Otherwise, change the source address and recurse on each field.
        SrcAddr = field.projectAddress(IGF, srcTupleAddr);
        visit(fieldP);
      }
    }
  };
}

/// Emit an initializer for a tuple pattern.
void swift::irgen::emitTuplePatternInitFromAddress(IRGenFunction &IGF,
                                                   Initialization &I,
                                                   Address addr,
                                                   TuplePattern *P,
                                                   const TypeInfo &TI) {
  const TupleTypeInfo &tupleTI = getAsTupleTypeInfo(TI);

  // If we can emit the initializer as an address, we can project
  // and copy directly.
  InitPatternFromAddress(IGF, I, addr).visitTuplePattern(P, tupleTI);
}
