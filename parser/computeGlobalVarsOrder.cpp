#include <llvm/IR/Instructions.h>
#include "../core/Program.h"

#include <memory>
#include <regex>
#include <unordered_set>

static void computeTopo(const llvm::Constant* con, std::vector<const llvm::GlobalVariable*>& order, std::unordered_set<const llvm::GlobalVariable*>& visited) {
    if (auto gvar = llvm::dyn_cast_or_null<llvm::GlobalVariable>(con)) {
        if (!visited.insert(gvar).second) {
            return;
        }
    }

    for (const auto& operand : con->operands()) {
        if (auto constOp = llvm::dyn_cast_or_null<llvm::Constant>(operand.get())) {
            computeTopo(constOp, order, visited);
        }
    }

    if (auto gvar = llvm::dyn_cast_or_null<llvm::GlobalVariable>(con)) {
        order.push_back(gvar);
    }
}

static void parseGlobalVar(const llvm::GlobalVariable& gvar, Program& program) {
    std::string gvarName = gvar.getName().str();
    std::replace(gvarName.begin(), gvarName.end(), '.', '_');

    std::regex varName("var[0-9]+");
    if (std::regex_match(gvarName, varName)) {
        program.globalVarNames.insert(gvarName);
    }

    llvm::PointerType* PT = llvm::cast<llvm::PointerType>(gvar.getType());

    auto type = program.getType(PT->getElementType());
    if (!type)
        assert(false && "Unable to determine global variable type");

    auto var = std::make_unique<GlobalValue>(gvarName, nullptr, type);
    var->isStatic = gvar.hasInternalLinkage();

    program.globalRefs[&gvar] = std::make_unique<RefExpr>(var.get(), program.typeHandler.pointerTo(var->getType()));

    program.addExpr(&gvar, program.globalRefs[&gvar].get());
    program.globalVars.push_back(std::move(var));
}

void computeGlobalVarsOrder(const llvm::Module* module, Program& program) {

    std::unordered_set<const llvm::GlobalVariable*> visited;
    std::vector<const llvm::GlobalVariable*> order;

    assert(program.isPassCompleted(PassType::ParseStructItems));

    for (const llvm::GlobalVariable& gvar : module->globals()) {
        if (llvm::isa<llvm::Function>(&gvar)) {
            continue;
        }

        // do not initialize twice
        if (visited.find(&gvar) != visited.end()) {
            continue;
        }

        computeTopo(&gvar, order, visited);
    }

    for (const auto* gvar : order) {
        parseGlobalVar(*gvar, program);
    }

    program.addPass(PassType::ComputeGlobalVarsOrder);
}
