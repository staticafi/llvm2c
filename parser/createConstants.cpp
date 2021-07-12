#include <llvm/IR/Instructions.h>

#include "../core/Program.h"
#include "constval.h"

static void createConstantsForOperands(const llvm::User* ins, Program& program) {
    for (const auto& op : ins->operands()) {

        // assumption: the GV that is an operand of this has already been initialized
        // reason: pass ComputeGlobalVarsOrder has been completed
        if (llvm::isa<llvm::GlobalVariable>(op.get()))
            continue;

        if (auto *user = llvm::dyn_cast_or_null<llvm::User>(op.get())) {
            if (!llvm::isa<llvm::Instruction>(user))
                createConstantsForOperands(user, program);
        }

        if (auto C = llvm::dyn_cast_or_null<llvm::Constant>(op.get())) {
            if (program.getExpr(op.get()) == nullptr)
                program.addExpr(op.get(), createConstantValue(C, program));
        }

    }
}

void createConstants(const llvm::Module* mod, Program& program) {

    assert(program.isPassCompleted(PassType::ComputeGlobalVarsOrder));
    assert(program.isPassCompleted(PassType::CreateFunctions));

    for (const auto& gvar : mod->globals()) {
        if (gvar.hasInitializer()) {
            auto* init = gvar.getInitializer();
            if (auto *user = llvm::dyn_cast_or_null<llvm::User>(init)) {
                createConstantsForOperands(user, program);
            }
        }
    }

    for (const auto& func : mod->functions()) {
        for (const auto& bb : func) {
            for (const auto& ins : bb) {
                createConstantsForOperands(&ins, program);
            }
        }
    }

    program.addPass(PassType::CreateConstants);
}
