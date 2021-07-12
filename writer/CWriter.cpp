#include "CWriter.h"


void CWriter::include(StrRef header) {
    out << "#include <" << header << ">" << std::endl;
}

void CWriter::declareStruct(StrRef name) {
    out << "struct " << name << ";" << std::endl;
}

void CWriter::declareUnion(StrRef name) {
    out << "union " << name << ";" << std::endl;
}

void CWriter::comment(StrRef comment) {
    out << "// " << comment << std::endl;
}

void CWriter::startStruct(StrRef name) {
    out << "struct " << name << " {" << std::endl;
}

void CWriter::endStruct() {
    out << "};" << std::endl;
}

void CWriter::indent(size_t tabs) {
    for (size_t t = 0; t < tabs; ++t) {
        out << "    ";
    }
}

void CWriter::structItem(StrRef ty, StrRef name) {
    out << ty << " " << name << ";" << std::endl;
}

void CWriter::defineType(StrRef ty, StrRef alias, StrRef end) {
    out << "typedef " << ty << " " << alias << end << ";" << std::endl;
}

void CWriter::startFunction(StrRef ret, StrRef name) {
    out << ret << " " << name;
}

void CWriter::startArrayFunction(StrRef ty, size_t levels, StrRef name) {
    out << ty << " (";
    for (size_t i = 0; i < levels; ++i) {
        out << "*";
    }
    out << name;
}

void CWriter::endFunctionDecl() {
    out << ";" << std::endl;
}

void CWriter::functionParam(StrRef type, StrRef name) {
    out << type << " " << name;
}

void CWriter::nextFunctionParam() {
    out << ", ";
}

void CWriter::raw(StrRef text) {
    out << text;
}

void CWriter::line(StrRef line) {
    out << line << std::endl;
}

void CWriter::functionVarArgs() {
    out << "...";
}

void CWriter::startFunctionBody() {
    out << "{" << std::endl;
}

void CWriter::endFunctionBody() {
    out << "}" << std::endl;
}


void CWriter::startFunctionParams() {
    out << "(";
}

void CWriter::endFunctionParams() {
    out << ")";
}

void CWriter::declareVar(StrRef ty, StrRef name) {
    out << ty << " " << name << ";" << std::endl;
}

void CWriter::startBlock(StrRef label) {
    out << label << ":" << std::endl;
}

void CWriter::functionNoArgs() {
    out << "void";
}

void CWriter::startUnion(StrRef name) {
    out << "union " << name << " {" << std::endl;
}
