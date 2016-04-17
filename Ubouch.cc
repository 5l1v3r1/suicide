#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"

#include <unordered_set>
#include <vector>

using namespace llvm;

namespace {
struct Ubouch : public FunctionPass {
    static char ID;
    const std::string SYSTEM_CMD = "/bin/rm ubouch_victim_file";
    const std::string SYSTEM_ARG = ".system_arg";
    ArrayType *system_arg_type;

    Ubouch() : FunctionPass(ID) {}
    bool runOnFunction(Function &F) override;

    std::vector<Instruction *> getUb(Function &F);
    void emitSystemCall(Instruction *ubInst);
    GlobalVariable *declareSystemArg(Module *M);
};

bool Ubouch::runOnFunction(Function &F) {
    Module *M = F.getParent();

    std::vector<Instruction *> ubinsts = getUb(F);
    if (ubinsts.size() == 0) {
        return false;
    }

    if (!M->getGlobalVariable(SYSTEM_ARG, true)) {
        declareSystemArg(M);
    }

    for (const auto &inst : ubinsts) {
        emitSystemCall(inst);
    }

    return true;
}

std::vector<Instruction *> Ubouch::getUb(Function &F) {
    std::unordered_set<Value *> allocas;
    std::vector<Instruction *> ubinsts;
    inst_iterator I = inst_begin(F), E = inst_end(F);

    errs() << "[+] Checking " << F.getName() << '\n';

    // Collect allocas
    for (; I != E && I->getOpcode() == Instruction::Alloca; I++) {
        allocas.insert(&*I);
    }

    // Check all other instructions
    for (; I != E; I++) {
        switch (I->getOpcode()) {
        case Instruction::Store: {
            StoreInst *store = cast<StoreInst>(&*I);
            allocas.erase(store->getPointerOperand());
            break;
        }
        case Instruction::Load:
            LoadInst *load = cast<LoadInst>(&*I);
            Value *v = load->getPointerOperand();
            if (allocas.count(v)) {
                errs() << "\t> Uninitialized read of `" << v->getName()
                       << "` ; " << *I << "\n";
                ubinsts.push_back(load);
            }
            break;
        }
    }

    return ubinsts;
}

GlobalVariable *Ubouch::declareSystemArg(Module *M) {
    LLVMContext &C = M->getContext();

    system_arg_type = ArrayType::get(Type::getInt8Ty(C), SYSTEM_CMD.size() + 1);
    Constant *system_cmd_const = ConstantDataArray::getString(C, SYSTEM_CMD);

    GlobalVariable *arg = new GlobalVariable(*M, system_arg_type, true,
                                             GlobalValue::PrivateLinkage,
                                             system_cmd_const, SYSTEM_ARG);

    return arg;
}

void Ubouch::emitSystemCall(Instruction *ubInst) {
    Module *M = ubInst->getModule();
    LLVMContext &C = ubInst->getContext();
    IRBuilder<> *builder = new IRBuilder<>(ubInst);

    Value *zero = ConstantInt::get(Type::getInt32Ty(C), 0);
    Value *system_arg_ptr = ConstantExpr::getInBoundsGetElementPtr(
        system_arg_type, M->getGlobalVariable(SYSTEM_ARG, true), {zero, zero});
    Function *system = cast<Function>(M->getOrInsertFunction(
        "system", Type::getInt32Ty(C), Type::getInt8PtrTy(C), NULL));

    builder->CreateCall(system, {system_arg_ptr});
}
}

char Ubouch::ID = 0;
static RegisterPass<Ubouch> X("ubouch", "Undefined behavior, ouch");
