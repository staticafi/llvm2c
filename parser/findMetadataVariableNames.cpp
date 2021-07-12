#include "../core/Program.h"
#include "../core/Func.h"
#include "../core/Block.h"

#include <llvm/IR/Instruction.h>

static void setMetadataInfo(const llvm::CallInst* ins, Block* block) {
    llvm::Metadata* md = llvm::dyn_cast_or_null<llvm::MetadataAsValue>(ins->getOperand(0))->getMetadata();
    llvm::Value* referredVal = llvm::cast<llvm::ValueAsMetadata>(md)->getValue();
    Expr* referred = block->func->getExpr(referredVal);

    if (auto* re = llvm::dyn_cast_or_null<RefExpr>(referred)) {
        referred = re->expr;
    }

    if (Value* variable = llvm::dyn_cast_or_null<Value>(referred)) {
        llvm::Metadata* varMD = llvm::dyn_cast_or_null<llvm::MetadataAsValue>(ins->getOperand(1))->getMetadata();
        llvm::DILocalVariable* localVar = llvm::dyn_cast_or_null<llvm::DILocalVariable>(varMD);

        if (!localVar->getName().empty()) {
            auto& counters = block->func->variableCounters;
            auto it = counters.find(localVar->getName().str());
            std::string name = localVar->getName().str();

            if (it != counters.end()) {
                it->second++;
                name += std::to_string(it->second);
            } else {
                counters[localVar->getName().str()] = 1;
            }

            variable->valueName = name;
        }
    }
}


void findMetadataVariableNames(const llvm::Module* module, Program& program) {
    assert(program.isPassCompleted(PassType::CreateAllocas));

    for (const auto& function : module->functions()) {
        auto* func = program.getFunction(&function);
        for (const auto& block : function) {
            auto* myBlock = func->getBlock(&block);

            for (const auto& ins : block) {
                if (ins.getOpcode() == llvm::Instruction::Call) {
                    const llvm::CallInst* CI = llvm::cast<llvm::CallInst>(&ins);
                    if (CI->getCalledFunction()) {
                        if (CI->getCalledFunction()->getName().str().compare("llvm.dbg.declare") == 0) {
                            setMetadataInfo(CI, myBlock);
                        }
                    }
                }
            }

        }
    }

    program.addPass(PassType::FindMetadataVariableNames);
}
