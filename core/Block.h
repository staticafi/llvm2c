#pragma once

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/InlineAsm.h"

#include "../expr/Expr.h"

class Func;

/**
 * @brief The Block class represents one of the block of the LLVM function.
 */
class Block {
    friend class Func;
public:
    const llvm::BasicBlock* block;

    Func* func;

    // a sequence of expression forming this basic block
    std::vector<Expr*> expressions;

    //store expressions
    std::map<Expr*, DerefExpr*> derefs; //Map used for storing pointers to DerefExpr (used in store instruction parsing)

    // instead of `goto block`, the block will be outputed in place
    bool doInline = false;

    // is this first block of a function?
    bool isFirst;

    /**
     * @brief createConstantValue Creates Value for given ConstantInt or ConstantFP and inserts it into exprMap.
     * @param val constant value
     */
    void createConstantValue(const llvm::Value* val);

    /**
     * @brief createFuncCallParam Creates new Expr for parameter of function call.
     * @param param Parameter of function call
     */
    void createFuncCallParam(const llvm::Use& param);

    /**
     * @brief getAsmOutputString Parses asm constraint string to get output operands.
     * @param info ConstraintInfoVector containing parsed asm constraint string
     * @return Strings containing output operand for inline assembler
     */
    std::vector<std::string> getAsmOutputStrings(llvm::InlineAsm::ConstraintInfoVector info) const;

    /**
     * @brief getAsmInputStrings Parses asm constraint string to get input operands.
     * @param info ConstraintInfoVector containing parsed asm constraint string
     * @return Vector of strings containing input operand for inline assembler
     */
    std::vector<std::string> getAsmInputStrings(llvm::InlineAsm::ConstraintInfoVector info) const;

    /**
     * @brief getRegisterString Parses string containing register label from LLVM to C.
     * @param str LLVM register string
     * @return C register string
     */
    std::string getRegisterString(const std::string& str) const;

    /**
     * @brief getAsmUsedRegString Parses asm constraint string to get used registers.
     * @param info ConstraintInfoVector containing parsed asm constraint string
     * @return String containing used registers
     */
    std::string getAsmUsedRegString(llvm::InlineAsm::ConstraintInfoVector info) const;

    /**
     * @brief toRawString Converts string to its raw format (including escape chars etc.)
     * @param str String
     * @return String in raw format
     */
    std::string toRawString(const std::string& str) const;

public:
    std::string blockName;

    void addExpr(Expr *expr) {
        assert(expr);
        expressions.push_back(expr);
    }

    /**
     * @brief Block Constructor for Block.
     * @param blockName Name of the block
     * @param block llvm::BasicBlock for parsing
     * @param func Func where the block is located
     */
    Block(const std::string &blockName, const llvm::BasicBlock* block, Func* func);

    void insertValue(const llvm::Value* value, std::unique_ptr<Value> expr);

    Value* getValue(const llvm::Value* value);

    void addOwnership(std::unique_ptr<Expr> expr);

    void addExprAndOwnership(std::unique_ptr<Expr> expr);
};
