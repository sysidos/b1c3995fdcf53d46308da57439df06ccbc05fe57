//===--- TypeCheckProtocol.cpp - Protocol Checking ------------------------===//
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
// This file implements semantic analysis for protocols, in particular, checking
// whether a given type conforms to a given protocol.
//===----------------------------------------------------------------------===//

#include "TypeChecker.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/Decl.h"
#include "swift/AST/NameLookup.h"

using namespace swift;

static Type getInstanceUsageType(ValueDecl *Value, ASTContext &Context) {
  Type Ty = Value->getType();
  if (FuncDecl *Func = dyn_cast<FuncDecl>(Value)) {
    if (Func->getDeclContext()->isTypeContext()) {
      if (auto FuncTy = Func->getType()->getAs<AnyFunctionType>())
        return FuncTy->getResult()->getUnlabeledType(Context);
    }
  }
  
  return Ty->getUnlabeledType(Context);
}

/// \brief Retrieve the kind of requirement described by the given declaration,
/// for use in some diagnostics.
/// FIXME: Enumify this.
int getRequirementKind(ValueDecl *VD) {
  if (isa<FuncDecl>(VD))
    return 0;
  
  if (isa<VarDecl>(VD))
    return 1;
  
  assert(isa<SubscriptDecl>(VD) && "Unhandled requirement kind");
  return 2;
}

static bool valueMemberMatches(ValueDecl *Candidate, ValueDecl *Requirement,
                               Type RequiredTy, ASTContext &Context) {
  if (Candidate->getKind() != Requirement->getKind())
    return false;
  if (!RequiredTy->isEqual(getInstanceUsageType(Candidate, Context)))
    return false;
  if (FuncDecl *FD = dyn_cast<FuncDecl>(Candidate)) {
    if (FD->isStatic() != cast<FuncDecl>(Requirement)->isStatic())
      return false;
  }
  return true;
}

static std::unique_ptr<ProtocolConformance>
checkConformsToProtocol(TypeChecker &TC, Type T, ProtocolDecl *Proto,
                        SourceLoc ComplainLoc) {
  llvm::DenseMap<ValueDecl *, ValueDecl *> Mapping;
  TypeSubstitutionMap TypeMapping;
  llvm::DenseMap<ProtocolDecl *, ProtocolConformance *> InheritedMapping;

  // Check that T conforms to all inherited protocols.
  for (auto Inherited : Proto->getInherited()) {
    SmallVector<ProtocolDecl *, 4> InheritedProtos;
    if (!Inherited.getType()->isExistentialType(InheritedProtos))
      return nullptr;
    
    for (auto InheritedProto : InheritedProtos) {
      ProtocolConformance *InheritedConformance = nullptr;
      if (TC.conformsToProtocol(T, InheritedProto, &InheritedConformance,
                                ComplainLoc))
        InheritedMapping[InheritedProto] = InheritedConformance;
      else {
        // Recursive call already diagnosed this problem, but tack on a note
        // to establish the relationship.
        if (ComplainLoc.isValid()) {
          TC.diagnose(Proto->getStartLoc(),
                      diag::inherited_protocol_does_not_conform, T,
                      Inherited.getType());
        }
        return nullptr;
      }
    }
  }
  
  bool Complained = false;
  
  // First, resolve any associated type members. They'll be used for checking
  // other members.
  
  // FIXME: This algorithm is totally busted. We want to allow deduction of
  // associated type witnesses from the parameter/return types of other
  // witnesses, or to figure out an associated type based on one of its other
  // names, which means we need a rather more sophisticated algorithm.
  for (auto Member : Proto->getMembers()) {
    auto AssociatedType = dyn_cast<TypeAliasDecl>(Member);
    if (!AssociatedType)
      continue;
    
    // Bind the implicit 'This' type to the type T.
    // FIXME: Should have some kind of 'implicit' bit to detect this.
    if (AssociatedType->getName().str().equals("This")) {
      TypeMapping[AssociatedType->getUnderlyingType()->getAs<ArchetypeType>()]
        = T;
      continue;
    }
    
    MemberLookup Lookup(T, AssociatedType->getName(), TC.TU);
    if (Lookup.isSuccess()) {
      SmallVector<TypeDecl *, 2> Viable;
      SmallVector<std::pair<TypeDecl *, ProtocolDecl *>, 2> NonViable;

      for (auto Candidate : Lookup.Results) {
        switch (Candidate.Kind) {
        case MemberLookupResult::MetaArchetypeMember:
        case MemberLookupResult::MetatypeMember:
          if (auto TypeD = dyn_cast<TypeDecl>(Candidate.D)) {
            // Check this type against the protocol requirements.
            bool SatisfiesRequirements = true;

            Type T = TypeD->getDeclaredType();

            // FIXME: When this is a meta-archetype member (i.e., an associated
            // type), map down to the archetype it models.

            for (auto Req : AssociatedType->getInherited()) {
              SmallVector<ProtocolDecl *, 4> ReqProtos;
              if (!Req.getType()->isExistentialType(ReqProtos))
                return nullptr;

              for (auto ReqProto : ReqProtos) {
                if (!TC.conformsToProtocol(T, ReqProto)) {
                  SatisfiesRequirements = false;

                  NonViable.push_back({TypeD, ReqProto});
                  break;
                }
              }

              if (!SatisfiesRequirements)
                break;
            }

            if (SatisfiesRequirements)
              Viable.push_back(TypeD);
          }
          break;

        case MemberLookupResult::MemberProperty:
        case MemberLookupResult::MemberFunction:
        case MemberLookupResult::ExistentialMember:
        case MemberLookupResult::ArchetypeMember:
          // These results cannot satisfy type requirements.
          break;
        }
      }
      
      if (Viable.size() == 1) {
        TypeMapping[AssociatedType->getUnderlyingType()->getAs<ArchetypeType>()]
          = Viable.front()->getDeclaredType();
        continue;
      }
      
      if (ComplainLoc.isInvalid())
        return nullptr;
      
      if (!Viable.empty()) {
        if (!Complained) {
          TC.diagnose(ComplainLoc, diag::type_does_not_conform,
                      T, Proto->getDeclaredType());
          Complained = true;
        }
        
        TC.diagnose(AssociatedType->getStartLoc(),
                    diag::ambiguous_witnesses_type,
                    AssociatedType->getName());
        
        for (auto Candidate : Viable)
          TC.diagnose(Candidate->getStartLoc(), diag::protocol_witness_type);
        
        TypeMapping[AssociatedType->getUnderlyingType()->getAs<ArchetypeType>()]
          = ErrorType::get(TC.Context);
        continue;
      }

      if (!NonViable.empty()) {
        if (!Complained) {
          TC.diagnose(ComplainLoc, diag::type_does_not_conform,
                      T, Proto->getDeclaredType());
          Complained = true;
        }

        TC.diagnose(AssociatedType->getLoc(), diag::no_witnesses_type,
                    AssociatedType->getName());

        for (auto Candidate : NonViable) {
          TC.diagnose(Candidate.first->getLoc(),
                      diag::protocol_witness_nonconform_type,
                      Candidate.first->getDeclaredType(),
                      Candidate.second->getDeclaredType());
        }

        TypeMapping[AssociatedType->getUnderlyingType()->getAs<ArchetypeType>()]
          = ErrorType::get(TC.Context);
        continue;
      }
    }
    
    if (ComplainLoc.isValid()) {
      if (!Complained) {
        TC.diagnose(ComplainLoc, diag::type_does_not_conform,
                    T, Proto->getDeclaredType());
        Complained = true;
      }
      
      TC.diagnose(AssociatedType->getLoc(), diag::no_witnesses_type,
                  AssociatedType->getName());
      for (auto Candidate : Lookup.Results)
        TC.diagnose(Candidate.D->getStartLoc(), diag::protocol_witness_type);
      
      TypeMapping[AssociatedType->getUnderlyingType()->getAs<ArchetypeType>()]
        = ErrorType::get(TC.Context);
    } else {
      return nullptr;
    }
  }

  // If we complain about any associated types, there is no point in continuing.
  if (Complained)
    return nullptr;

  // Check that T provides all of the required func/variable/subscript members.
  for (auto Member : Proto->getMembers()) {
    auto Requirement = dyn_cast<ValueDecl>(Member);
    if (!Requirement)
      continue;

    // Associated type requirements handled above.
    if (isa<TypeAliasDecl>(Requirement))
      continue;

    // FIXME: This screams 'refactor me!', starting with collapsing the
    // lookup-results structures of member lookup and unqualified lookup into
    // a single structure.
    // Determine the type that we require the witness to have, substituting
    // in the witnesses we've collected for our archetypes.
    Type RequiredTy = TC.substType(getInstanceUsageType(Requirement,TC.Context),
                                   TypeMapping)->getUnlabeledType(TC.Context);
    TC.validateTypeSimple(RequiredTy);

    if (Requirement->getName().isOperator()) {
      // Operator lookup is always global.
      UnqualifiedLookup Lookup(Requirement->getName(), &TC.TU);

      if (Lookup.isSuccess()) {
        SmallVector<ValueDecl *, 2> Viable;
        for (auto Candidate : Lookup.Results) {
          switch (Candidate.Kind) {
          case UnqualifiedLookupResult::ModuleMember:
            if (valueMemberMatches(Candidate.getValueDecl(), Requirement,
                                   RequiredTy, TC.Context))
              Viable.push_back(Candidate.getValueDecl());
            break;

          case UnqualifiedLookupResult::ArchetypeMember:
          case UnqualifiedLookupResult::ExistentialMember:
          case UnqualifiedLookupResult::LocalDecl:
          case UnqualifiedLookupResult::MemberFunction:
          case UnqualifiedLookupResult::MemberProperty:
          case UnqualifiedLookupResult::MetaArchetypeMember:
          case UnqualifiedLookupResult::MetatypeMember:
          case UnqualifiedLookupResult::ModuleName:
            break;
          }
        }

        if (Viable.size() == 1) {
          Mapping[Requirement] = Viable.front();
          continue;
        }

        if (ComplainLoc.isInvalid())
          return nullptr;

        if (!Viable.empty()) {
          if (!Complained) {
            TC.diagnose(ComplainLoc, diag::type_does_not_conform,
                        T, Proto->getDeclaredType());
            Complained = true;
          }

          TC.diagnose(Requirement->getStartLoc(), diag::ambiguous_witnesses,
                      getRequirementKind(Requirement),
                      Requirement->getName(),
                      RequiredTy);

          for (auto Candidate : Viable)
            TC.diagnose(Candidate->getStartLoc(), diag::protocol_witness,
                        getInstanceUsageType(Candidate, TC.Context));
          
          continue;
        }
      }

      if (ComplainLoc.isValid()) {
        if (!Complained) {
          TC.diagnose(ComplainLoc, diag::type_does_not_conform,
                      T, Proto->getDeclaredType());
          Complained = true;
        }

        TC.diagnose(Requirement->getStartLoc(), diag::no_witnesses,
                    getRequirementKind(Requirement),
                    Requirement->getName(),
                    getInstanceUsageType(Requirement, TC.Context));
        for (auto Candidate : Lookup.Results)
          if (Candidate.hasValueDecl())
          TC.diagnose(Candidate.getValueDecl()->getStartLoc(),
                      diag::protocol_witness,
                      getInstanceUsageType(Candidate.getValueDecl(),
                                           TC.Context));
        continue;
      }

      return nullptr;
    }

    // Variable/function/subscript requirements.
    MemberLookup Lookup(T, Requirement->getName(), TC.TU);

    if (Lookup.isSuccess()) {
      SmallVector<ValueDecl *, 2> Viable;

      for (auto Candidate : Lookup.Results) {
        switch (Candidate.Kind) {
        case MemberLookupResult::MetatypeMember:
        case MemberLookupResult::MetaArchetypeMember:
        case MemberLookupResult::MemberProperty:
        case MemberLookupResult::MemberFunction:
        case MemberLookupResult::ExistentialMember:
          // FIXME: Handle substitution for meta-archetype members.
          if (valueMemberMatches(Candidate.D, Requirement, RequiredTy,
                                 TC.Context))
            Viable.push_back(Candidate.D);
          break;

        case MemberLookupResult::ArchetypeMember: {
          if (Candidate.D->getKind() != Requirement->getKind())
            break;

          // Determine the effective type of the candidate.
          Type CandidateTy = getInstanceUsageType(Candidate.D, TC.Context);
          CandidateTy = TC.substMemberTypeWithBase(CandidateTy, Candidate.D, T);
          if (!CandidateTy)
            break;

          if (RequiredTy->isEqual(CandidateTy))
            Viable.push_back(Candidate.D);
          break;
        }
        }
      }
      
      if (Viable.size() == 1) {
        Mapping[Requirement] = Viable.front();
        continue;
      }
      
      if (ComplainLoc.isInvalid())
        return nullptr;
      
      if (!Viable.empty()) {
        if (!Complained) {
          TC.diagnose(ComplainLoc, diag::type_does_not_conform,
                      T, Proto->getDeclaredType());
          Complained = true;
        }
        
        TC.diagnose(Requirement->getStartLoc(), diag::ambiguous_witnesses,
                    getRequirementKind(Requirement),
                    Requirement->getName(),
                    RequiredTy);
        
        for (auto Candidate : Viable)
          TC.diagnose(Candidate->getStartLoc(), diag::protocol_witness,
                      getInstanceUsageType(Candidate, TC.Context));
        
        continue;
      }
    }
    
    if (ComplainLoc.isValid()) {
      if (!Complained) {
        TC.diagnose(ComplainLoc, diag::type_does_not_conform,
                    T, Proto->getDeclaredType());
        Complained = true;
      }

      TC.diagnose(Requirement->getStartLoc(), diag::no_witnesses,
                  getRequirementKind(Requirement),
                  Requirement->getName(),
                  getInstanceUsageType(Requirement, TC.Context));
      for (auto Candidate : Lookup.Results)
        TC.diagnose(Candidate.D->getStartLoc(), diag::protocol_witness,
                    getInstanceUsageType(Candidate.D, TC.Context));
    } else {
      return nullptr;
    }
  }
  
  if (Complained)
    return nullptr;
  
  std::unique_ptr<ProtocolConformance> Result(new ProtocolConformance);
  // FIXME: Make DenseMap movable to make this efficient.
  Result->Mapping = std::move(Mapping);
  Result->TypeMapping = std::move(TypeMapping);
  Result->InheritedMapping = std::move(InheritedMapping);
  return Result;
}

bool TypeChecker::conformsToProtocol(Type T, ProtocolDecl *Proto,
                                     ProtocolConformance **Conformance,
                                     SourceLoc ComplainLoc) {
  if (Conformance)
    *Conformance = nullptr;

  // If we have an archetype, check whether this archetype's requirements
  // include this protocol (or something that inherits from it).
  if (auto Archetype = T->getAs<ArchetypeType>()) {
    for (auto AP : Archetype->getConformsTo()) {
      if (AP == Proto || AP->inheritsFrom(Proto))
        return true;
    }
  }

  // If we have an existential type, check whether this type includes this
  // protocol we're looking for (or something that inherits from it).
  {
    SmallVector<ProtocolDecl *, 4> AProtos;
    if (T->isExistentialType(AProtos)) {
      for (auto AP : AProtos) {
        if (AP == Proto || AP->inheritsFrom(Proto))
          return true;
      }
    }
  }

  ASTContext::ConformsToMap::key_type Key(T->getCanonicalType(), Proto);
  ASTContext::ConformsToMap::iterator Known = Context.ConformsTo.find(Key);
  if (Known != Context.ConformsTo.end()) {
    if (Conformance)
      *Conformance = Known->second;
    
    return Known->second != nullptr;
  }
  
  // Assume that the type does not conform to this protocol while checking
  // whether it does in fact conform. This eliminates both infinite recursion
  // (if the protocol hierarchies are circular) as well as tautologies.
  Context.ConformsTo[Key] = nullptr;
  if (std::unique_ptr<ProtocolConformance> ComputedConformance
        = checkConformsToProtocol(*this, T, Proto, ComplainLoc)) {
    auto result = ComputedConformance.release();
    Context.ConformsTo[Key] = result;

    if (Conformance)
      *Conformance = result;
    return true;
  }
  return nullptr;
}

bool TypeChecker::checkSubstitutions(TypeSubstitutionMap &Substitutions,
                                     ConformanceMap &Conformance,
                                     SourceLoc ComplainLoc,
                                     TypeSubstitutionMap *RecordSubstitutions) {
  llvm::SmallPtrSet<ArchetypeType *, 8> knownArchetypes;
  SmallVector<ArchetypeType *, 8> archetypeStack;

  // Find all of the primary archetypes and enter them into the archetype
  // stack.
  for (const auto &sub : Substitutions) {
    auto archetype = sub.first->getArchetype();
    if (archetype->isPrimary() && knownArchetypes.insert(archetype))
      archetypeStack.push_back(archetype);
  }

  // Check that each of the replacements for the archetypes conform
  // to the required protocols.
  while (!archetypeStack.empty()) {
    // Grab the last archetype on the stack.
    auto archetype = archetypeStack.back();
    archetypeStack.pop_back();

    // Substitute our deductions into the archetype type to produce the
    // concrete type we need to evaluate.
    Type T = substType(archetype, Substitutions);
    if (!T)
      return true;

    // If we were asked to record the substitution, do so now.
    if (RecordSubstitutions)
      (*RecordSubstitutions)[archetype] = T;

    SmallVectorImpl<ProtocolConformance *> &Conformances
      = Conformance[archetype];
    if (Conformances.empty()) {
      for (auto Proto : archetype->getConformsTo()) {
        ProtocolConformance *Conformance = nullptr;
        if (conformsToProtocol(T, Proto, &Conformance, ComplainLoc)) {
          Conformances.push_back(Conformance);
        } else {
          // FIXME: Diagnose, if requested.
          return true;
        }
      }
    }

    validateTypeSimple(T);

    // Add any nested archetypes to the archetype stack.
    for (auto Nested : archetype->getNestedTypes()) {
      if (knownArchetypes.insert(Nested.second))
        archetypeStack.push_back(Nested.second);
    }
  }

  // FIXME: Check same-type constraints!
  
  return false; 

}

