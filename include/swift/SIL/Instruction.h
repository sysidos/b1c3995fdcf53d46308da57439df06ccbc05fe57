//===--- Instruction.h - Instructions for high-level SIL code ---*- C++ -*-===//
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
// This file defines the high-level Instruction class used for Swift SIL code.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SIL_INSTRUCTION_H
#define SWIFT_SIL_INSTRUCTION_H

#include "swift/Basic/LLVM.h"
#include "swift/SIL/SILBase.h"
#include "swift/SIL/SILLocation.h"
#include "swift/SIL/SILSuccessor.h"
#include "swift/SIL/Value.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/ilist.h"

namespace swift {

class ValueDecl;
class Type;
class Function;
class BasicBlock;
class ApplyExpr;
class AssignStmt;
class CharacterLiteralExpr;
class DeclRefExpr;
class FloatLiteralExpr;
class ImplicitConversionExpr;
class IntegerLiteralExpr;
class LoadExpr;
class MaterializeExpr;
class MetatypeExpr;
class ReturnStmt;
class ScalarToTupleExpr;
class SpecializeExpr;
class StringLiteralExpr;
class Stmt;
class TupleElementExpr;
class TupleExpr;
class TupleShuffleExpr;
class VarDecl;

/// This is the root class for all instructions that can be used as the contents
/// of a Swift BasicBlock.
class Instruction : public Value, public llvm::ilist_node<Instruction> {
  friend struct llvm::ilist_traits<Instruction>;

  /// A backreference to the containing basic block.  This is maintained by
  /// ilist_traits<Instruction>.
  BasicBlock *ParentBB;

  SILLocation Loc;

  friend struct llvm::ilist_sentinel_traits<Instruction>;
  Instruction() = delete;
  void operator=(const Instruction &) = delete;
  void operator delete(void *Ptr, size_t) = delete;

protected:
  Instruction(ValueKind Kind, SILLocation Loc, Type Ty)
    : Value(Kind, Ty), ParentBB(0), Loc(Loc) {}

public:

  const BasicBlock *getParent() const { return ParentBB; }
  BasicBlock *getParent() { return ParentBB; }

  SILLocation getLoc() const { return Loc; }

  /// Return the AST expression that this instruction is produced from, or null
  /// if it is implicitly generated.  Note that this is aborts on locations that
  /// come from statements.
  template<typename T>
  T *getLocDecl() const { return cast_or_null<T>(Loc.template get<Decl*>()); }

  /// Return the AST expression that this instruction is produced from, or null
  /// if it is implicitly generated.  Note that this is aborts on locations that
  /// come from statements.
  template<typename T>
  T *getLocExpr() const { return cast_or_null<T>(Loc.template get<Expr*>()); }

  /// Return the AST statement that this instruction is produced from, or null
  /// if it is implicitly generated.  Note that this is aborts on locations that
  /// come from statements.
  template<typename T>
  T *getLocStmt() const { return cast_or_null<T>(Loc.template get<Stmt*>()); }


  /// removeFromParent - This method unlinks 'this' from the containing basic
  /// block, but does not delete it.
  ///
  void removeFromParent();
  
  /// eraseFromParent - This method unlinks 'this' from the containing basic
  /// block and deletes it.
  ///
  void eraseFromParent();

  static bool classof(const Value *I) {
    return I->getKind() >= ValueKind::First_Instruction &&
           I->getKind() <= ValueKind::Last_Instruction;
  }
};


/// AllocInst - This is the abstract base class common among all the memory
/// allocation mechanisms.  This can allocate heap or stack memory.
class AllocInst : public Instruction {
// Eventually: enum AllocKind { Heap, Stack, StackNoRefCount, Pseudo };

protected:
  AllocInst(ValueKind Kind, SILLocation Loc, Type Ty)
    : Instruction(Kind, Loc, Ty) {}
public:

  static bool classof(const Value *I) {
    return I->getKind() >= ValueKind::First_AllocInst &&
           I->getKind() <= ValueKind::Last_AllocInst;
  }
};


/// AllocVarInst - This represents the allocation of a local variable due to a
/// 'var' declaration.  A single var declaration may allocate multiple different
/// SIL allocations at once through its pattern.  One of these will be created
/// for each variable in something like "var (x,y) : (Int, Int)".
class AllocVarInst : public AllocInst {
public:
  AllocVarInst(VarDecl *VD);

  /// getDecl - Return the underlying declaration.
  VarDecl *getDecl() const;

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::AllocVarInst;
  }
};

/// AllocTmpInst - This represents the allocation of a temporary variable due to
/// a MaterializeExpr.  This occurs when an rvalue needs to be converted to an
/// l-value, for example to be the receiver of a dot-syntax method call.
///
/// The initial value for the temp will be provided by an initalization-style
/// store to the temporary.
class AllocTmpInst : public AllocInst {
public:

  AllocTmpInst(MaterializeExpr *E);

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::AllocTmpInst;
  }
};

/// AllocArrayInst - This represents the allocation of an array of elements,
/// whose element memory is left uninitialized.  This returns a value of tuple
/// type.  The first return element is the object pointer (pointer to the object
/// header) with Builtin.ObjectPointer type.  The second element returned is an
/// lvalue to the first array element.
///
class AllocArrayInst : public Instruction {
  Type ElementType;
  Value *NumElements;
public:

  AllocArrayInst(Expr *E, Type ElementType, Value *NumElements);

  Type getElementType() const { return ElementType; }
  Value *getNumElements() const { return NumElements; }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::AllocArrayInst;
  }
};


/// ApplyInst - Represents application of an argument to a function.
class ApplyInst : public Instruction {
  /// The instruction representing the called function.
  Value *Callee;

  unsigned NumArgs;
  Value **getArgsStorage() { return reinterpret_cast<Value**>(this + 1); }
  
  /// Construct an ApplyInst from a given call expression and the provided
  /// arguments.
  ApplyInst(SILLocation Loc, Type Ty, Value *Callee, ArrayRef<Value*> Args);

public:
  static ApplyInst *create(ApplyExpr *Expr, Value *Callee,
                           ArrayRef<Value*> Args, Function &F);
  static ApplyInst *create(Value *Callee, ArrayRef<Value*> Args, Function &F);

  
  Value *getCallee() { return Callee; }
  
  /// The arguments passed to this ApplyInst.
  MutableArrayRef<Value*> getArguments() {
    return MutableArrayRef<Value*>(getArgsStorage(), NumArgs);
  }

  /// The arguments passed to this ApplyInst.
  ArrayRef<Value*> getArguments() const {
    return const_cast<ApplyInst*>(this)->getArguments();
  }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::ApplyInst;
  }
};

/// ConstantRefInst - Represents a reference to a *constant* declaration,
/// evaluating to its value.
class ConstantRefInst : public Instruction {
public:

  /// Construct a ConstantRefInst.
  ///
  /// \param Expr A backpointer to the original DeclRefExpr.
  ///
  ConstantRefInst(DeclRefExpr *E);

  DeclRefExpr *getExpr() const;

  /// getDecl - Return the underlying declaration.
  ValueDecl *getDecl() const;

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::ConstantRefInst;
  }
};

/// A default "zero" value used to initialize a variable that was not otherwise
/// explicitly initialized.
class ZeroValueInst : public Instruction {
public:
  ZeroValueInst(VarDecl *D);

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::ZeroValueInst;
  }
};

/// IntegerLiteralInst - Encapsulates an integer constant, as defined originally
/// by an an IntegerLiteralExpr.
class IntegerLiteralInst : public Instruction {
public:
  IntegerLiteralInst(IntegerLiteralExpr *E);
  
  IntegerLiteralExpr *getExpr() const;
  
  /// getValue - Return the APInt for the underlying integer literal.
  APInt getValue() const;

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::IntegerLiteralInst;
  }
};

/// FloatLiteralInst - Encapsulates a floating point constant, as defined
/// originally by a FloatLiteralExpr.
class FloatLiteralInst : public Instruction {
public:
  FloatLiteralInst(FloatLiteralExpr *E);

  FloatLiteralExpr *getExpr() const;

  /// getValue - Return the APFloat for the underlying FP literal.
  APFloat getValue() const;

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::FloatLiteralInst;
  }
};

/// CharacterLiteralInst - Encapsulates a character constant, as defined
/// originally by a CharacterLiteralExpr.
class CharacterLiteralInst : public Instruction {
public:
  CharacterLiteralInst(CharacterLiteralExpr *E);

  CharacterLiteralExpr *getExpr() const;

  /// getValue - Return the value for the underlying literal.
  uint32_t getValue() const;

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::CharacterLiteralInst;
  }
};

/// StringLiteralInst - Encapsulates a string constant, as defined originally by
/// a StringLiteralExpr.
class StringLiteralInst : public Instruction {
public:
  StringLiteralInst(StringLiteralExpr *E);

  StringLiteralExpr *getExpr() const;

  /// getValue - Return the string data for the literal.
  StringRef getValue() const;

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::StringLiteralInst;
  }
};


/// LoadInst - Represents a load from a memory location. A load can optionally
/// "take" ownership of the loaded value from the memory location, leaving the
/// memory uninitialized.
class LoadInst : public Instruction {
  /// The LValue (memory address) to use for the load.
  Value *LValue;
  
  /// IsTake - True if the result of the load instruction takes ownership of the
  /// value and deinitializes the lvalue.
  bool IsTake;
public:
  /// Constructs a LoadInst.
  ///
  /// \param Expr The backing LoadExpr in the AST.
  ///
  /// \param LValue The Value *representing the lvalue (address) to
  ///        use for the load.
  ///
  /// \param IsTake True if this load takes ownership of the value from the
  ///        lvalue.
  LoadInst(LoadExpr *E, Value *LValue, bool IsTake = false);

  Value *getLValue() const { return LValue; }
  
  bool isTake() const { return IsTake; }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::LoadInst;
  }
};

/// StoreInst - Represents a store from a memory location.  If the destination
/// is unitialized memory, this models an initialization of that memory
/// location.
class StoreInst : public Instruction {
  /// The value being stored and the lvalue being stored to.
  Value *Src, *Dest;

  /// IsInitialization - True if this is the initialization of a memory location
  /// that is uninitialized, not a general store.  In an initialization of an
  /// ARC'd pointer (for example), the old value is not released.
  bool IsInitialization;
public:

  StoreInst(AssignStmt *S, Value *Src, Value *Dest);
  StoreInst(VarDecl *VD, Value *Src, Value *Dest);
  StoreInst(MaterializeExpr *E, Value *Src, Value *Dest);
  StoreInst(Expr *E, bool isInitialization, Value *Src, Value *Dest);

  Value *getSrc() const { return Src; }
  Value *getDest() const { return Dest; }

  bool isInitialization() const { return IsInitialization; }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::StoreInst;
  }
};
  
/// CopyInst - Represents a copy from one memory location to another. This is
/// similar to:
///   %1 = load %src
///   store %1 -> %dest
/// but a copy instruction can be used with types that cannot be
/// loaded, such as resilient value types.
class CopyInst : public Instruction {
  /// Src - The lvalue being loaded from.
  Value *Src;
  /// Dest - The lvalue being stored to.
  Value *Dest;
  
  /// IsTakeOfSrc - True if ownership will be taken from the value at the source
  /// memory location.
  bool IsTakeOfSrc : 1;
  /// IsInitializationOfDest - True if this is the initialization of the
  /// uninitialized destination memory location.
  bool IsInitializationOfDest : 1;
  
public:
  CopyInst(Expr *E,
           Value *Src, Value *Dest,
           bool IsTakeOfSrc, bool IsInitializationOfDest);
  
  Value *getSrc() const { return Src; }
  Value *getDest() const { return Dest; }
  bool isTakeOfSrc() const { return IsTakeOfSrc; }
  bool isInitializationOfDest() const { return IsInitializationOfDest; }
  
  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::CopyInst;
  }
};

/// SpecializeInst - Specializes a reference to a generic entity by binding
/// each of its type parameters to a specific type.
///
/// This instruction takes an arbitrary value of generic type and returns a new
/// closure that takes concrete types for its archetypes.  This is commonly used
/// in call sequences to generic functions, but can occur in arbitrarily general
/// cases as well.
///
class SpecializeInst : public Instruction {
  /// The value being specialized.  It always has FunctionType.
  Value *Operand;
public:

  SpecializeInst(SpecializeExpr *SE, Value *Operand, Type DestTy);

  Value *getOperand() const { return Operand; }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::SpecializeInst;
  }
};


/// TypeConversionInst - Change the Type of some value without affecting how it
/// will codegen.
class TypeConversionInst : public Instruction {
  Value *Operand;
public:
  TypeConversionInst(ImplicitConversionExpr *E, Value *Operand);
  TypeConversionInst(Type Ty, Value *Operand);

  Value *getOperand() const { return Operand; }
  
  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::TypeConversionInst;
  }
};


/// TupleInst - Represents a constructed tuple.
class TupleInst : public Instruction {
  Value **getElementsStorage() {
    return reinterpret_cast<Value**>(this + 1);
  }
  unsigned NumArgs;

  /// Private constructor.  Because of the storage requirements of
  /// TupleInst, object creation goes through 'create()'.
  TupleInst(Expr *E, Type Ty, ArrayRef<Value*> Elements);
  static TupleInst *createImpl(Expr *E, Type Ty,
                               ArrayRef<Value*> Elements, Function &F);

public:
  /// The elements referenced by this TupleInst.
  MutableArrayRef<Value*> getElements() {
    return MutableArrayRef<Value*>(getElementsStorage(), NumArgs);
  }

  /// The elements referenced by this TupleInst.
  ArrayRef<Value*> getElements() const {
    return const_cast<TupleInst*>(this)->getElements();
  }

  /// Construct a TupleInst.  The two forms are used to ensure that these are
  /// only created for specific syntactic forms.
  static TupleInst *create(Expr *E, ArrayRef<Value*> Elements, Function &F) {
    return createImpl(E, Type(), Elements, F);
  }
  static TupleInst *create(Type Ty, ArrayRef<Value*> Elements, Function &F) {
    return createImpl(nullptr, Ty, Elements, F);
  }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::TupleInst;
  }
};

/// MetatypeInst - Represents the production of an instance of a given metatype.
///
/// FIXME: base!
class MetatypeInst : public Instruction {
public:

  /// Constructs a TypeOfInst.
  ///
  /// \param Expr A backpointer to the original MetatypeExpr.
  ///
  MetatypeInst(MetatypeExpr *E);

  MetatypeExpr *getExpr() const;

  /// getMetaType - Return the type of the metatype that this instruction
  /// returns.
  Type getMetaType() const;

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::MetatypeInst;
  }
};


/// TupleElementInst - Extract a numbered element out of a value of tuple type.
class TupleElementInst : public Instruction {
  Value *Operand;
  unsigned FieldNo;
public:
  TupleElementInst(TupleElementExpr *E, Value *Operand, unsigned FieldNo);
  TupleElementInst(Type ResultTy, Value *Operand, unsigned FieldNo);
  
  Value *getOperand() const { return Operand; }
  unsigned getFieldNo() const { return FieldNo; }
  
  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::TupleElementInst;
  }
};

/// RetainInst - Increase the retain count of a value.
class RetainInst : public Instruction {
  Value *Operand;
public:
  RetainInst(Expr *E, Value *Operand);
  
  Value *getOperand() const { return Operand; }
  
  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::RetainInst;
  }
};

/// ReleaseInst - Decrease the retain count of a value, and dealloc the value
/// if its retain count is zero.
class ReleaseInst : public Instruction {
  Value *Operand;
public:
  ReleaseInst(Expr *E, Value *Operand);
  
  Value *getOperand() const { return Operand; }
  
  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::ReleaseInst;
  }
};

/// DeallocInst - Dealloc a value, releasing any resources it owns.
class DeallocInst : public Instruction {
  Value *Operand;
public:
  DeallocInst(Expr *E, Value *Operand);
  
  Value *getOperand() const { return Operand; }
  
  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::DeallocInst;
  }
};

/// DestroyInst - Destroy the value at a memory location and deallocate the
/// memory. This is similar to:
///   %1 = load %operand
///   release %1
///   dealloc %operand
/// but a destroy instruction can be used for types that cannot be loaded,
/// such as resilient value types.
class DestroyInst : public Instruction {
  Value *Operand;
public:
  DestroyInst(Expr *E, Value *Operand);
  
  Value *getOperand() const { return Operand; }
  
  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::ReleaseInst;
  }
};

//===----------------------------------------------------------------------===//
// SIL-only instructions that don't have an AST analog
//===----------------------------------------------------------------------===//


/// IndexLValueInst - "%1 = index_lvalue %0, 42"
/// This takes an lvalue and indexes over the pointer, striding by the type of
/// the lvalue.  This is used to index into arrays of uniform elements.
class IndexLValueInst : public Instruction {
  Value *Operand;
  unsigned Index;
public:
  IndexLValueInst(Expr *E, Value *Operand, unsigned Index);

  Value *getOperand() const { return Operand; }
  unsigned getIndex() const { return Index; }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::IndexLValueInst;
  }
};

/// IntegerValueInst - Always produces an integer of the specified value.  These
/// always have Builtin.Integer type.
class IntegerValueInst : public Instruction {
  uint64_t Val;
public:
  IntegerValueInst(uint64_t Val, Type Ty);

  uint64_t getValue() const { return Val; }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::IntegerValueInst;
  }
};


//===----------------------------------------------------------------------===//
// Instructions representing terminators
//===----------------------------------------------------------------------===//

/// This class defines a "terminating instruction" for a BasicBlock.
class TermInst : public Instruction {
protected:
  TermInst(ValueKind K, SILLocation Loc, Type Ty) : Instruction(K, Loc, Ty) {}
public:

  typedef llvm::ArrayRef<SILSuccessor> SuccessorListTy;

  /// The successor basic blocks of this terminator.
  SuccessorListTy getSuccessors();

  /// The successor basic blocks of this terminator.
  const SuccessorListTy getSuccessors() const {
    return const_cast<TermInst*>(this)->getSuccessors();
  }

  static bool classof(const Value *I) {
    return I->getKind() >= ValueKind::First_TermInst &&
           I->getKind() <= ValueKind::Last_TermInst;
  }
};

/// UnreachableInst - Position in the code which would be undefined to reach.
/// These are always implicitly generated, e.g. when falling off the end of a
/// function or after a no-return function call.
class UnreachableInst : public TermInst {
public:
  UnreachableInst(Function &F);
  
  SuccessorListTy getSuccessors() {
    // No Successors.
    return SuccessorListTy();
  }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::UnreachableInst;
  }
};

/// ReturnInst - Representation of a ReturnStmt.
class ReturnInst : public TermInst {
  /// The value to be returned.  This is never null.
  Value *ReturnValue;
  
public:
  /// Constructs a ReturnInst representing an \b explicit return.
  ///
  /// \param returnStmt The backing return statement in the AST.
  ///
  /// \param returnValue The value to be returned.
  ///
  ReturnInst(ReturnStmt *S, Value *ReturnValue);

  Value *getReturnValue() const { return ReturnValue; }

  SuccessorListTy getSuccessors() {
    // No Successors.
    return SuccessorListTy();
  }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::ReturnInst;
  }
};

/// BranchInst - An unconditional branch.
class BranchInst : public TermInst {
  llvm::ArrayRef<Value*> Arguments;
  SILSuccessor DestBB;
public:
  typedef ArrayRef<Value*> ArgsTy;
  
  /// Construct an BranchInst that will branches to the specified block.
  BranchInst(BasicBlock *DestBB, Function &F);
  
  /// The jump target for the branch.
  BasicBlock *getDestBB() const { return DestBB; }

  SuccessorListTy getSuccessors() {
    return DestBB;
  }

  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::BranchInst;
  }
};

class CondBranchInst : public TermInst {
  /// The condition value used for the branch.
  Value *Condition;

  SILSuccessor DestBBs[2];
public:

  CondBranchInst(Stmt *TheStmt, Value *Condition,
                 BasicBlock *TrueBB, BasicBlock *FalseBB);

  Value *getCondition() const { return Condition; }

  SuccessorListTy getSuccessors() {
    return DestBBs;
  }
  
  BasicBlock *getTrueBB() { return DestBBs[0]; }
  const BasicBlock *getTrueBB() const { return DestBBs[0]; }
  BasicBlock *getFalseBB() { return DestBBs[1]; }
  const BasicBlock *getFalseBB() const { return DestBBs[1]; }
  
  void setTrueBB(BasicBlock *BB) { DestBBs[0] = BB; }
  void setFalseBB(BasicBlock *BB) { DestBBs[1] = BB; }
  
  static bool classof(const Value *I) {
    return I->getKind() == ValueKind::CondBranchInst;
  }
};

} // end swift namespace

//===----------------------------------------------------------------------===//
// ilist_traits for Instruction
//===----------------------------------------------------------------------===//

namespace llvm {

template <>
struct ilist_traits<::swift::Instruction> :
  public ilist_default_traits<::swift::Instruction> {
  typedef ::swift::Instruction Instruction;

private:
  mutable ilist_half_node<Instruction> Sentinel;

  swift::BasicBlock *getContainingBlock();

public:
  Instruction *createSentinel() const {
    return static_cast<Instruction*>(&Sentinel);
  }
  void destroySentinel(Instruction *) const {}

  Instruction *provideInitialHead() const { return createSentinel(); }
  Instruction *ensureHead(Instruction*) const { return createSentinel(); }
  static void noteHead(Instruction*, Instruction*) {}
  static void deleteNode(Instruction *V) {}

  void addNodeToList(Instruction *I);
  void removeNodeFromList(Instruction *I);
  void transferNodesFromList(ilist_traits<Instruction> &L2,
                             ilist_iterator<Instruction> first,
                             ilist_iterator<Instruction> last);

private:
  void createNode(const Instruction &);
};

} // end llvm namespace

#endif
