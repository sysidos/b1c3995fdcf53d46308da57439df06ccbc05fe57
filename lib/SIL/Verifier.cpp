//===--- Verifier.cpp - Verification of Swift SIL Code --------------------===//
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

#include "swift/SIL/Function.h"
#include "swift/SIL/SILVisitor.h"
#include "swift/AST/Types.h"
using namespace swift;

namespace {
/// SILVerifier class - This class implements the SIL verifier, which walks over
/// SIL, checking and enforcing its invariants.
class SILVerifier : public SILVisitor<SILVerifier> {
public:
  
  void visit(Instruction *I) {
    
    const BasicBlock *BB = I->getParent();
    (void)BB;
    // Check that non-terminators look ok.
    if (!isa<TermInst>(I)) {
      assert(!BB->empty() &&
             "Can't be in a parent block if it is empty");
      assert(&*BB->getInsts().rbegin() != I &&
             "Non-terminators cannot be the last in a block");
    } else {
      assert(&*BB->getInsts().rbegin() == I &&
             "Terminator must be the last in block");
    }

    
    // Dispatch to our more-specialized instances below.
    ((SILVisitor<SILVerifier>*)this)->visit(I);
  }

  void visitAllocInst(AllocInst *AI) {
    assert(AI->getType()->is<LValueType>() &&"Allocation should return lvalue");
  }

  void visitAllocVarInst(AllocVarInst *AI) {
    visitAllocInst(AI);
  }

  void visitAllocTmpInst(AllocTmpInst *AI) {
    visitAllocInst(AI);
  }


  void visitApplyInst(ApplyInst *AI) {
    assert(AI->getCallee()->getType()->is<FunctionType>() &&
           "Callee of ApplyInst should have function type");
    FunctionType *FT = AI->getCallee()->getType()->castTo<FunctionType>();
    assert(AI->getType()->isEqual(FT->getResult()) &&
           "ApplyInst result type mismatch");


    // If there is a single argument to the apply, it might be a scalar or the
    // whole tuple being presented all at once.
    if (AI->getArguments().size() != 1 ||
        !AI->getArguments()[0]->getType()->isEqual(FT->getInput())) {
      // Otherwise, we must have a decomposed tuple.  Verify the arguments match
      // up.
      const TupleType *TT = FT->getInput()->castTo<TupleType>();
      (void)TT;
      assert(AI->getArguments().size() == TT->getFields().size() &&
             "ApplyInst contains unexpected argument count for function");
      for (unsigned i = 0, e = AI->getArguments().size(); i != e; ++i)
        assert(AI->getArguments()[i]->getType()
                 ->isEqual(TT->getFields()[i].getType()) &&
               "ApplyInst argument type mismatch");
    }
  }

  void visitConstantRefInst(ConstantRefInst *DRI) {
    assert(!DRI->getType()->is<LValueType>() &&
           "ConstantRef should return not produce an lvalue");
  }

  void visitIntegerLiteralInst(IntegerLiteralInst *ILI) {
    assert(ILI->getType()->is<BuiltinIntegerType>() &&
           "invalid integer literal type");
  }
  void visitLoadInst(LoadInst *LI) {
    assert(!LI->getType()->is<LValueType>() && "Load should produce rvalue");
    assert(LI->getLValue()->getType()->is<LValueType>() &&
           "Load op should be lvalue");
    assert(LI->getLValue()->getType()->getRValueType()->isEqual(LI->getType()) &&
           "Load operand type and result type mismatch");
  }

  void visitStoreInst(StoreInst *SI) {
    assert(!SI->getSrc()->getType()->is<LValueType>() &&
           "Src value should be rvalue");
    assert(SI->getDest()->getType()->is<LValueType>() &&
           "Dest address should be lvalue");
    assert(SI->getDest()->getType()->getRValueType()->
              isEqual(SI->getSrc()->getType()) &&
           "Store operand type and dest type mismatch");
  }

  void visitCopyInst(CopyInst *SI) {
    assert(SI->getSrc()->getType()->is<LValueType>() &&
           "Src value should be lvalue");
    assert(SI->getDest()->getType()->is<LValueType>() &&
           "Dest address should be lvalue");
    assert(SI->getDest()->getType()->getRValueType()->
           isEqual(SI->getSrc()->getType()->getRValueType()) &&
           "Store operand type and dest type mismatch");
  }
  
  void visitSpecializeInst(SpecializeInst *SI) {
    assert(SI->getType()->is<FunctionType>() &&
           SI->getOperand()->getType()->is<PolymorphicFunctionType>() &&
           "SpecializeInst only works on function types");
  }

  void visitTupleInst(TupleInst *TI) {
    assert(TI->getType()->is<TupleType>() && "TupleInst should return a tuple");
    TupleType *ResTy = TI->getType()->castTo<TupleType>(); (void)ResTy;

    assert(TI->getElements().size() == ResTy->getFields().size() &&
           "Tuple field count mismatch!");
  }
  void visitMetatypeInst(MetatypeInst *MI) {
  }
  
  void visitRetainInst(RetainInst *RI) {
    assert(!RI->getOperand()->getType()->is<LValueType>() &&
           "Operand of retain must not be lvalue");
  }
  void visitReleaseInst(ReleaseInst *RI) {
    assert(!RI->getOperand()->getType()->is<LValueType>() &&
           "Operand of release must not be lvalue");
  }
  void visitDeallocInst(DeallocInst *DI) {
    assert(DI->getOperand()->getType()->is<LValueType>() &&
           "Operand of dealloc must be lvalue");
  }
  void visitDestroyInst(DestroyInst *DI) {
    assert(DI->getOperand()->getType()->is<LValueType>() &&
           "Operand of destroy must be lvalue");
  }

  void visitIndexLValueInst(IndexLValueInst *ILI) {
    assert(ILI->getType()->is<LValueType>() &&
           ILI->getType()->isEqual(ILI->getOperand()->getType()) &&
           "invalid IndexLValueInst");
  }

  void visitIntegerValueInst(IntegerValueInst *IVI) {
    assert(IVI->getType()->is<BuiltinIntegerType>());
  }

  void visitReturnInst(ReturnInst *RI) {
    assert(RI->getReturnValue() && "Return of null value is invalid");
  }
  
  void visitBranchInst(BranchInst *BI) {
  }
  
  void visitCondBranchInst(CondBranchInst *CBI) {
    assert(CBI->getCondition() &&
           "Condition of conditional branch can't be missing");
  }
};
} // end anonymous namespace


/// verify - Run the IR verifier to make sure that the Function follows
/// invariants.
void Function::verify() const {
  SILVerifier().visitFunction(const_cast<Function*>(this));
}
