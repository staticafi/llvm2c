#include <llvm/IR/Instruction.h>

#include "../core/Program.h"
#include "../core/Func.h"
#include "../core/Block.h"

#include "SimplifyingExprVisitor.h"

class PropagateTypesVisitor: public SimplifyingExprVisitor {
public:
    const llvm::Module* mdl;
    Program *prgrm;
    std::set<Expr*> exprsWithChangedType;
    std::set<std::string> varsReadyForTypeRefinement;
    PropagateTypesVisitor(const llvm::Module* mdl, Program* prgrm) : mdl(mdl), prgrm(prgrm) {}

protected:
    void visit(ExtractValueExpr& expr) override;
    void visit(AggregateElement& expr) override;
    void visit(LogicalOr& expr) override;
    void visit(LogicalAnd& expr) override;
    void visit(ArrowExpr& expr) override;
    void visit(ArrayElement& expr) override;
    void visit(StackAlloc& expr) override;
    void visit(GepExpr& expr) override;
    void visit(PointerShift& expr) override;
    void visit(AndExpr& expr) override;
    void visit(OrExpr& expr) override;
    void visit(XorExpr& expr) override;
    void visit(AshrExpr& expr) override;
    void visit(LshrExpr& expr) override;
    void visit(ShlExpr& expr) override;
    void visit(AddExpr& expr) override;
    void visit(SubExpr& expr) override;
    void visit(MulExpr& expr) override;
    void visit(DivExpr& expr) override;
    void visit(RemExpr& expr) override;
    void visit(LogicalNot& expr) override;
    void visit(MinusExpr& expr) override;
    void visit(RetExpr& expr) override;
    void visit(DerefExpr& expr) override;
    void visit(RefExpr& expr) override;
    void visit(DoWhile& expr) override;
    void visit(IfExpr& expr) override;
    void visit(AssignExpr& expr) override;
    void visit(SelectExpr& expr) override;
    Expr* simplify(Expr* expr) override;
};

void PropagateTypesVisitor::visit(ExtractValueExpr& expr) {
    for (auto& index : expr.indices) {
        index->accept(*this);
    }
    expr.setType(expr.indices.back()->getType());
}

void PropagateTypesVisitor::visit(AggregateElement& ae) {
    ae.expr->accept(*this);

    ae.setType(llvm::dyn_cast<AggregateType>(ae.expr->getType())->items[ae.element].first);
}


void PropagateTypesVisitor::visit(ShlExpr& expr) {
    expr.left->accept(*this);
    expr.right->accept(*this);
    expr.setType(expr.left->getType() ? expr.left->getType() : expr.right->getType());
}

void PropagateTypesVisitor::visit(ArrowExpr& expr) {
    
    expr.expr->accept(*this);

    auto* ty = llvm::dyn_cast<PointerType>(expr.expr->getType());
    assert(ty);
    auto* ag = llvm::dyn_cast<StructType>(ty->type);
    assert(ag);

    expr.setType(ag->items[expr.element].first);
    } 


void PropagateTypesVisitor::visit(ArrayElement& ae) {
    ae.expr->accept(*this);
    ae.element->accept(*this);

    ae.setType(static_cast<ArrayType*>(ae.expr->getType())->type);
}

void PropagateTypesVisitor::visit(StackAlloc& expr) {
    expr.setType(expr.value->getType());
}

void PropagateTypesVisitor::visit(GepExpr& expr) {
    for (auto& index : expr.indices) {
        index->accept(*this);
    }
    expr.setType(expr.indices[expr.indices.size() - 1]->getType());
}

void PropagateTypesVisitor::visit(PointerShift& expr) {
    expr.pointer->accept(*this);
    expr.move->accept(*this);
    
    // Get the pointer type from the base pointer expression
    auto basePointerType = llvm::dyn_cast<PointerType>(expr.pointer->getType());
    if (basePointerType) {
        // Update both the result type and the ptrType used for casting
        expr.setType(basePointerType->type);
        // Create a new pointer type that matches the base pointer's element type
        // This ensures that pointer arithmetic casts use the correct signedness
        expr.ptrType = prgrm->typeHandler.pointerTo(basePointerType->type);
    }
}

void PropagateTypesVisitor::visit(LogicalOr& expr) {
    expr.lhs->accept(*this);
    expr.rhs->accept(*this);
    expr.setType(expr.lhs->getType() ? expr.lhs->getType() : expr.rhs->getType());
}

void PropagateTypesVisitor::visit(LogicalAnd& expr) {
    expr.lhs->accept(*this);
    expr.rhs->accept(*this);
    expr.setType(expr.lhs->getType() ? expr.lhs->getType() : expr.rhs->getType());
}

void PropagateTypesVisitor::visit(RemExpr& expr) {
    expr.left->accept(*this);
    expr.right->accept(*this);
    expr.setType(expr.left->getType() ? expr.left->getType() : expr.right->getType());
    // Unlike add/sub/mul, signedness is the semantics of div/rem: keep the
    // opcode's (udiv/urem vs sdiv/srem) and make the result type agree, so
    // addSignCasts casts the operands instead of the operation being flipped.
    if(auto IT = llvm::dyn_cast_or_null<IntegerType>(expr.getType())) {
	    if (IT->unsignedType != expr.isUnsigned)
		    expr.setType(prgrm->typeHandler.toggleSignedness(IT));
    }
}

void PropagateTypesVisitor::visit(AshrExpr& expr) {
    expr.left->accept(*this);
    expr.right->accept(*this);
    expr.setType(expr.left->getType() ? expr.left->getType() : expr.right->getType());
}

void PropagateTypesVisitor::visit(LshrExpr& expr) {
    expr.left->accept(*this);
    expr.right->accept(*this);
    expr.setType(expr.left->getType() ? expr.left->getType() : expr.right->getType());
}

void PropagateTypesVisitor::visit(AndExpr& expr) {
    expr.left->accept(*this);
    expr.right->accept(*this);
    expr.setType(expr.left->getType() ? expr.left->getType() : expr.right->getType());
}

void PropagateTypesVisitor::visit(OrExpr& expr) {
    expr.left->accept(*this);
    expr.right->accept(*this);
    expr.setType(expr.left->getType() ? expr.left->getType() : expr.right->getType());
}

void PropagateTypesVisitor::visit(XorExpr& expr) {
    expr.left->accept(*this);
    expr.right->accept(*this);
    expr.setType(expr.left->getType() ? expr.left->getType() : expr.right->getType());
}

void PropagateTypesVisitor::visit(AddExpr& expr) {
    expr.left->accept(*this);
    expr.right->accept(*this);
    expr.setType(expr.left->getType() ? expr.left->getType() : expr.right->getType());
    // a wrapping op (no nsw) stays unsigned whatever its operands' types
    if(auto IT = llvm::dyn_cast_or_null<IntegerType>(expr.getType()); IT && !expr.wraps) {
	    expr.isUnsigned = IT->unsignedType;
    }
}
void PropagateTypesVisitor::visit(SubExpr& expr) {
    expr.left->accept(*this);
    expr.right->accept(*this);
    expr.setType(expr.left->getType() ? expr.left->getType() : expr.right->getType());
    // a wrapping op (no nsw) stays unsigned whatever its operands' types
    if(auto IT = llvm::dyn_cast_or_null<IntegerType>(expr.getType()); IT && !expr.wraps) {
	    expr.isUnsigned = IT->unsignedType;
    }
}
void PropagateTypesVisitor::visit(MulExpr& expr) {
    expr.left->accept(*this);
    expr.right->accept(*this);
    expr.setType(expr.left->getType() ? expr.left->getType() : expr.right->getType());
    // a wrapping op (no nsw) stays unsigned whatever its operands' types
    if(auto IT = llvm::dyn_cast_or_null<IntegerType>(expr.getType()); IT && !expr.wraps) {
	    expr.isUnsigned = IT->unsignedType;
    }
}
void PropagateTypesVisitor::visit(DivExpr& expr) {
    expr.left->accept(*this);
    expr.right->accept(*this);
    expr.setType(expr.left->getType() ? expr.left->getType() : expr.right->getType());
    // Unlike add/sub/mul, signedness is the semantics of div/rem: keep the
    // opcode's (udiv/urem vs sdiv/srem) and make the result type agree, so
    // addSignCasts casts the operands instead of the operation being flipped.
    if(auto IT = llvm::dyn_cast_or_null<IntegerType>(expr.getType())) {
	    if (IT->unsignedType != expr.isUnsigned)
		    expr.setType(prgrm->typeHandler.toggleSignedness(IT));
    }
}



void PropagateTypesVisitor::visit(LogicalNot& expr) {
    expr.expr->accept(*this);
    expr.setType(expr.expr->getType());
}

void propagateTypes(const llvm::Module* module, Program& program) {
    assert(program.isPassCompleted(PassType::CreateExpressions));

    PropagateTypesVisitor rcv(module, &program);

    for (const llvm::Function& func : module->functions()) {
        auto* function = program.getFunction(&func);
	if(function) {
                for (auto var : function->variables) {
                    if (std::find_if(function->parameters.begin(),
                                     function->parameters.end(), [&var](Value *param){return var->valueName == param->valueName;} )
                                         == function->parameters.end()) {
                        if ( !rcv.prgrm->metadatedVars.count(var->valueName)) {
                            rcv.varsReadyForTypeRefinement.insert(var->valueName);
                        }
                    }
                }
	}
        for (const auto& block : func) {
            auto* myBlock = function->getBlock(&block);
                for (auto it = myBlock->expressions.begin(); it != myBlock->expressions.end(); ++it) {
                    auto expr = *it;
                    expr->accept(rcv);
                }
        }
    }
    program.addPass(PassType::PropagateTypes);
}

void PropagateTypesVisitor::visit(IfExpr& expr) {
    if (expr.cmp) {
        expr.cmp->accept(*this);
    }
}

void PropagateTypesVisitor::visit(DoWhile& expr) {
    if (expr.cond) {
        expr.cond->accept(*this);
    }
    if (expr.body)
        expr.body->accept(*this);
}

void PropagateTypesVisitor::visit(DerefExpr& expr) {
    expr.expr->accept(*this);

    if ( auto pt = llvm::dyn_cast_or_null<PointerType>(expr.expr->getType()))
        expr.setType(pt->type);
}

void PropagateTypesVisitor::visit(RefExpr& expr) {
    expr.expr->accept(*this);
    expr.setType(this->prgrm->typeHandler.pointerTo(expr.expr->getType()));
}

void PropagateTypesVisitor::visit(RetExpr& expr) {
    if (expr.expr) {
        expr.expr->accept(*this);
        expr.setType(expr.expr->getType());
    }
}

void PropagateTypesVisitor::visit(MinusExpr& expr) {
    expr.expr->accept(*this);
    expr.setType(expr.expr->getType());
}

void PropagateTypesVisitor::visit(AssignExpr& expr) {
    expr.left->accept(*this);
    expr.right->accept(*this);


    if(!(expr.right->getKind() == Expr::EK_CallExpr)) {
        if (exprsWithChangedType.count(expr.left)) {
            //llvm::errs() << "Attempt to change already changed type for: "
            //             << expr.left->getType()->toString() << " to "
            //             << expr.right->getType()->toString() << " \n";
            return;
        }

        if (auto v = llvm::dyn_cast_or_null<Value>(expr.left)) {
            if (this->varsReadyForTypeRefinement.count(v->valueName)) {
                expr.left->setType(expr.right->getType());
                exprsWithChangedType.insert(expr.left);
	    }
        }
    }
}

void PropagateTypesVisitor::visit(SelectExpr& expr) {
    expr.left->accept(*this);
    expr.right->accept(*this);
    expr.comp->accept(*this);

    if(auto IntgrType = llvm::dyn_cast_or_null<IntegerType>(expr.getType())) {
        if(llvm::dyn_cast_or_null<IntegerType>(expr.left->getType())->unsignedType) {
            expr.setType(expr.right->getType()); return;
        }
    }
    expr.setType(expr.left->getType());
}

Expr* PropagateTypesVisitor::simplify(Expr* expr) { return expr;}
