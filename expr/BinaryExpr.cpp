#include "BinaryExpr.h"

#include "llvm/Support/raw_ostream.h"

#include "../type/TypeHandler.h"

BoolType CmpExpr::type = BoolType{};

/*
 * BinaryExpr classes
 */

BinaryExpr::BinaryExpr(Expr* l, Expr* r, ExprKind kind): ExprBase(kind) {
    left = l;
    right = r;

    setType(TypeHandler::getBinaryType(left->getType(), right->getType()));
}

AddExpr::AddExpr(Expr* l, Expr* r, bool isUnsigned) :
    BinaryExpr(l, r, EK_AddExpr), isUnsigned(isUnsigned), wraps(isUnsigned) { }

void AddExpr::accept(ExprVisitor& visitor) {
    visitor.visit(*this);
}

bool AddExpr::classof(const Expr* expr) {
    return expr->getKind() == EK_AddExpr;
}

SubExpr::SubExpr(Expr* l, Expr* r, bool isUnsigned) :
    BinaryExpr(l, r, EK_SubExpr), isUnsigned(isUnsigned), wraps(isUnsigned) { }

void SubExpr::accept(ExprVisitor& visitor) {
    visitor.visit(*this);
}

bool SubExpr::classof(const Expr* expr) {
    return expr->getKind() == EK_SubExpr;
}

AssignExpr::AssignExpr(Expr* l, Expr* r) :
    BinaryExpr(l, r, EK_AssignExpr) { }

void AssignExpr::accept(ExprVisitor& visitor) {
    visitor.visit(*this);
}

bool AssignExpr::classof(const Expr* expr) {
    return expr->getKind() == EK_AssignExpr;
}

MulExpr::MulExpr(Expr* l, Expr* r, bool isUnsigned) :
    BinaryExpr(l, r, EK_MulExpr), isUnsigned(isUnsigned), wraps(isUnsigned) { }

void MulExpr::accept(ExprVisitor& visitor) {
    visitor.visit(*this);
}

bool MulExpr::classof(const Expr* expr) {
    return expr->getKind() == EK_MulExpr;
}

DivExpr::DivExpr(Expr* l, Expr* r, bool isUnsigned) :
    BinaryExpr(l, r, EK_DivExpr), isUnsigned(isUnsigned) { }

void DivExpr::accept(ExprVisitor& visitor) {
    visitor.visit(*this);
}

bool DivExpr::classof(const Expr* expr) {
    return expr->getKind() == EK_DivExpr;
}

RemExpr::RemExpr(Expr* l, Expr* r, bool isUnsigned) :
    BinaryExpr(l, r, EK_RemExpr), isUnsigned(isUnsigned) { }

void RemExpr::accept(ExprVisitor& visitor) {
    visitor.visit(*this);
}

bool RemExpr::classof(const Expr* expr) {
    return expr->getKind() == EK_RemExpr;
}

AndExpr::AndExpr(Expr* l, Expr* r) :
    BinaryExpr(l, r, EK_AndExpr) { }

void AndExpr::accept(ExprVisitor& visitor) {
    visitor.visit(*this);
}

bool AndExpr::classof(const Expr* expr) {
    return expr->getKind() == EK_AndExpr;
}

OrExpr::OrExpr(Expr* l, Expr* r) :
    BinaryExpr(l, r, EK_OrExpr) { }

void OrExpr::accept(ExprVisitor& visitor) {
    visitor.visit(*this);
}

bool OrExpr::classof(const Expr* expr) {
    return expr->getKind() == EK_OrExpr;
}

XorExpr::XorExpr(Expr* l, Expr* r) :
    BinaryExpr(l, r, EK_XorExpr) { }

void XorExpr::accept(ExprVisitor& visitor) {
    visitor.visit(*this);
}

bool XorExpr::classof(const Expr* expr) {
    return expr->getKind() == EK_XorExpr;
}

CmpExpr::CmpExpr(Expr* l, Expr* r, const std::string& cmp, bool isUnsigned) :
    BinaryExpr(l,r, EK_CmpExpr) {
    comparsion = cmp;
    this->isUnsigned = isUnsigned;
    setType(&CmpExpr::type);
}

void CmpExpr::accept(ExprVisitor& visitor) {
    visitor.visit(*this);
}

bool CmpExpr::classof(const Expr* expr) {
    return expr->getKind() == EK_CmpExpr;
}

AshrExpr::AshrExpr(Expr* l, Expr* r) :
    BinaryExpr(l, r, EK_AshrExpr) { }

void AshrExpr::accept(ExprVisitor& visitor) {
    visitor.visit(*this);
}

bool AshrExpr::classof(const Expr* expr) {
    return expr->getKind() == EK_AshrExpr;
}

LshrExpr::LshrExpr(Expr* l, Expr* r) :
    BinaryExpr(l, r, EK_LshrExpr) { }

void LshrExpr::accept(ExprVisitor& visitor) {
    visitor.visit(*this);
}

bool LshrExpr::classof(const Expr* expr) {
    return expr->getKind() == EK_LshrExpr;
}

ShlExpr::ShlExpr(Expr* l, Expr* r, bool isUnsigned) :
    BinaryExpr(l, r, EK_ShlExpr), isUnsigned(isUnsigned) { }

void ShlExpr::accept(ExprVisitor& visitor) {
    visitor.visit(*this);
}

bool ShlExpr::classof(const Expr* expr) {
    return expr->getKind() == EK_ShlExpr;
}

