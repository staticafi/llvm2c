#include "../core/Program.h"
#include "../core/Func.h"
#include "../core/Block.h"
#include "constval.h"
#include "cfunc.h"
#include "compare.h"
#include <cstring>

#include <llvm/ADT/iterator_range.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/Support/Casting.h>
#include <llvm/IR/GetElementPtrTypeIterator.h>
#include <unordered_set>


static std::unordered_set<int> read_only = {
    llvm::Instruction::Add,
    llvm::Instruction::FAdd,
    llvm::Instruction::Sub,
    llvm::Instruction::FSub,
    llvm::Instruction::Mul,
    llvm::Instruction::FMul,
    llvm::Instruction::UDiv,
    llvm::Instruction::FDiv,
    llvm::Instruction::SDiv,
    llvm::Instruction::URem,
    llvm::Instruction::FRem,
    llvm::Instruction::SRem,
    llvm::Instruction::And,
    llvm::Instruction::Or,
    llvm::Instruction::Xor,
    llvm::Instruction::Load,
    llvm::Instruction::ICmp,
    llvm::Instruction::FCmp,
    llvm::Instruction::Shl,
    llvm::Instruction::LShr,
    llvm::Instruction::AShr,
    llvm::Instruction::SExt,
    llvm::Instruction::ZExt,
    llvm::Instruction::FPToSI,
    llvm::Instruction::SIToFP,
    llvm::Instruction::FPTrunc,
    llvm::Instruction::FPExt,
    llvm::Instruction::FPToUI,
    llvm::Instruction::UIToFP,
    llvm::Instruction::PtrToInt,
    llvm::Instruction::IntToPtr,
    llvm::Instruction::Trunc,
    llvm::Instruction::BitCast,
    llvm::Instruction::GetElementPtr,
    llvm::Instruction::ExtractValue,
    llvm::Instruction::PHI,
};

Expr* parseLLVMInstruction(const llvm::Instruction& ins, Program& program);
static void parseInlineASM(const llvm::Instruction& ins, Func* func, Block* block);

// FIXME: Remove this when LLVM 8 is the minimal version for LLVM2C!
static inline llvm::iterator_range<llvm::User::op_iterator> args_wrapper(llvm::CallInst *CI) {
    return make_range(CI->arg_begin(), CI->arg_end());
}

static inline llvm::iterator_range<llvm::User::const_op_iterator> args_wrapper(const llvm::CallInst *CI) {
    return make_range(CI->arg_begin(), CI->arg_end());
}

const llvm::Instruction *getNextNonDebugInstruction(const llvm::Instruction *ins) {
#if LLVM_VERSION_MAJOR > 6
    return ins->getNextNonDebugInstruction();
#else
    // copied from LLVM sources, adapted
    for (auto I = ins->getNextNode(); I; I = I->getNextNode())
        if (!llvm::isa<llvm::DbgInfoIntrinsic>(I))
          return I;
    return nullptr;
#endif // LLVM_VERSION_MAJOR > 6
}

static bool canInline(const llvm::Value* value) {
    if (llvm::isa<llvm::Constant>(value)) {
        return true;
    }

    if (const auto* ins = llvm::dyn_cast_or_null<llvm::Instruction>(value)) {
        if (ins->hasNUses(1)) {
            const auto user = *ins->user_begin();

            auto cur = getNextNonDebugInstruction(ins);
            while (cur) {
                if (cur == user) {
                    return true;
                }

                if (read_only.find(cur->getOpcode()) == read_only.end()) {
                    return false;
                }

                cur = getNextNonDebugInstruction(cur);
            }
        }
    }

    return false;
}

static void inlineOrCreateVariable(const llvm::Value* value, Expr* expr, Func* func, Block* block) {
    if (auto *valExpr = llvm::dyn_cast_or_null<Value>(expr)) {
        const std::string &txt = valExpr->valueName;
        if (!txt.empty() && std::isdigit(txt[0])) {
            func->program->addExpr(value, expr);   // inline literal
            return;
        }
    }

    if (canInline(value)) {
        func->program->addExpr(value, expr);
	//llvm::errs() << "INLINING\n";
	//value->print(llvm::errs());
	//llvm::errs() << "\n";
        return;
    }
	//llvm::errs() << "Createting varibale: \n";

    auto var = std::make_unique<Value>(func->getVarName(), expr->getType());
	//llvm::errs() << var->valueName << " from ";
	//value->print(llvm::errs());
	//llvm::errs() << " created! \n";
    auto assign = std::make_unique<AssignExpr>(var.get(), expr);
    auto alloca = std::make_unique<StackAlloc>(var.get());

    block->addExprAndOwnership(std::move(alloca));
    block->addExprAndOwnership(std::move(assign));

    func->program->addExpr(value, func->program->addOwnership(std::move(var)));
}

static void createFuncCallParam(const llvm::Use& param, Program& program) {
    if (llvm::PointerType* PT = llvm::dyn_cast_or_null<llvm::PointerType>(param->getType())) {
        if (llvm::isa<llvm::ConstantPointerNull>(param)) {
            program.exprMap[param] = createConstantValue(param, program);
        } else if (PT->getPointerElementType()->isFunctionTy() && !param->getName().empty()) {
            auto val = std::make_unique<Value>(param->getName().str(), program.typeHandler.voidType.get());
            program.exprMap[param] = program.addOwnership(std::move(val));
        } else {
            program.exprMap[param] = createConstantValue(param, program);
        }
    } else {
        program.exprMap[param] = createConstantValue(param, program);
    }
}

static Expr* parseExtractValueInstruction(const llvm::Instruction& ins, Program& program) {
    const llvm::ExtractValueInst* EVI = llvm::cast<const llvm::ExtractValueInst>(&ins);

    std::vector<std::unique_ptr<Expr>> indices;
    Type* prevType = program.getType(ins.getOperand(0)->getType());
    Expr* expr = program.getExpr(ins.getOperand(0));

    if (llvm::isa<AsmExpr>(expr)) {
        return nullptr;
    }

    for (unsigned idx : EVI->getIndices()) {
        std::unique_ptr<Expr> element = nullptr;

        if (llvm::isa<StructType>(prevType))
            element = std::make_unique<AggregateElement>(expr, idx);

        if (llvm::dyn_cast_or_null<ArrayType>(prevType)) {
            auto newVal = std::make_unique<Value>(std::to_string(idx), program.typeHandler.uint.get());
            element = std::make_unique<ArrayElement>(expr, newVal.get());
            program.addOwnership(std::move(newVal));
        }

        prevType = element->getType();
        expr = element.get();
        indices.push_back(std::move(element));
    }

    return program.makeExpr<ExtractValueExpr>(std::move(indices));
}

static void parseInsertValueInstruction(const llvm::Instruction& ins, Func* func, Block* block, Program& program) {
    const llvm::InsertValueInst* IVI = llvm::cast<const llvm::InsertValueInst>(&ins);

    // Result aggregate type
    Type* aggregateType = program.getType(IVI->getType());

    // Create a new temporary aggregate variable to hold the updated value
    auto* tempVar = static_cast<Value*>(program.makeExpr<Value>(func->getVarName(), aggregateType));
    auto* alloca = program.makeExpr<StackAlloc>(tempVar);
    block->addExpr(alloca);

    // Initialize from the source aggregate if it is not 'undef'
    Expr* srcAgg = program.getExpr(IVI->getAggregateOperand());
    if (!srcAgg && llvm::isa<llvm::Constant>(IVI->getAggregateOperand())) {
        srcAgg = createConstantValue(IVI->getAggregateOperand(), program);
        if (srcAgg) {
            program.addExpr(IVI->getAggregateOperand(), srcAgg);
        }
    }

    if (srcAgg && !llvm::isa<llvm::UndefValue>(IVI->getAggregateOperand())) {
        auto* initAssign = program.makeExpr<AssignExpr>(tempVar, srcAgg);
        block->addExpr(initAssign);
    }

    // Build an lvalue expression referencing the target field within the temp aggregate
    Expr* lhs = tempVar;
    Type* prevType = aggregateType;
    for (unsigned idx : IVI->getIndices()) {
        if (llvm::isa<StructType>(prevType)) {
            lhs = program.makeExpr<AggregateElement>(lhs, idx);
            prevType = lhs->getType();
            continue;
        }

        if (auto* arrTy = llvm::dyn_cast_or_null<ArrayType>(prevType)) {
            auto* indexVal = program.makeExpr<Value>(std::to_string(idx), program.typeHandler.uint.get());
            lhs = program.makeExpr<ArrayElement>(lhs, indexVal, arrTy->type);
            prevType = lhs->getType();
            continue;
        }

        // Unsupported aggregate kind in index path; bail out
        assert(false && "insertvalue: unsupported aggregate index kind");
        abort();
    }

    // Right-hand side value to insert
    Expr* rhs = program.getExpr(IVI->getInsertedValueOperand());
    if (!rhs && llvm::isa<llvm::Constant>(IVI->getInsertedValueOperand())) {
        rhs = createConstantValue(IVI->getInsertedValueOperand(), program);
        if (rhs) {
            program.addExpr(IVI->getInsertedValueOperand(), rhs);
        }
    }
    assert(rhs && "insertvalue: missing inserted value expression");

    // Perform the field assignment on the temporary aggregate
    auto* fieldAssign = program.makeExpr<AssignExpr>(lhs, rhs);
    block->addExpr(fieldAssign);

    // The result of insertvalue is the (updated) aggregate value
    program.addExpr(&ins, tempVar);
}

static void parseLandingPadInstruction(const llvm::Instruction& ins, Func* func, Block* block, Program& program) {
    // Create a local temporary of the landingpad result type and bind it to this instruction.
    Type* lpType = program.getType(ins.getType());
    auto* tempVar = static_cast<Value*>(program.makeExpr<Value>(func->getVarName(), lpType));
    auto* alloca = program.makeExpr<StackAlloc>(tempVar);
    block->addExpr(alloca);
    program.addExpr(&ins, tempVar);
}

static void parseResumeInstruction(const llvm::Instruction& ins, Func* func, Block* block, Program& program) {
    // Translate resume as terminating abort(), which is a conservative replacement under panic=abort.
    auto* call = program.makeExpr<CallExpr>(nullptr, "abort", std::vector<Expr*>{}, program.typeHandler.voidType.get());
    block->addExpr(call);
}

// Helper function to build isnan checks for floating point comparisons
// Implementation note: __isnan and __isnanf should be defined as:
//   int __isnan( double x ) { return x != x; }
//   int __isnanf( float x ) { return x != x; }
static std::unique_ptr<Expr> buildIsNan(Program& program, Expr* val) {
    if (val->getType() == program.typeHandler.floatType.get())
        return std::make_unique<CallExpr>(nullptr, "__isnanf", std::vector<Expr*>{val}, program.typeHandler.sint.get());
    return std::make_unique<CallExpr>(nullptr, "__isnan", std::vector<Expr*>{val}, program.typeHandler.sint.get());

}

static Expr* parseFCmpInstruction(const llvm::Instruction& ins, Program& program) {
    static std::unordered_set<llvm::CmpInst::Predicate> validPredicates = {
        llvm::CmpInst::ICMP_EQ,
        llvm::CmpInst::FCMP_OEQ,
        llvm::CmpInst::FCMP_UEQ,
        llvm::CmpInst::ICMP_NE,
        llvm::CmpInst::FCMP_ONE,
        llvm::CmpInst::FCMP_UNE,
        llvm::CmpInst::ICMP_UGT,
        llvm::CmpInst::ICMP_SGT,
        llvm::CmpInst::FCMP_UGT,
        llvm::CmpInst::FCMP_OGT,
        llvm::CmpInst::ICMP_UGE,
        llvm::CmpInst::ICMP_SGE,
        llvm::CmpInst::FCMP_OGE,
        llvm::CmpInst::FCMP_UGE,
        llvm::CmpInst::ICMP_ULT,
        llvm::CmpInst::ICMP_SLT,
        llvm::CmpInst::FCMP_OLT,
        llvm::CmpInst::FCMP_ULT,
        llvm::CmpInst::ICMP_ULE,
        llvm::CmpInst::ICMP_SLE,
        llvm::CmpInst::FCMP_OLE,
        llvm::CmpInst::FCMP_ULE,
    };

    Expr* val0 = program.getExpr(ins.getOperand(0));
    Expr* val1 = program.getExpr(ins.getOperand(1));
    assert(val0 && val1);

    auto cmpInst = llvm::cast<const llvm::CmpInst>(&ins);
    auto isNan0 = program.addOwnership(buildIsNan(program, val0));
    auto isNan1 = program.addOwnership(buildIsNan(program, val1));
    auto isOrderedExpr0 = program.makeExpr<LogicalNot>(isNan0);
    auto isOrderedExpr1 = program.makeExpr<LogicalNot>(isNan1);
    Expr* isAllOrdered = program.makeExpr<LogicalAnd>(isOrderedExpr0, isOrderedExpr1);

    // include CMath, because we need it for checking isNan
    program.hasCMath = true;

    assert(llvm::CmpInst::isFPPredicate(cmpInst->getPredicate()) && "expressions: parseFCmpInstruction received a CmpInst with non-FP predicate");
    switch(cmpInst->getPredicate()) {
    case llvm::CmpInst::FCMP_FALSE:
        return program.makeExpr<Value>("0", program.typeHandler.sint.get());
    case llvm::CmpInst::FCMP_TRUE:
        return program.makeExpr<Value>("1", program.typeHandler.sint.get());

    case llvm::CmpInst::FCMP_ORD:
        return isAllOrdered;
    case llvm::CmpInst::FCMP_UNO:
        return program.makeExpr<LogicalNot>(isAllOrdered);
    default:
        // just make sure it is one of comparison operators. otherwise, it is unhandled by llvm2c
        assert(validPredicates.find(cmpInst->getPredicate()) != validPredicates.end());
        break;
    }
    std::string pred = getComparePredicate(cmpInst);

    auto cmpExpr = program.makeExpr<CmpExpr>(val0, val1, pred, false);

    if (llvm::CmpInst::isOrdered(cmpInst->getPredicate())) {
        return program.makeExpr<LogicalAnd>(isAllOrdered, cmpExpr);
    } else if (llvm::CmpInst::isUnordered(cmpInst->getPredicate())) {
        auto isUnordered = program.makeExpr<LogicalNot>(isAllOrdered);
        return program.makeExpr<LogicalOr>(isUnordered, cmpExpr);
    }

    //cmpInst->print(llvm::errs(), true);
    assert(false && "parseFCmpInstruction: unknown compare predicate");
    abort(); // for release builds
}

static Expr* parseICmpInstruction(const llvm::Instruction& ins, Program& program) {
    Expr* val0 = program.getExpr(ins.getOperand(0));
    Expr* val1 = program.getExpr(ins.getOperand(1));
    assert(val0 && val1);

    auto cmpInst = llvm::cast<const llvm::CmpInst>(&ins);

    return program.makeExpr<CmpExpr>(val0, val1, getComparePredicate(cmpInst), isIntegerCompareUnsigned(cmpInst));
}


static Expr* parseStoreInstruction(const llvm::Instruction& ins, Program& program) {
    auto type = program.getType(ins.getOperand(0)->getType());
    if (llvm::dyn_cast_or_null<PointerType>(type)) {
        if (llvm::Function* function = llvm::dyn_cast_or_null<llvm::Function>(ins.getOperand(0))) {
            if (!program.getExpr(ins.getOperand(0))) {
                auto val0 = std::make_unique<Value>("&" + function->getName().str(), program.typeHandler.voidType.get());
                program.addExpr(ins.getOperand(0), val0.get());
                program.addOwnership(std::move(val0));
            }
        }
    }

    //skip stacksave
    if (llvm::CallInst* CI = llvm::dyn_cast_or_null<llvm::CallInst>(ins.getOperand(0))) {
        if (CI->getCalledFunction()) {
            if (CI->getCalledFunction()->getName().str().compare("llvm.stacksave") == 0) {
                // TODO: somehow skip parsing the instruction
                // but it should not be a problem since store is not operand of anything
                return nullptr;
            }
        }
    }

    llvm::Value* inst = ins.getOperand(0);
    bool isCast = false;
    //get rid of casts in case of asm output parameter
    while (llvm::CastInst* CI = llvm::dyn_cast_or_null<llvm::CastInst>(inst)) {
        inst = CI->getOperand(0);
        isCast = true;
    }

    if (isCast) {
        //inline asm with multiple outputs with casts
        if (llvm::ExtractValueInst* EVI = llvm::dyn_cast_or_null<llvm::ExtractValueInst>(inst)) {
            Expr* value = program.getExpr(ins.getOperand(1));
            assert(value);

            Expr* asmExpr = program.getExpr(EVI->getOperand(0));

            if (auto RE = llvm::dyn_cast_or_null<RefExpr>(value)) {
                value = RE->expr;
            }

            if (auto AE = llvm::dyn_cast_or_null<AsmExpr>(asmExpr)) {
                AE->addOutputExpr(value, EVI->getIndices()[0]);
                return nullptr;
            }
        }

        //inline asm with single output with cast
        if (AsmExpr* AE = llvm::dyn_cast_or_null<AsmExpr>(program.getExpr(inst))) {
            Expr* value = program.getExpr(ins.getOperand(1));
            assert(value);

            if (auto RE = llvm::dyn_cast_or_null<RefExpr>(value)) {
                value = RE->expr;
            }

            AE->addOutputExpr(value, 0);
            return nullptr;
        }
    }

    //inline asm with multiple outputs
    if (llvm::ExtractValueInst* EVI = llvm::dyn_cast_or_null<llvm::ExtractValueInst>(ins.getOperand(0))) {
        Expr* value = program.getExpr(ins.getOperand(1));
        assert(value);
        Expr* asmExpr = program.getExpr(EVI->getOperand(0));

        if (auto RE = llvm::dyn_cast_or_null<RefExpr>(value)) {
            value = RE->expr;
        }

        if (auto AE = llvm::dyn_cast_or_null<AsmExpr>(asmExpr)) {
            AE->addOutputExpr(value, EVI->getIndices()[0]);
            return nullptr;
        }
    }

    Expr* val0 = program.getExpr(ins.getOperand(0));
    Expr* val1 = program.getExpr(ins.getOperand(1));
    assert(val0 && val1);

    //storing to NULL
    if (val1->isZero()) {
        auto newCast = std::make_unique<CastExpr>(val1, program.getType(ins.getOperand(1)->getType()));
        val1 = newCast.get();
        program.addOwnership(std::move(newCast));
    }

    auto deref = std::make_unique<DerefExpr>(val1);

    //inline asm with single output
    if (auto AE = llvm::dyn_cast_or_null<AsmExpr>(val0)) {
        if (auto RE = llvm::dyn_cast_or_null<RefExpr>(val1)) {
            val1 = RE->expr;
        }
        AE->addOutputExpr(val1, 0);
        return nullptr;
    }
    // An aggregate copy of array type is emitted as memcpy by the writer
    // (C forbids array assignment); make sure <string.h> is pulled in even
    // when the module itself contains no memcpy call.
    if (llvm::dyn_cast_or_null<ArrayType>(type)) {
        program.hasString = true;
    }

    auto assign = std::make_unique<AssignExpr>(program.addOwnership(std::move(deref)), val0);

    return program.addOwnership(std::move(assign));
}

static Expr* parseLoadInstruction(const llvm::Instruction& ins, Program& program) {
    auto deref = std::make_unique<DerefExpr>(program.getExpr(ins.getOperand(0)));
    return program.addOwnership(std::move(deref));
}

static Expr* parseFreezeInstruction(const llvm::Instruction& ins, Program& program) {
    return program.getExpr(ins.getOperand(0));
}

static Expr* parseFnegInstruction(const llvm::Instruction& ins, Program& program) {
    auto mnsExpr = std::make_unique<MinusExpr>(program.getExpr(ins.getOperand(0)));
    return program.addOwnership(std::move(mnsExpr));
}

static Expr *toSigned(Expr *expr, Program& program) {
    auto IT = static_cast<IntegerType*>(expr->getType());
    auto *cast
        = program.makeExpr<CastExpr>(
            expr,
            program.typeHandler.setSigned(IT)
    );
    return cast;
}

static Expr *toUnsigned(Expr *expr, Program& program) {
    auto IT = static_cast<IntegerType*>(expr->getType());
    auto *cast
        = program.makeExpr<CastExpr>(
            expr,
            program.typeHandler.setUnsigned(IT)
    );
    return cast;
}

static Expr* parseBinaryInstruction(const llvm::Instruction& ins, Program& program) {
    using namespace llvm;
    auto* binOp = llvm::cast<const llvm::BinaryOperator>(&ins);

    Expr* val0 = program.getExpr(ins.getOperand(0));
    Expr* val1 = program.getExpr(ins.getOperand(1));
    assert(val0 && val1);

    // Check if this is an OverflowingBinaryOperator before calling hasNoSignedWrap()
    bool hasNSW = false;
    if (auto* overflowOp = llvm::dyn_cast<const llvm::OverflowingBinaryOperator>(&ins)) {
        hasNSW = overflowOp->hasNoSignedWrap();
    }

    if (hasNSW ||
        ins.getOpcode() == Instruction::SDiv ||
        ins.getOpcode() == Instruction::FDiv ||
        ins.getOpcode() == Instruction::SRem ||
        ins.getOpcode() == Instruction::FRem) {
        // signed operation
        val0 = toSigned(val0, program);
        val1 = toSigned(val1, program);
    }

    std::unique_ptr<Expr> expr;
    switch (ins.getOpcode()) {
    case llvm::Instruction::Add:
    case llvm::Instruction::FAdd:
        expr = std::make_unique<AddExpr>(val0, val1, !hasNSW);
        break;
    case llvm::Instruction::Sub:
    case llvm::Instruction::FSub:
        expr = std::make_unique<SubExpr>(val0, val1, !hasNSW);
        break;
    case llvm::Instruction::Mul:
    case llvm::Instruction::FMul:
        expr = std::make_unique<MulExpr>(val0, val1, !hasNSW);
        break;
    case llvm::Instruction::UDiv:
        expr = std::make_unique<DivExpr>(val0, val1, true);
        break;
    case llvm::Instruction::SDiv:
    case llvm::Instruction::FDiv:
        expr = std::make_unique<DivExpr>(val0, val1, false);
        break;
    case llvm::Instruction::URem:
        expr = std::make_unique<RemExpr>(val0, val1, true);
        break;
    case llvm::Instruction::SRem:
    case llvm::Instruction::FRem:
        expr = std::make_unique<RemExpr>(val0, val1, false);
        break;
    case llvm::Instruction::And:
        expr = std::make_unique<AndExpr>(val0, val1);
        break;
    case llvm::Instruction::Or:
        expr = std::make_unique<OrExpr>(val0, val1);
        break;
    case llvm::Instruction::Xor:
        // LLVM uses xor to create a negation -- detect the
        // cases and do the negation, because otherwise
        // the translation is wrong (xor on 8 bits is different
        // to xor on 1 bit)
        if (ins.getType()->isIntegerTy(1)) {
            if (auto *T = dyn_cast<ConstantInt>(ins.getOperand(0))) {
                if (T->isOne()) {
                    expr = std::make_unique<LogicalNot>(val1);
                    break;
                }
            } else if (auto *T = dyn_cast<ConstantInt>(ins.getOperand(1))) {
                if (T->isOne()) {
                    expr = std::make_unique<LogicalNot>(val0);
                    break;
                }
            }
        }
        expr = std::make_unique<XorExpr>(val0, val1);
        break;
    default:
        llvm::outs() << "Unsupported binary instruction: " << ins << "\n";
        throw std::invalid_argument("Unsupported binary instruction encountered!");
    }

    // If this is a floating point operation with float-typed result, wrap the expression in an
    // explicit (float) cast so comparisons match LLVM IR float semantics (no implicit double).
    if (ins.getType()->isFloatTy()) {
        auto* inner = program.addOwnership(std::move(expr));
        expr = std::make_unique<CastExpr>(inner, program.typeHandler.floatType.get());
    }

    return program.addOwnership(std::move(expr));
}

static Expr* createListOfOneGoto(Block* container, Block* gotoTarget) {
    auto gotoExpr = std::make_unique<GotoExpr>(gotoTarget);
    std::vector<Expr*> exprs { gotoExpr.get() };
    auto list = std::make_unique<ExprList>(std::move(exprs));

    container->addOwnership(std::move(gotoExpr));
    auto* result = list.get();
    container->addOwnership(std::move(list));
    return result;
}

static void parseSwitchInstruction(const llvm::Instruction& ins, bool isConstExpr, const llvm::Value* val, Func* func, Block* block) {
    std::map<int, Expr*> cases;

    Expr* cmp = func->getExpr(ins.getOperand(0));
    assert(cmp);

    Block* def = func->createBlockIfNotExist(llvm::cast<llvm::BasicBlock>(ins.getOperand(1)));
    const llvm::SwitchInst* switchIns = llvm::cast<llvm::SwitchInst>(&ins);

    for (auto& switchCase : switchIns->cases()) {
        Block *target = func->createBlockIfNotExist(switchCase.getCaseSuccessor());
        cases[switchCase.getCaseValue()->getSExtValue()] = createListOfOneGoto(block, target);
    }

    if (!isConstExpr) {
        func->createExpr(&ins, std::make_unique<SwitchExpr>(cmp, createListOfOneGoto(block, def), cases));
        block->addExpr(func->getExpr(&ins));
    } else {
        func->createExpr(val, std::make_unique<SwitchExpr>(cmp, createListOfOneGoto(block, def), cases));
    }
}

static void parseAsmInst(const llvm::Instruction& ins, Func* func, Block* block) {
    std::string inst;

    switch(ins.getOpcode()) {
    case llvm::Instruction::Unreachable:
        inst = "int3";
        break;
    case llvm::Instruction::Fence:
        inst = "fence";
        break;
    default:
        break;
    }

    func->createExpr(&ins, std::make_unique<AsmExpr>(inst, std::vector<std::pair<std::string, Expr*>>(), std::vector<std::pair<std::string, Expr*>>(), ""));
    block->addExpr(func->getExpr(&ins));
}

static Expr * removeCastsFromExpr(Expr* expr) {
    if (auto* cast = llvm::dyn_cast_or_null<CastExpr>(expr)) {
        //llvm::errs() << "cast removed\n";
        Expr* innermost = cast->expr;

        while (auto* inner = llvm::dyn_cast_or_null<CastExpr>(innermost)) {
            //llvm::errs() << "cast removed\n";
            innermost = inner->expr;
        }

        return innermost;
    }

    return expr;
}

static Expr* parseShiftInstruction(const llvm::Instruction& ins, Program& program) {
    Expr* val0 = program.getExpr(ins.getOperand(0));
    Expr* val1 = program.getExpr(ins.getOperand(1));
    assert(val0 && val1);

    auto* binOp = llvm::cast<const llvm::BinaryOperator>(&ins);
    
    // Check if this is an OverflowingBinaryOperator before calling hasNoSignedWrap()
    bool hasNSW = false;
    if (auto* overflowOp = llvm::dyn_cast<const llvm::OverflowingBinaryOperator>(&ins)) {
        hasNSW = overflowOp->hasNoSignedWrap();
    }

    std::unique_ptr<Expr> expr;
    switch (ins.getOpcode()) {
    case llvm::Instruction::Shl:
        if (hasNSW) {
            val0 = toSigned(val0, program);
            val1 = toSigned(val1, program);
        }

		//llvm::errs() << removeCastsFromExpr(val1)->getKind() << " end\n";
	if(llvm::dyn_cast_or_null<Value>(removeCastsFromExpr(val1)) &&
	   llvm::dyn_cast_or_null<Value>(removeCastsFromExpr(val1))->valueName == "1") {
		//llvm::errs() << removeCastsFromExpr(val1)->getKind() << "WASHER\n";
		auto two = std::make_unique<Value>("2", removeCastsFromExpr(val1)->getType());
		expr = std::make_unique<MulExpr>(val0,  two.get(),
					 !binOp->hasNoSignedWrap());
		program.addOwnership(std::move(two));
	    }
	else {
        expr = std::make_unique<ShlExpr>(val0, toUnsigned(val1, program),
                                         !binOp->hasNoSignedWrap());
	} break;
    case llvm::Instruction::LShr:
	if(ins.getType()->isIntegerTy(64) &&
	   llvm::dyn_cast_or_null<Value>(removeCastsFromExpr(val1)) &&
	   llvm::dyn_cast_or_null<Value>(removeCastsFromExpr(val1))->valueName == "63") {
           // create 0
	   auto zero = std::make_unique<Value>("0", val0->getType());
	   // create: val0 < 0 (signed: lshr x, 63 is the sign bit of x)
	   expr = std::make_unique<CmpExpr>(val0, zero.get(), "<", false);
	   program.addOwnership(std::move(zero));

   } else {
	expr = std::make_unique<LshrExpr>(val0, val1);
        } break;
    case llvm::Instruction::AShr:
        expr = std::make_unique<AshrExpr>(val0, toUnsigned(val1, program));
        break;
    }

    return program.addOwnership(std::move(expr));
}
std::string getRegisterString(const std::string& str) {
    std::string reg = str;
    std::string ret;

    if (reg[0] != '{') {
        return reg;
    }

    if (reg[2] == 'i') {
        ret += (char)std::toupper(reg[1]);
    }

    if (reg[2] == 'x') {
        ret += reg[1];
    }

    return ret;
}

std::vector<std::string> getAsmInputStrings(llvm::InlineAsm::ConstraintInfoVector info) {
    std::vector<std::string> ret;

    for (llvm::InlineAsm::ConstraintInfoVector::iterator iter = info.begin(); iter != info.end(); iter++) {
        if (iter->Type != llvm::InlineAsm::isInput) {
            continue;
        }

        std::string input;
        for (unsigned i = 0; i < iter->Codes.size(); i++) {
            input += getRegisterString(iter->Codes[i]);
        }

        if (iter->isEarlyClobber) {
            input = "&" + input;
        }

        if (iter->isIndirect) {
            input = "*" + input;
        }

        ret.push_back("\"" + input + "\"");
    }

    return ret;
}

std::string getAsmUsedRegString(llvm::InlineAsm::ConstraintInfoVector info) {
    static std::vector<std::string> CLOBBER = {"memory", "rax", "eax", "ax", "al", "rbx", "ebx", "bx", "bl", "rcx", "ecx", "cx",
                                               "cl", "rdx", "edx", "dx", "dl", "rsi", "esi", "si", "rdi", "edi", "di"};

    std::string ret;
    bool first = true;

    for (llvm::InlineAsm::ConstraintInfoVector::iterator iter = info.begin(); iter != info.end(); iter++) {
        if (iter->Type != llvm::InlineAsm::isClobber) {
            continue;
        }

        std::string clobber = iter->Codes[0];
        clobber = clobber.substr(1, clobber.size() - 2);

        if (std::find(CLOBBER.begin(), CLOBBER.end(), clobber) != CLOBBER.end()) {
            if (!first) {
                ret += ", ";
            }
            first = false;

            ret += "\"";
            if (clobber.compare("memory") != 0) {
                ret += "%";
            }
            ret += clobber + "\"";
        }
    }

    return ret;
}


std::vector<std::string> getAsmOutputStrings(llvm::InlineAsm::ConstraintInfoVector info) {
    std::vector<std::string> ret;

    for (llvm::InlineAsm::ConstraintInfoVector::iterator iter = info.begin(); iter != info.end(); iter++) {
        if (iter->Type != llvm::InlineAsm::isOutput) {
            continue;
        }

        std::string output;
        if (iter->isMultipleAlternative) {
            bool first = true;
            for (unsigned i = 0; i < iter->multipleAlternatives.size(); i++) {
                if (!first) {
                    output += ",";
                }

                if (iter->multipleAlternatives[i].Codes.size() > 1) {
                    output += iter->multipleAlternatives[i].Codes[0];
                    output += getRegisterString(iter->multipleAlternatives[i].Codes[1]);
                } else {
                    output += getRegisterString(iter->multipleAlternatives[i].Codes[0]);
                }

                first = false;
            }

            if (iter->isEarlyClobber) {
                output = "&" + output;
            }

            if (iter->isIndirect) {
                ret.push_back("\"=*" + output + "\"");
            } else {
                ret.push_back("\"=" + output + "\"");
            }
            continue;
        }

        if (iter->Codes.size() > 1) {
            output = iter->Codes[0];
            output += getRegisterString(iter->Codes[1]);
        } else {
            output = getRegisterString(iter->Codes[0]);
        }

        if (iter->isEarlyClobber) {
            output = "&" + output;
        }

        if (iter->isIndirect) {
            ret.push_back("\"=*" + output + "\"");
        } else {
            ret.push_back("\"=" + output + "\"");
        }
    }

    return ret;
}

static Type *deduceNondetReturnType(const std::string &name,
    TypeHandler &th) {
// Fast reject
if (name.rfind("__VERIFIER_nondet_", 0) != 0)
return nullptr;

if (name.compare(0, 22, "__VERIFIER_nondet_bool") == 0)
return th.uchar.get(); // _Bool is typically unsigned char
if (name.compare(0, 22, "__VERIFIER_nondet_char") == 0)
return th.schar.get();
if (name.compare(0, 23, "__VERIFIER_nondet_uchar") == 0)
return th.uchar.get();
if (name.compare(0, 23, "__VERIFIER_nondet_short") == 0)
return th.sshort.get();
if (name.compare(0, 24, "__VERIFIER_nondet_ushort") == 0)
return th.ushort.get();
if (name.compare(0, 21, "__VERIFIER_nondet_int") == 0)
return th.sint.get();
if (name.compare(0, 22, "__VERIFIER_nondet_uint") == 0)
return th.uint.get();
if (name.compare(0, 22, "__VERIFIER_nondet_long") == 0)
return th.slong.get();
if (name.compare(0, 23, "__VERIFIER_nondet_ulong") == 0)
return th.ulong.get();
if (name.compare(0, 25, "__VERIFIER_nondet_longlong") == 0)
return th.slonglong.get();
if (name.compare(0, 26, "__VERIFIER_nondet_ulonglong") == 0)
return th.ulonglong.get();
if (name.compare(0, 23, "__VERIFIER_nondet_uint128") == 0)
return th.int128.get();

return nullptr; // not found
}

static void parseCallInstruction(const llvm::Instruction& ins, Func* func, Block* block) {
    const llvm::Value* value = &ins;
    const llvm::CallInst* callInst = llvm::cast<llvm::CallInst>(&ins);
    Expr* funcValue = nullptr;
    std::string funcName;
    std::vector<Expr*> params;
    Type* type = nullptr;

    if (callInst->getCalledFunction()) {
        funcName = callInst->getCalledFunction()->getName().str();
        type = func->getType(callInst->getCalledFunction()->getReturnType());

        if (funcName == "llvm.dbg.declare" || funcName == "llvm.dbg.value" || funcName == "llvm.dbg.label") {
            return;
        }

        // skip lifetime markers
        if (funcName.find("llvm.lifetime.") == 0) {
            return;
        }

        if (funcName.compare("llvm.trap") == 0 || funcName.compare("llvm.debugtrap") == 0) {
            func->createExpr(&ins, std::make_unique<AsmExpr>("int3", std::vector<std::pair<std::string, Expr*>>(), std::vector<std::pair<std::string, Expr*>>(), ""));
            block->addExpr(func->getExpr(&ins));
            return;
        }

        if (funcName.compare("llvm.stacksave") == 0 || funcName.compare("llvm.stackrestore") == 0) {
            func->stackIgnored();
            return;
        }

        if (!funcName.substr(0,8).compare("llvm.abs")) {
            Expr* a = func->getExpr(ins.getOperand(0));
            auto zero = std::make_unique<Value>("0", type);
            std::unique_ptr<CmpExpr> cmprsn = std::make_unique<CmpExpr>(a, zero.get(), "<", false);
            auto mnsExpr = std::make_unique<MinusExpr>(a);
            auto slctExpr = std::make_unique<SelectExpr>(cmprsn.get(), mnsExpr.get() ,a);
            block->addOwnership(std::move(zero));
            block->addOwnership(std::move(cmprsn));
            block->addOwnership(std::move(mnsExpr));
            if (value->hasNUses(0)) {
                block->addExpr(slctExpr.get());
                func->createExpr(value, std::move(slctExpr));
            } else {
                inlineOrCreateVariable(value, func->program->addOwnership(std::move(slctExpr)), func, block);
            }
            return;
        }

        /*
        https://reviews.llvm.org/D9293?id=&download=true
        The expression::

            call i8 @llvm.umax.i8(i8 %a, i8 %b)

        is equivalent to::

            %1 = icmp ugt i8 %a, %b
            %2 = select i1 %1, i8 %a, i8 %b
        */
        if (!funcName.substr(0,9).compare("llvm.smax") || !funcName.substr(0,9).compare("llvm.smin") ||
            !funcName.substr(0,9).compare("llvm.umax") || !funcName.substr(0,9).compare("llvm.umin")) {
            Expr* a = func->getExpr(ins.getOperand(0));
            Expr* b = func->getExpr(ins.getOperand(1));
            assert(a && b);
            Expr* cmprsn_ptr = nullptr;
            if (!funcName.substr(0,9).compare("llvm.smin")) {
                std::unique_ptr<CmpExpr> cmprsn = std::make_unique<CmpExpr>(a, b, "<", false);
                cmprsn_ptr = cmprsn.get();
                block->addOwnership(std::move(cmprsn));
            } else if (!funcName.substr(0,9).compare("llvm.smax")) {
                std::unique_ptr<CmpExpr> cmprsn = std::make_unique<CmpExpr>(a, b, ">", false);
                cmprsn_ptr = cmprsn.get();
                block->addOwnership(std::move(cmprsn));
            } else if (!funcName.substr(0,9).compare("llvm.umin")) {
                std::unique_ptr<CmpExpr> cmprsn = std::make_unique<CmpExpr>(a, b, "<", true);
                cmprsn_ptr = cmprsn.get();
                block->addOwnership(std::move(cmprsn));
            } else if (!funcName.substr(0,9).compare("llvm.umax")) {
                std::unique_ptr<CmpExpr> cmprsn = std::make_unique<CmpExpr>(a, b, ">", true);
                cmprsn_ptr = cmprsn.get();
                block->addOwnership(std::move(cmprsn));
            }
            auto slctExpr = std::make_unique<SelectExpr>(cmprsn_ptr, a, b);
		    //llvm::errs() << "T: " << slctExpr->getType()->toString() << "\n";
		    //llvm::errs() << "Ta: " << a->getType()->toString() << "\n";
            if (value->hasNUses(0)) {
		    //llvm::errs() << "zeroUSES\n";
                block->addExpr(slctExpr.get());
                func->createExpr(value, std::move(slctExpr));
            } else {
		    //llvm::errs() << "NONzeroUSES\n";
                inlineOrCreateVariable(value, func->program->addOwnership(std::move(slctExpr)), func, block);
            }
            return;
        }

        // Handle llvm.ctpop.* intrinsics: convert to __builtin_popcount/__builtin_popcountl
        if (!funcName.substr(0,10).compare("llvm.ctpop")) {
            Expr* a = func->getExpr(ins.getOperand(0));
            assert(a);
            
            // Determine which builtin to use based on bit width
            std::string builtinName;
            if (funcName.find("i64") != std::string::npos || funcName.find("i128") != std::string::npos) {
                builtinName = "__builtin_popcountl";
            } else {
                builtinName = "__builtin_popcount";
            }
            
            // Create a CallExpr with the builtin name (nullptr for funcValue since it's a direct call)
            std::vector<Expr*> params = {a};
            auto callExpr = std::make_unique<CallExpr>(nullptr, builtinName, params, type);
            
            if (value->hasNUses(0)) {
                block->addExpr(callExpr.get());
                func->createExpr(value, std::move(callExpr));
            } else {
                inlineOrCreateVariable(value, func->program->addOwnership(std::move(callExpr)), func, block);
            }
            return;
        }

        if (!funcName.substr(0,11).compare("llvm.assume")) {
            return;
        }

        // Handle llvm.fmuladd.* intrinsics: a * b + c
        if (!funcName.substr(0,12).compare("llvm.fmuladd")) {
            Expr* a = func->getExpr(ins.getOperand(0));
            Expr* b = func->getExpr(ins.getOperand(1));
            Expr* c = func->getExpr(ins.getOperand(2));
            assert(a && b && c);
            
            // Create a * b
            auto mulExpr = std::make_unique<MulExpr>(a, b, true);
            // Create (a * b) + c
            std::unique_ptr<Expr> addExpr = std::make_unique<AddExpr>(mulExpr.get(), c, true);
            block->addOwnership(std::move(mulExpr));
                // For float result, wrap in explicit (float) cast to preserve float semantics
            if (ins.getType()->isFloatTy()) {
                auto* ownedAdd = func->program->addOwnership(std::move(addExpr));
                addExpr = std::make_unique<CastExpr>(ownedAdd, func->program->typeHandler.floatType.get());
            }
            
            if (value->hasNUses(0)) {
                block->addExpr(addExpr.get());
                func->createExpr(value, std::move(addExpr));
            } else {
                inlineOrCreateVariable(value, func->program->addOwnership(std::move(addExpr)), func, block);
            }
            return;
        }

        if (funcName.substr(0,4).compare("llvm") == 0) {
            // Check for overflow intrinsics that need definitions
            if (funcName.find("uadd.with.overflow") != std::string::npos ||
                funcName.find("usub.with.overflow") != std::string::npos ||
                funcName.find("umul.with.overflow") != std::string::npos ||
                funcName.find("sadd.with.overflow") != std::string::npos ||
                funcName.find("ssub.with.overflow") != std::string::npos ||
                funcName.find("smul.with.overflow") != std::string::npos) {
                // Mark for definition generation (use original name before transformation)
                func->program->markIntrinsicForDefinition(funcName);
            }
            
            if (isCFunc(trimPrefix(funcName))) {
                funcName = trimPrefix(funcName);
            } else {
                std::replace(funcName.begin(), funcName.end(), '.', '_');
            }
        }
    } else {
#if LLVM_VERSION_MAJOR >= 8
        const llvm::Value* operand = callInst->getCalledOperand();
#else
        const llvm::Value* operand = callInst->getCalledValue();
#endif
        llvm::PointerType* PT = llvm::cast<llvm::PointerType>(operand->getType());
        llvm::FunctionType* FT = llvm::cast<llvm::FunctionType>(PT->getPointerElementType());
        // Try to detect SV-COMP nondet helpers and override the return type
        // accordingly. Fallback to the LLVM-deduced return type otherwise.
        if (Type *ndTy = deduceNondetReturnType(funcName, func->program->typeHandler)) {
            type = ndTy;
        } else {
            type = func->getType(FT->getReturnType());
        }

        if (llvm::isa<llvm::InlineAsm>(operand)) {
            parseInlineASM(ins, func, block);
            return;
        }

        funcValue = func->getExpr(operand);
    }

    int i = 0;
    for (const llvm::Use& param : args_wrapper(callInst)) {
        //sometimes LLVM uses these functions with more arguments than their C counterparts
        if ((funcName.compare("memcpy") == 0 || funcName.compare("memmove") == 0 || funcName.compare("memset") == 0)  && i == 3) {
            break;
        }

        if (!func->getExpr(param)) {
            createFuncCallParam(param, *func->program);
        }
        params.push_back(func->getExpr(param));
        i++;
    }

    if (funcName.compare("va_start") == 0) {
        params.push_back(func->lastArg);
    }

    //call function if it returns void, otherwise store function return value to a new variable and use this variable instead of function call
    if (llvm::dyn_cast_or_null<VoidType>(type)) {
        func->createExpr(value, std::make_unique<CallExpr>(funcValue, funcName, params, type));
        block->addExpr(func->getExpr(value));
    } else {
        auto callExpr = std::make_unique<CallExpr>(funcValue, funcName, params, type);
        //llvm::errs() << "Var: " << var->valueName << " with type:  " << expr->getType()->toString() <<  " from ";
	//llvm::errs() << "CallExpr created: ";
	//value->print(llvm::errs());
	        //llvm::errs() << "inlining?\n";

        // for example printf returns value, but it is usually not used. in this case, we need to add the call to the block regardless
        if (value->hasNUses(0)) {
            block->addExpr(callExpr.get());
            func->createExpr(value, std::move(callExpr));
        } else {
            inlineOrCreateVariable(value, func->program->addOwnership(std::move(callExpr)), func, block);
        }
    }
}

static std::string toRawString(const std::string& str) {
    std::string ret = str;
    size_t pos = 0;

    while ((pos = ret.find("%", pos)) != std::string::npos) {
        if (ret[pos + 1] < 48 || ret[pos + 1] > 57) {
            ret.replace(pos, 1, "%%");
            pos += 2;
        }
    }

    pos = 0;
    while ((pos = ret.find("$", pos)) != std::string::npos) {
        if (ret[pos + 1] != '$') {
            ret.replace(pos, 1, "%");
        } else {
            pos += 2;
        }
    }

    pos = 0;
    while ((pos = ret.find("$$", pos)) != std::string::npos) {
        ret.replace(pos, 2, "$");
    }

    pos = 0;
    while ((pos = ret.find("\"", pos)) != std::string::npos) {
        ret.replace(pos, 1, "\\\"");
        pos += 2;
    }

    pos = 0;
    while ((pos = ret.find("\n", pos)) != std::string::npos) {
        ret.replace(pos, 1, "\\n");
        pos += 2;
    }

    pos = 0;
    while ((pos = ret.find("\t", pos)) != std::string::npos) {
        ret.replace(pos, 1, "\\t");
        pos += 2;
    }

    return ret;
}

static void parseInlineASM(const llvm::Instruction& ins, Func* func, Block* block) {
    const auto callInst = llvm::cast<llvm::CallInst>(&ins);
#if LLVM_VERSION_MAJOR >= 8
    const auto IA = llvm::cast<llvm::InlineAsm>(callInst->getCalledOperand());
#else
    const auto IA = llvm::cast<llvm::InlineAsm>(callInst->getCalledValue());
#endif

    std::string asmString = toRawString(IA->getAsmString());
    std::vector<std::string> inputStrings;
    std::vector<std::string> outputStrings;
    std::string usedReg;

    if (!IA->getConstraintString().empty()) {
        outputStrings = getAsmOutputStrings(IA->ParseConstraints());
        inputStrings = getAsmInputStrings(IA->ParseConstraints());
        usedReg = getAsmUsedRegString(IA->ParseConstraints());
    }

    std::vector<Expr*> args;
    for (const llvm::Use& arg : args_wrapper(callInst)) {
        if (!func->getExpr(arg.get())) {
            createFuncCallParam(arg, *func->program);
        }

        const llvm::AllocaInst* AI = llvm::dyn_cast_or_null<llvm::AllocaInst>(arg.get());
        const llvm::GetElementPtrInst* GI = llvm::dyn_cast_or_null<llvm::GetElementPtrInst>(arg.get());
        const llvm::CastInst* CI = llvm::dyn_cast_or_null<llvm::CastInst>(arg.get());
        const llvm::GlobalVariable* GV = llvm::dyn_cast_or_null<llvm::GlobalVariable>(arg.get());
        llvm::ConstantExpr* CE = llvm::dyn_cast_or_null<llvm::ConstantExpr>(arg.get());

        //creates new variable for every alloca, getelementptr and cast instruction and global variable that inline asm takes as a parameter
        //as inline asm has problem with casts and expressions containing "&" symbol
        if (GI || CI || AI || GV) {
            auto newVar = std::make_unique<Value>(func->getVarName(), func->getExpr(arg.get())->getType());
            auto stackAlloc = std::make_unique<StackAlloc>(newVar.get());
            auto newAssign = std::make_unique<AssignExpr>(newVar.get(), func->getExpr(arg.get()));
            args.push_back(newVar.get());

            block->addExprAndOwnership(std::move(stackAlloc));
            block->addExprAndOwnership(std::move(newAssign));

            block->addOwnership(std::move(newVar));
        } else if (CE) {
            if (CE->getOpcode() == llvm::Instruction::GetElementPtr) {
                auto newVar = std::make_unique<Value>(func->getVarName(), func->getExpr(arg.get())->getType());
                auto stackAlloc = std::make_unique<StackAlloc>(newVar.get());
                auto newAssign = std::make_unique<AssignExpr>(newVar.get(), func->getExpr(arg.get()));

                args.push_back(newVar.get());

                block->addExprAndOwnership(std::move(stackAlloc));
                block->addExprAndOwnership(std::move(newAssign));

                block->addOwnership(std::move(newVar));
            } else {
                auto *expr = func->getExpr(arg.get());
                assert(expr);
                args.push_back(expr);
            }
        } else {
            args.push_back(func->getExpr(arg.get()));
        }
    }

    //output parsing
    std::vector<std::pair<std::string, Expr*>> output;
    Expr* expr = nullptr;
    unsigned pos = 0;
    for (const auto& str : outputStrings) {
        if (str.find('*') == std::string::npos) {
            output.push_back({str, expr});
        } else {
            output.push_back({str, args[pos]});
            pos++;
        }
    }

    //input parsing
    std::vector<std::pair<std::string, Expr*>> input;
    unsigned arg = args.size() - 1;
    for (int i = inputStrings.size() - 1; i >= 0; i--) {
        //handle number constraint reffering to multiple option constraint
        std::string inputString = inputStrings[i].substr(1, inputStrings[i].size() - 2);
        std::string::const_iterator it = inputString.begin();
        while (it != inputString.end() && std::isdigit(*it)) {
            ++it;
        }

        //handle number constraint refering to multi-alternative output constraint
        if (it == inputString.end()) {
            int num = std::stoi(inputString);
            std::string outputString = output[num].first;
            if (outputString.find(',') != std::string::npos) {
                output[num].first = "\"+" + outputString.substr(3, outputString.size() - 3);
                arg--;
                continue;
            }
        }

        input.push_back({inputStrings[i], args[arg]});
        arg--;
    }

    auto asmExpr = std::make_unique<AsmExpr>(asmString, output, input, usedReg);
    block->addExpr(asmExpr.get());
    func->createExpr(&ins, std::move(asmExpr));
}

static void parseBitcastInstruction(const llvm::Instruction& ins, Func* func, Block* block, Program& program) {
    Expr* expr = program.getExpr(ins.getOperand(0));
    assert(expr);

    const llvm::CastInst* CI = llvm::cast<const llvm::CastInst>(&ins);

    auto* unionVar = static_cast<Value*>(program.makeExpr<Value>(func->getVarName(), program.bitcastUnion));
    auto* stackAlloc = program.makeExpr<StackAlloc>(unionVar);

    auto* assignInitial = program.makeExpr<AssignExpr>(program.makeExpr<AggregateElement>(unionVar, program.bitcastUnion->indexOfType(program.getType(CI->getSrcTy()))), expr);
    auto* resultingValue = program.makeExpr<AggregateElement>(unionVar, program.bitcastUnion->indexOfType(program.getType(CI->getDestTy())));

    block->addExpr(stackAlloc);
    block->addExpr(assignInitial);

    program.addExpr(&ins, resultingValue);
}

// a no-nsw add/sub/mul, or a select, &, |, ^ or shift whose unsigned-ness comes from one:
// C computes it in unsigned
static bool isWrappingArith(Expr* e) {
    if (auto* a = llvm::dyn_cast<AddExpr>(e)) return a->wraps;
    if (auto* s = llvm::dyn_cast<SubExpr>(e)) return s->wraps;
    if (auto* m = llvm::dyn_cast<MulExpr>(e)) return m->wraps;
    if (auto* s = llvm::dyn_cast<SelectExpr>(e)) return isWrappingArith(s->left) || isWrappingArith(s->right);
    if (llvm::isa<AndExpr>(e) || llvm::isa<OrExpr>(e) || llvm::isa<XorExpr>(e)) {
        auto* b = static_cast<BinaryExpr*>(e);
        return isWrappingArith(b->left) || isWrappingArith(b->right);
    }
    if (llvm::isa<ShlExpr>(e))
        return isWrappingArith(static_cast<BinaryExpr*>(e)->left);
    return false;
}

static Expr* parseCastInstruction(const llvm::Instruction& ins, Program& program) {
    Expr* expr = program.getExpr(ins.getOperand(0));
    
    // If the operand expression doesn't exist yet, try to create it
    if (!expr) {
        const llvm::Value* operand = ins.getOperand(0);
        
        // If the operand is an instruction, try to process it first
        if (const llvm::Instruction* operandInst = llvm::dyn_cast<llvm::Instruction>(operand)) {
            expr = parseLLVMInstruction(*operandInst, program);
        } 
        // If it's a constant, try to create a constant value
        else if (llvm::isa<llvm::Constant>(operand)) {
            expr = createConstantValue(operand, program);
            if (expr) {
                program.addExpr(operand, expr);
            }
        }
    }
    
    assert(expr);

    //operand is used for initializing output in inline asm
    if (!expr || llvm::isa<AsmExpr>(expr)) {
        return nullptr;
    }

    const llvm::CastInst* CI = llvm::cast<const llvm::CastInst>(&ins);


    if (llvm::isa<llvm::SExtInst>(CI)) {
        // for SExt, we do double cast -- first cast to the original
        // type that is made signed and then to the new type.
        // We must do that because we store all values as unsigned...
    if (CI->getOperand(0)->getType()->isIntegerTy(1) &&
        CI->getDestTy()->isIntegerTy() ) {
        // create zero
        auto zero = std::make_unique<Value>("0", program.getType(CI->getDestTy()));
        // create -1
        auto minusOne = std::make_unique<Value>("-1", program.getType(CI->getDestTy()));
        // create comparison select ? -1 : 0
        Expr* cond = program.getExpr(ins.getOperand(0));

        auto slctExpr = program.makeExpr<SelectExpr>(cond, minusOne.get(), zero.get());
        program.addOwnership(std::move(zero));
        program.addOwnership(std::move(minusOne));
        return slctExpr;
    }
        auto *recastOrigExpr
            = program.makeExpr<CastExpr>(
                expr,
                program.getType(ins.getOperand(0)->getType())
        );
        auto *IT = static_cast<IntegerType*>(recastOrigExpr->getType());
        recastOrigExpr->setType(program.typeHandler.setSigned(IT));

        auto *castExpr = program.makeExpr<CastExpr>(
                recastOrigExpr,
                program.getType(CI->getDestTy())
        );
        IT = static_cast<IntegerType*>(castExpr->getType());
        castExpr->setType(program.typeHandler.setSigned(IT));

        return castExpr;
    }

    if (llvm::isa<llvm::ZExtInst>(CI)) {
        auto *recastOrigExpr
                    = program.makeExpr<CastExpr>(
                        expr,
                        program.getType(ins.getOperand(0)->getType())
                );
        auto *IT = static_cast<IntegerType*>(recastOrigExpr->getType());
        recastOrigExpr->setType(program.typeHandler.setUnsigned(IT));
        auto *castExpr = program.makeExpr<CastExpr>(
                recastOrigExpr,
                program.getType(CI->getDestTy())
        );
        IT = static_cast<IntegerType*>(castExpr->getType());
        castExpr->setType(program.typeHandler.setUnsigned(IT));
        return castExpr;
    }
    // a wrapping add/sub/mul is printed in unsigned; sitofp reads it signed,
    // so cast it back to the signed source type first, as for SExt
    if (ins.getOpcode() == llvm::Instruction::SIToFP && isWrappingArith(expr)) {
        auto *recastOrigExpr = program.makeExpr<CastExpr>(expr, program.getType(ins.getOperand(0)->getType()));
        auto *IT = static_cast<IntegerType*>(recastOrigExpr->getType());
        recastOrigExpr->setType(program.typeHandler.setSigned(IT));
        expr = recastOrigExpr;
    }
    auto castExpr = program.makeExpr<CastExpr>(expr, program.getType(CI->getDestTy()));
    auto IT = static_cast<IntegerType*>(castExpr->getType());
    if (ins.getOpcode() == llvm::Instruction::FPToUI) {
        castExpr->setType(program.typeHandler.toggleSignedness(IT));
    }

    if (ins.getOpcode() == llvm::Instruction::FPToSI) {
        castExpr->setType(program.typeHandler.setSigned(IT));
    }

    return castExpr;
}

static Expr* parseSelectInstruction(const llvm::Instruction& ins, Program& program) {
    const llvm::SelectInst* SI = llvm::cast<const llvm::SelectInst>(&ins);
    Expr* cond = program.getExpr(SI->getCondition());

    Expr* val0 = program.getExpr(ins.getOperand(1));
    Expr* val1 = program.getExpr(ins.getOperand(2));
    assert(val0 && val1);

    return program.makeExpr<SelectExpr>(cond, val0, val1);
}

static Expr* parseGepInstruction(const llvm::Instruction& ins, Program& program) {
    const llvm::GetElementPtrInst* gepInst = llvm::cast<llvm::GetElementPtrInst>(&ins);

    Expr* expr = program.getExpr(gepInst->getOperand(0));
    assert(expr);

    llvm::Type* prevType = gepInst->getOperand(0)->getType();
    Expr* prevExpr = expr;
    std::vector<Expr*> indices;

    //if getelementptr contains null, cast it to given type
    if (expr->isZero()) {
        auto newCast = std::make_unique<CastExpr>(expr, program.getType(prevType));
        prevExpr = newCast.get();
        program.addOwnership(std::move(newCast));
    }

    // Normalize pointer index for robust signed pointer arithmetic
    auto normalizeIndexForPointerShift = [&](Expr* idx) -> Expr* {
        Expr* result = idx;
        // Detect pattern: (0 - X) => -(X)
        if (auto sub = llvm::dyn_cast_or_null<SubExpr>(idx)) {
            if (sub->left && sub->left->isZero() && sub->right) {
                Expr* inner = sub->right;
                // Unwrap ALL casts to get to the core expression
                // This handles: (int)(unsigned int)(ptrtoint(...))
                while (auto cast = llvm::dyn_cast_or_null<CastExpr>(inner)) {
                    inner = cast->expr;
                }
                // FIX: Cast to long FIRST, then negate (not the other way around)
                // This generates: -(long)expr instead of (long)(-expr)
                // The latter is invalid when expr is a pointer like &(...)
                Expr* innerAsLong = program.makeExpr<CastExpr>(inner, program.typeHandler.slong.get());
                result = program.makeExpr<MinusExpr>(innerAsLong);
                return result;  // Already cast, don't cast again below
            }
        }
        // Cast index (possibly negated) to appropriate type for pointer arithmetic
        // Use unsigned long long for unsigned indices (like size_t), signed long for signed indices
        auto* intType = llvm::dyn_cast_or_null<IntegerType>(result->getType());
        if (intType && intType->unsignedType) {
            // For unsigned indices, use unsigned long long to preserve the value
            result = program.makeExpr<CastExpr>(result, program.typeHandler.ulonglong.get());
        } else {
            // For signed indices, use signed long to model ptrdiff_t
            result = program.makeExpr<CastExpr>(result, program.typeHandler.slong.get());
        }
        return result;
    };

    for (auto it = llvm::gep_type_begin(gepInst); it != llvm::gep_type_end(gepInst); it++) {
        Expr* index = program.getExpr(it.getOperand());
        assert(index);

        if (prevType->isPointerTy()) {
            if (index->isZero()) {
                indices.push_back(program.makeExpr<DerefExpr>(prevExpr));
            } else {
                indices.push_back(program.makeExpr<PointerShift>(program.getType(prevType), prevExpr, normalizeIndexForPointerShift(index)));
            }
        }

        if (prevType->isArrayTy()) {
            indices.push_back(program.makeExpr<ArrayElement>(prevExpr, index, program.getType(prevType->getArrayElementType())));
        }

        if (prevType->isStructTy()) {
            llvm::ConstantInt* CI = llvm::dyn_cast_or_null<llvm::ConstantInt>(it.getOperand());
            if (!CI) {
                throw std::invalid_argument("Invalid GEP index - access to struct element only allows integer!");
            }

            if (!prevExpr->getType()) {
                assert(false);
            }
            indices.push_back(program.makeExpr<AggregateElement>(prevExpr, CI->getSExtValue()));
        }

        prevType = it.getIndexedType();
        prevExpr = indices[indices.size() - 1];
    }
    auto newGep = program.makeExpr<GepExpr>(std::move(indices));
    return program.makeExpr<RefExpr>(newGep, program.typeHandler.pointerTo(newGep->getType()));
}

Expr* parsePhiInstruction(const llvm::Instruction& ins, Func* func) {
    auto* expr = func->createPhiVariable(&ins);
    func->program->addExpr(&ins, expr);
    return expr;
}

Expr* parseConstantBitcast(const llvm::Instruction& ins, Program& program) {
    // TODO handle situation when c cast is lossy
    return createConstantValue(ins.getOperand(0), program);
}

Expr* parseLLVMInstruction(const llvm::Instruction& ins, Program& program) {
    switch (ins.getOpcode()) {
    case llvm::Instruction::Add:
    case llvm::Instruction::FAdd:
    case llvm::Instruction::Sub:
    case llvm::Instruction::FSub:
    case llvm::Instruction::Mul:
    case llvm::Instruction::FMul:
    case llvm::Instruction::UDiv:
    case llvm::Instruction::FDiv:
    case llvm::Instruction::SDiv:
    case llvm::Instruction::URem:
    case llvm::Instruction::FRem:
    case llvm::Instruction::SRem:
    case llvm::Instruction::And:
    case llvm::Instruction::Or:
    case llvm::Instruction::Xor:
        return parseBinaryInstruction(ins, program);
    case llvm::Instruction::Load:
        return parseLoadInstruction(ins, program);
    case llvm::Instruction::ICmp:
        return parseICmpInstruction(ins, program);
    case llvm::Instruction::FCmp:
        return parseFCmpInstruction(ins, program);
    case llvm::Instruction::Shl:
    case llvm::Instruction::LShr:
    case llvm::Instruction::AShr:
        return parseShiftInstruction(ins, program);
    case llvm::Instruction::Select:
        return parseSelectInstruction(ins, program);
    case llvm::Instruction::GetElementPtr:
        return parseGepInstruction(ins, program);
    case llvm::Instruction::ExtractValue:
        return parseExtractValueInstruction(ins, program);
    case llvm::Instruction::InsertValue:
        // insertvalue is handled in the top-level creator where we have Block/Func.
        // Here, just return the expression if it was already created.
        return program.getExpr(&ins);
    case llvm::Instruction::BitCast:
    case llvm::Instruction::SExt:
    case llvm::Instruction::ZExt:
    case llvm::Instruction::FPToSI:
    case llvm::Instruction::SIToFP:
    case llvm::Instruction::FPTrunc:
    case llvm::Instruction::FPExt:
    case llvm::Instruction::FPToUI:
    case llvm::Instruction::UIToFP:
    case llvm::Instruction::PtrToInt:
    case llvm::Instruction::IntToPtr:
    case llvm::Instruction::Trunc:
        return parseCastInstruction(ins, program);
    case llvm::Instruction::Freeze:
        return parseFreezeInstruction(ins, program);
    case llvm::Instruction::FNeg:
        return parseFnegInstruction(ins, program);
    default:
        llvm::errs() << "Unsupported instruction encountered by llvm2c: ";
        ins.print(llvm::errs());
        llvm::errs() << "\n";
        llvm::errs().flush();
        assert(false && "File contains unsupported instruction!");
        abort();
    }
}
    // {
    //     auto *expr = parseCastInstruction(ins, program);
    //     if (auto castExpr = llvm::dyn_cast_or_null<CastExpr>( expr ))
    //         castExpr->setLossy();
    //     return expr;
    // }

void createExpressions(const llvm::Module* module, Program& program, bool bitcastUnions) {
    assert(program.isPassCompleted(PassType::CreateConstants));
    assert(program.isPassCompleted(PassType::CreateAllocas));
    assert(program.isPassCompleted(PassType::CreateFunctionParameters));
    assert(!bitcastUnions || program.isPassCompleted(PassType::PrepareBitcastUnion));

    for (const auto& function : module->functions()) {
        auto* func = program.getFunction(&function);
        for (const auto& block : function) {
            auto* myBlock = func->getBlock(&block);

            for (const auto& ins : block) {
                Expr* expr = nullptr;
                switch (ins.getOpcode()) {
                case llvm::Instruction::Alloca: {
                    // VLA: `v = (elem*)__builtin_alloca(n * sizeof(elem));` here, where
                    // the size expression exists and evaluates as in the IR
                    const auto* AI = llvm::cast<llvm::AllocaInst>(&ins);
                    auto vla = program.vlaAllocs.find(AI);
                    if (vla != program.vlaAllocs.end()) {
                        Expr* count = program.getExpr(AI->getArraySize());
                        if (!count)
                            count = createConstantValue(AI->getArraySize(), program);
                        auto* ull = program.typeHandler.ulonglong.get();
                        uint64_t elemBytes = module->getDataLayout().getTypeAllocSize(AI->getAllocatedType());
                        Expr* bytes = program.makeExpr<MulExpr>(program.makeExpr<CastExpr>(count, ull),
                                                                program.makeExpr<Value>(std::to_string(elemBytes) + "ULL", ull), true);
                        Expr* call = program.makeExpr<CallExpr>(nullptr, "__builtin_alloca", std::vector<Expr*>{bytes},
                                                                program.typeHandler.pointerTo(program.typeHandler.voidType.get()));
                        // no cast: the `void*` converts implicitly, and parseMetadataTypes
                        // may still change var's element type (signedness) after this
                        Value* var = vla->second;
                        myBlock->addExpr(program.makeExpr<AssignExpr>(var, call));
                    }
                    break;
                }
                case llvm::Instruction::Switch:
                    parseSwitchInstruction(ins, false, nullptr, func, myBlock);
                    break;
                case llvm::Instruction::Br:
                case llvm::Instruction::Ret:
                    // leave these for later
                    break;
                case llvm::Instruction::PHI:
                    parsePhiInstruction(ins, func);
                    break;
                case llvm::Instruction::Store:
                    expr = parseStoreInstruction(ins, program);
                    if (expr)
                        myBlock->addExpr(expr);
                    break;
                case llvm::Instruction::InsertValue:
                    parseInsertValueInstruction(ins, func, myBlock, program);
                    break;
                case llvm::Instruction::LandingPad:
                    parseLandingPadInstruction(ins, func, myBlock, program);
                    break;
                case llvm::Instruction::Resume:
                    parseResumeInstruction(ins, func, myBlock, program);
                    break;
                case llvm::Instruction::Call:
                    parseCallInstruction(ins, func, myBlock);
                    break;
                case llvm::Instruction::Unreachable:
                case llvm::Instruction::Fence:
                    parseAsmInst(ins, func, myBlock);
                    break;
                case llvm::Instruction::BitCast:
                    if (bitcastUnions) {
                        parseBitcastInstruction(ins, func, myBlock, program);
                        break;
                    } // else fall-through
                default:
                    expr = parseLLVMInstruction(ins, program);
                    if (expr) {
                        program.addExpr(&ins, expr);
                        inlineOrCreateVariable(&ins, expr, func, myBlock);
                    }
                }
            }

        }
    }

    program.addPass(PassType::CreateExpressions);
}
