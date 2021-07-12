#include "constval.h"
#include "toinst.h"

Expr* parseLLVMInstruction(const llvm::Instruction& ins, Program& program);

Expr* createUndefValue(const llvm::Type* ty, Program& program) {
    if (ty->isPointerTy()) {
        return program.makeExpr<Value>("0", program.getType(ty));
    }

    if (ty->isIntegerTy()) {
        auto zero = std::make_unique<Value>("0", program.getType(ty));
        return program.addOwnership(std::move(zero));
    }

    std::vector<Expr*> values;

    if (auto* ST = llvm::dyn_cast_or_null<llvm::ArrayType>(ty)) {
        for (unsigned i = 0; i < ST->getNumElements(); ++i) {
            values.push_back(createUndefValue(ST->getElementType(), program));
        }

    } else if (auto* ST = llvm::dyn_cast_or_null<llvm::StructType>(ty)) {
        for (unsigned i = 0; i < ST->getNumElements(); ++i) {
            values.push_back(createUndefValue(ST->getElementType(i), program));
        }
    } else {
        ty->print(llvm::errs(), true);
        assert(false && "globalVars: unrecognized type of undef value");
    }

    auto init = std::make_unique<AggregateInitializer>(values);
    init->setType(program.getType(ty));
    return program.addOwnership(std::move(init));
}

Expr* createConstantValue(const llvm::Value* val, Program& program) {
    if (llvm::isa<llvm::UndefValue>(val)) {
        return createUndefValue(val->getType(), program);
    }

    if (llvm::isa<llvm::ConstantPointerNull>(val))
        return program.makeExpr<Value>("0", program.getType(val->getType()));

    if (const llvm::GlobalVariable* GV = llvm::dyn_cast_or_null<llvm::GlobalVariable>(val)) {
        auto RE = program.getGlobalRef(GV);
        assert(RE && "initialize-global-vars: global value does not have a ref");

        return RE;
    }


    if (llvm::isa<llvm::Function>(val)) {
        auto ref = std::make_unique<Value>(val->getName().str(), program.typeHandler.pointerTo(program.typeHandler.schar.get()));
        return program.addOwnership(std::move(ref));
    }

    if (auto CI = llvm::dyn_cast_or_null<llvm::ConstantInt>(val)) {
        std::string value;
        if (CI->getBitWidth() > 64) {
            const llvm::APInt& API = CI->getValue();
            value = std::to_string(API.getLimitedValue());
        } else if (CI->getBitWidth() == 1) { //bool in LLVM
            value = std::to_string(-1 * CI->getSExtValue());
        } else {
            value = std::to_string(CI->getSExtValue());
        }

        return program.makeExpr<Value>(value, program.getType(CI->getType()));
    }

    if (auto CFP = llvm::dyn_cast_or_null<llvm::ConstantFP>(val)) {
        if (CFP->isInfinity()) {
            if (CFP->isNegative()) {
                auto call = std::make_unique<CallExpr>(nullptr, "__builtin_inff", std::vector<Expr*>{}, program.getType(CFP->getType()));
                auto* callPtr = program.addOwnership(std::move(call));
                return program.makeExpr<MinusExpr>(callPtr);
            } else {
                auto call = std::make_unique<CallExpr>(nullptr, "__builtin_inff", std::vector<Expr*>{}, program.getType(CFP->getType()));
                return program.addOwnership(std::move(call));
            }
        } else if (CFP->isNaN()){
            auto param = std::make_unique<Value>("\"\"", program.typeHandler.pointerTo(program.typeHandler.uchar.get()));
            auto call = std::make_unique<CallExpr>(nullptr, "__builtin_nanf", std::vector<Expr*>{param.get()}, program.typeHandler.floatType.get());
            program.addOwnership(std::move(param));
            return program.addOwnership(std::move(call));
        } else {
            std::string CFPvalue;
            if (CFP->getType()->isFloatTy()) {
                CFPvalue = std::to_string(CFP->getValueAPF().convertToFloat());
            } else if (CFP->getType()->isDoubleTy()) {
                CFPvalue = std::to_string(CFP->getValueAPF().convertToDouble());
            } else {
                assert(false && "constval: unknown constant floating point type");
            }

            if (CFPvalue.compare("-nan") == 0) {
                auto param = std::make_unique<Value>("\"\"", program.typeHandler.pointerTo(program.typeHandler.uchar.get()));
                auto call = std::make_unique<CallExpr>(nullptr, "__builtin_nanf", std::vector<Expr*>{param.get()}, program.typeHandler.floatType.get());
                auto* callPtr = program.addOwnership(std::move(call));
                program.addOwnership(std::move(param));
                return program.makeExpr<MinusExpr>(callPtr);
            } else {
                llvm::SmallVector<char, 32> string;
                CFPvalue = "";
                CFP->getValueAPF().toString(string, 32, 0);
                for (unsigned i = 0; i < string.size(); i++) {
                    CFPvalue += string[i];
                }
            }

            return program.makeExpr<Value>(CFPvalue, program.getType(CFP->getType()));
        }
    }

    if (const auto* CAZ = llvm::dyn_cast_or_null<llvm::ConstantAggregateZero>(val)) {
        std::vector<Expr*> values;

        for (unsigned i = 0; i < CAZ->getNumElements(); ++i) {
            auto* elem = CAZ->getElementValue(i);
            values.push_back(createConstantValue(elem, program));
        }

        auto ai = std::make_unique<AggregateInitializer>(std::move(values));
        ai->setType(program.getType(val->getType()));

        return program.addOwnership(std::move(ai));
    }

    if (const llvm::ConstantAggregate* CA = llvm::dyn_cast_or_null<llvm::ConstantAggregate>(val)) {
        std::vector<Expr*> values;

        for (int i = 0; true; ++i) {
            auto* elem = CA->getAggregateElement(i);
            if (!elem)
                break;

            values.push_back(createConstantValue(elem, program));
        }

        auto ai = std::make_unique<AggregateInitializer>(std::move(values));
        ai->setType(program.getType(val->getType()));

        return program.addOwnership(std::move(ai));
    }

    if (const llvm::ConstantDataSequential* CDS = llvm::dyn_cast_or_null<llvm::ConstantDataSequential>(val)) {
        std::vector<Expr*> values;

        for (unsigned i = 0; i < CDS->getNumElements(); ++i) {
            values.push_back(createConstantValue(CDS->getElementAsConstant(i), program));
        }

        auto ai = std::make_unique<AggregateInitializer>(values);
        ai->setType(program.getType(val->getType()));

        return program.addOwnership(std::move(ai));
    }

    if (auto *CE = const_cast<llvm::ConstantExpr*>(llvm::dyn_cast_or_null<llvm::ConstantExpr>(val))) {
        auto inst = toInst(CE);
        return parseLLVMInstruction(*inst.get(), program);
    }

    if (!val->getType()->isStructTy() && !val->getType()->isPointerTy() && !val->getType()->isArrayTy()) {
        val->getType()->print(llvm::errs(), true);

        val->print(llvm::errs(), true);

        assert(false && "constval: unknown type of constant value");
    }

    auto ai = std::make_unique<AggregateInitializer>(std::vector<Expr*>{});
    return program.addOwnership(std::move(ai));
}
