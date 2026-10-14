#pragma once

#include <string>

#include "Expr.h"

/**
 * @brief The BinaryExpr class is a base class for all binary expressions.
 */
class BinaryExpr : public ExprBase {
public:
    Expr* left; //first operand of binary operation
    Expr* right; //second operand of binary operation

    BinaryExpr(Expr*, Expr*, ExprKind);
};

/**
 * @brief The AddExpr class represents add.
 */
class AddExpr : public BinaryExpr {
public:
    bool isUnsigned;
    // the IR op has no nsw flag: it wraps, so it must compute in unsigned
    const bool wraps;

    AddExpr(Expr*, Expr*, bool isUnsigned);

    void accept(ExprVisitor& visitor) override;

    static bool classof(const Expr* expr);
};

/**
 * @brief The SubExpr class represents substitution.
 */
class SubExpr : public BinaryExpr {
public:
    bool isUnsigned;
    // the IR op has no nsw flag: it wraps, so it must compute in unsigned
    const bool wraps;

    SubExpr(Expr*, Expr*, bool isUnsigned);

    void accept(ExprVisitor& visitor) override;

    static bool classof(const Expr* expr);
};

/**
 * @brief The AssignExpr class represents assingment.
 */
class AssignExpr : public BinaryExpr {
public:
    AssignExpr(Expr*, Expr*);

    void accept(ExprVisitor& visitor) override;

    static bool classof(const Expr* expr);
};

/**
 * @brief The MulExpr class represents multiplication.
 */
class MulExpr : public BinaryExpr {
public:
    bool isUnsigned;
    // the IR op has no nsw flag: it wraps, so it must compute in unsigned
    const bool wraps;

    MulExpr(Expr*, Expr*, bool isUnsigned);

    void accept(ExprVisitor& visitor) override;

    static bool classof(const Expr* expr);
};

/**
 * @brief The DivExpr class represents division.
 */
class DivExpr : public BinaryExpr {
public:
    bool isUnsigned;

    DivExpr(Expr*, Expr*, bool isUnsigned);

    void accept(ExprVisitor& visitor) override;

    static bool classof(const Expr* expr);
};

/**
 * @brief The RemExpr class represents remainder (modulo).
 */
class RemExpr : public BinaryExpr {
public:
    bool isUnsigned;

    RemExpr(Expr*, Expr*, bool isUnsigned);

    void accept(ExprVisitor& visitor) override;

    static bool classof(const Expr* expr);
};

/**
 * @brief The AndExpr class represents bitwise AND.
 */
class AndExpr : public BinaryExpr {
public:
    AndExpr(Expr*, Expr*);

    void accept(ExprVisitor& visitor) override;

    static bool classof(const Expr* expr);
};

/**
 * @brief The OrExpr class represents bitwise OR.
 */
class OrExpr : public BinaryExpr {
public:
    OrExpr(Expr*, Expr*);

    void accept(ExprVisitor& visitor) override;

    static bool classof(const Expr* expr);
};

/**
 * @brief The XorExpr class represents bitwise XOR.
 */
class XorExpr : public BinaryExpr {
public:
    XorExpr(Expr*, Expr*);

    void accept(ExprVisitor& visitor) override;

    static bool classof(const Expr* expr);
};

/**
 * @brief The CmpExpr class represents comparsion.
 */
class CmpExpr : public BinaryExpr {
public:
    std::string comparsion; //symbol of comparsion
    bool isUnsigned; //indicates that unsigned version of cmp instruction was used
    CmpExpr(Expr*, Expr*, const std::string&, bool);

    void accept(ExprVisitor& visitor) override;

    static bool classof(const Expr* expr);

    static BoolType type;
};

/**
 * @brief The AshrExpr class represents arithmetic shift right.
 */
class AshrExpr : public BinaryExpr {
public:
    AshrExpr(Expr*, Expr*);

    void accept(ExprVisitor& visitor) override;

    static bool classof(const Expr* expr);
};

/**
 * @brief The LshrExpr class represents logical shift right.
 */
class LshrExpr : public BinaryExpr {
public:
    LshrExpr(Expr*, Expr*);

    void accept(ExprVisitor& visitor) override;

    static bool classof(const Expr* expr);
};

/**
 * @brief The ShlExpr class represents shift left.
 */
class ShlExpr : public BinaryExpr {
public:
    bool isUnsigned;

    ShlExpr(Expr*, Expr*, bool isUnsigned);

    void accept(ExprVisitor& visitor) override;

    static bool classof(const Expr* expr);
};
