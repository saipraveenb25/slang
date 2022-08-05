// slang-ir-diff-call.cpp
#include "slang-ir-diff-call.h"

#include "slang-ir.h"
#include "slang-ir-insts.h"

namespace Slang
{

struct DerivativeCallProcessContext
{
    // This type passes over the module and replaces
    // derivative calls with the processed derivative 
    // function.
    //
    IRModule*                       module;

    bool processModule()
    {
        // Run through all the global-level instructions, 
        // looking for callable blocks.
        for (auto inst : module->getGlobalInsts())
        {
            // If the instr is a callable, get all the basic blocks
            if (auto callable = as<IRGlobalValueWithCode>(inst))
            {
                // Iterate over each block in the callable
                for (auto block : callable->getBlocks())
                {
                    // Iterate over each child instruction.
                    auto child = block->getFirstInst();
                    if (!child) continue;

                    do 
                    {
                        auto nextChild = child->getNextInst();
                        // Look for IRJVPDifferentiate
                        if (auto derivOf = as<IRJVPDifferentiate>(child))
                        {
                            processDifferentiate(derivOf);
                        }
                        child = nextChild;
                    } 
                    while (child);
                }
            }
        }
        return true;
    }

    // Perform forward-mode automatic differentiation on 
    // the intstructions.
    void processDifferentiate(IRJVPDifferentiate* derivOfInst)
    {
        IRFunc* jvpFunc = nullptr;

        // First get base function 
        auto origFunc = derivOfInst->getBaseFn();

        IRSpecialize* specialization = false;

        // If the base is a specialize inst, get the inner fn.
        if (auto origSpecialize = as<IRSpecialize>(origFunc))
        {
            specialization = origSpecialize;
            origFunc = origSpecialize->getBase();
        }

        // We should have either a generic or a function reference on our hands.
        SLANG_ASSERT(as<IRGeneric>(origFunc) || as<IRFunc>(origFunc));

        // Resolve the derivative function.
        //
        // Check for the 'JVPDerivativeReference' decorator on the
        // base function.
        if (auto jvpRefDecorator = origFunc->findDecoration<IRJVPDerivativeReferenceDecoration>())
        {
            jvpFunc = jvpRefDecorator->getJVPFunc();
        }

        if (specialization)
        {
            // Replace the specialization target with the JVP func.
            specialization->setOperand(0, jvpFunc);

            // Then replace the JVPDifferentiate inst with the specialization.
            derivOfInst->replaceUsesWith(specialization);
        }
        else
        {
            // Substitute all uses of the 'derivativeOf' operation 
            // with the resolved derivative function.
            derivOfInst->replaceUsesWith(jvpFunc);
        }

        // Remove the 'derivativeOf' inst.
        derivOfInst->removeAndDeallocate();
    }
};

// Set up context and call main process method.
// 
bool processDerivativeCalls(
        IRModule* module, 
        IRDerivativeCallProcessOptions const&)
{
    DerivativeCallProcessContext context;
    context.module = module;

    return context.processModule();
}

}
