#include "../core/Program.h"
#include "../core/Func.h"
#include "../core/Block.h"
#include "../type/TypeHandler.h"

#include <llvm/IR/Instruction.h>


static void initVarargStruct(StructType& varargStruct, Program& program) {
    varargStruct.addItem(program.typeHandler.uint.get(), "gp_offset");
    varargStruct.addItem(program.typeHandler.uint.get(), "fp_offset");
    varargStruct.addItem(program.typeHandler.pointerTo(program.typeHandler.voidType.get()), "overflow_arg_area");
    varargStruct.addItem(program.typeHandler.pointerTo(program.typeHandler.voidType.get()), "reg_save_area");
}

void parseStructDeclarations(const llvm::Module* module, Program& program) {

    for (llvm::StructType* structType : module->getIdentifiedStructTypes()) {
        std::string structName = TypeHandler::getStructName(structType->getName().str());

        if (!program.hasVarArg && structName.compare("__va_list_tag") == 0) {
            auto varargStruct = std::make_unique<StructType>(structName);
            initVarargStruct(*varargStruct, program);
            program.addStruct(std::move(varargStruct));

            program.hasVarArg = true;
            continue;
        }

        auto structExpr = std::make_unique<StructType>(structName);
        program.addStruct(std::move(structExpr));
    }

    program.addPass(PassType::ParseStructDeclarations);
}
